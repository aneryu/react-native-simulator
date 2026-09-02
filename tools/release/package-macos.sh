#!/bin/sh

set -eu

project_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
RNS_CODESIGN_LIB_DIR="$project_root/tools/release"
# shellcheck disable=SC1091
. "$RNS_CODESIGN_LIB_DIR/macos-codesign.sh"
build_dir=${1:-"$project_root/build/release"}
output_dir=${2:-"$project_root/dist"}
release_version=$(sed -n \
  's/^project(ReactNativeSimulator VERSION \([^ ]*\) .*/\1/p' \
  "$project_root/CMakeLists.txt")
if [ -z "$release_version" ]; then
  echo "Cannot read ReactNativeSimulator project version" >&2
  exit 1
fi

release_commit=$(git -C "$project_root" rev-parse HEAD)
release_status=$(git -C "$project_root" status --porcelain)
if [ -n "$release_status" ]; then
  if [ "${RNS_ALLOW_DIRTY_PACKAGE:-0}" != 1 ]; then
    echo "Release packaging requires a clean working tree." >&2
    echo "Use RNS_ALLOW_DIRTY_PACKAGE=1 only for local pre-release verification." >&2
    exit 1
  fi
fi
if git -C "$project_root" submodule status --recursive | grep -Eq '^[+-U]'; then
  echo "Release packaging requires every submodule at its recorded commit." >&2
  git -C "$project_root" submodule status --recursive >&2
  exit 1
fi

case "$build_dir" in
  /*) ;;
  *) build_dir="$project_root/$build_dir" ;;
esac
case "$output_dir" in
  /*) ;;
  *) output_dir="$project_root/$output_dir" ;;
esac

if [ ! -x "$build_dir/runtime/rnsim" ]; then
  echo "rnsim is not built at $build_dir/runtime/rnsim" >&2
  exit 1
fi

if ! file "$build_dir/runtime/rnsim" | grep -q 'Mach-O 64-bit executable arm64'; then
  echo "release packaging currently requires an arm64 rnsim binary" >&2
  file "$build_dir/runtime/rnsim" >&2
  exit 1
fi
build_info=$("$build_dir/runtime/rnsim" --version --json)
printf '%s\n' "$build_info" | grep -Fq "\"version\":\"$release_version\"" || {
  echo "Built rnsim version does not match $release_version" >&2
  exit 1
}
printf '%s\n' "$build_info" | grep -Fq "\"commit\":\"$release_commit\"" || {
  echo "Built rnsim commit does not match $release_commit; reconfigure and rebuild." >&2
  exit 1
}

stage_root=$(mktemp -d "${TMPDIR:-/tmp}/rnsim-release.XXXXXX")
trap 'rm -rf "$stage_root"' EXIT HUP INT TERM
install_root="$stage_root/rnsim"

cmake --install "$build_dir" \
  --prefix "$install_root" \
  --component react-native-simulator
cp "$project_root/LICENSE" "$project_root/NOTICE" \
  "$project_root/README.md" "$project_root/SECURITY.md" \
  "$project_root/THIRD_PARTY_NOTICES.md" "$install_root/"
mkdir -p "$install_root/docs"
cp "$project_root/docs/guides/GETTING_STARTED.md" \
  "$project_root/docs/guides/TROUBLESHOOTING.md" \
  "$install_root/docs/"
cp "$project_root/tools/release/install.sh" "$install_root/install.sh"
chmod +x "$install_root/install.sh"
printf '%s\n' "$release_version" >"$install_root/VERSION"
"$project_root/tools/release/collect-licenses.sh" "$install_root/licenses"
SOURCE_DATE_EPOCH=$(git -C "$project_root" show -s --format=%ct HEAD) \
  "$project_root/tools/release/generate-sbom.sh" \
  "$install_root/SBOM.spdx.json" runtime
"$project_root/tools/release/generate-release-manifest.sh" \
  "$install_root/release-manifest.json" runtime \
  "$build_dir/runtime/rnsim"

mkdir -p "$install_root/lib"
queue_file="$stage_root/macho-queue"
seen_file="$stage_root/macho-seen"
find "$install_root/bin" "$install_root/lib" -type f -print >"$queue_file"
: >"$seen_file"

is_system_dependency() {
  case "$1" in
    /System/*|/usr/lib/*|@*) return 0 ;;
    *) return 1 ;;
  esac
}

# Recursively vendor every non-system dynamic dependency. Homebrew formulae
# frequently depend on one another (for example Folly -> glog -> gflags), so
# auditing only rnsim itself is insufficient.
while IFS= read -r binary; do
  file "$binary" | grep -q 'Mach-O' || continue
  grep -Fqx "$binary" "$seen_file" && continue
  printf '%s\n' "$binary" >>"$seen_file"
  otool -L "$binary" | tail -n +2 | awk '{print $1}' | while IFS= read -r dependency; do
    is_system_dependency "$dependency" && continue
    if [ ! -f "$dependency" ]; then
      echo "unresolved dynamic dependency: $binary -> $dependency" >&2
      exit 1
    fi
    destination="$install_root/lib/$(basename "$dependency")"
    if [ ! -e "$destination" ]; then
      cp -L "$dependency" "$destination"
      chmod u+w "$destination"
      printf '%s\n' "$destination" >>"$queue_file"
    fi
  done
done <"$queue_file"

# Rewrite absolute dependency names to the bundled lib directory. Keep the
# rpath local to each binary class: executable, shared library, or addon.
while IFS= read -r binary; do
  file "$binary" | grep -q 'Mach-O' || continue
  chmod u+w "$binary"
  # Finder custom icons are extended attributes, not part of the CLI product.
  # They are rejected by strict code-signing validation, so keep them on local
  # build outputs only and clear them from release staging before sealing code.
  xattr -c "$binary"
  case "$binary" in
    "$install_root"/lib/react-native-simulator/addons/*.dylib)
      desired_rpath='@loader_path/../..'
      ;;
    "$install_root"/lib/*.dylib)
      install_name_tool -id "@rpath/$(basename "$binary")" "$binary"
      desired_rpath='@loader_path'
      ;;
    "$install_root"/bin/*)
      desired_rpath='@executable_path/../lib'
      ;;
    *) desired_rpath='@loader_path' ;;
  esac

  otool -L "$binary" | tail -n +2 | awk '{print $1}' | while IFS= read -r dependency; do
    is_system_dependency "$dependency" && continue
    install_name_tool -change "$dependency" \
      "@rpath/$(basename "$dependency")" "$binary"
  done

  otool -l "$binary" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" {want = 1; next}
    want && $1 == "path" {print $2; want = 0}
  ' | while IFS= read -r rpath; do
    case "$rpath" in
      @*) ;;
      *) install_name_tool -delete_rpath "$rpath" "$binary" ;;
    esac
  done
  current_rpaths=$(otool -l "$binary" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" {want = 1; next}
    want && $1 == "path" {print $2; want = 0}
  ')
  printf '%s\n' "$current_rpaths" | grep -Fqx "$desired_rpath" || \
    install_name_tool -add_rpath "$desired_rpath" "$binary"

  # Release archives do not need local Mach-O symbols. Keep exported symbols
  # intact for the embedding dylib and addons while reducing disk footprint.
  strip -x "$binary"
  # Apple Silicon requires a valid signature even without a Developer ID.
  # Relocation and stripping happen after link-time signing, so seal the final
  # bytes here. Default identity is ad-hoc so archives stay reproducible;
  # Developer ID + notarization is a separate sign-and-notarize.sh step.
  rns_codesign_file "$binary"
done <"$seen_file"

# Fail closed if packaging missed an absolute, non-system dependency or rpath.
audit_failed=0
while IFS= read -r binary; do
  file "$binary" | grep -q 'Mach-O' || continue
  if otool -L "$binary" | tail -n +2 | awk '{print $1}' | \
      grep -Ev '^(@|/System/|/usr/lib/)' >/dev/null; then
    echo "non-relocatable dependency remains in $binary" >&2
    otool -L "$binary" >&2
    audit_failed=1
  fi
  if otool -l "$binary" | awk '
      $1 == "cmd" && $2 == "LC_RPATH" {want = 1; next}
      want && $1 == "path" {print $2; want = 0}
    ' | grep -Ev '^@' >/dev/null; then
    echo "non-relocatable rpath remains in $binary" >&2
    audit_failed=1
  fi
  if ! rns_codesign_verify_file "$binary"; then
    echo "invalid code signature in $binary" >&2
    audit_failed=1
  fi
  minimum_macos=$(vtool -show-build "$binary" 2>/dev/null | \
    awk '$1 == "minos" {print $2; exit}')
  if [ -z "$minimum_macos" ]; then
    echo "Mach-O has no LC_BUILD_VERSION minos: $binary" >&2
    audit_failed=1
  elif ! awk -v actual="$minimum_macos" -v declared=15.0 'BEGIN {
      split(actual, a, "."); split(declared, d, ".");
      exit !((a[1] + 0) < (d[1] + 0) ||
        ((a[1] + 0) == (d[1] + 0) && (a[2] + 0) <= (d[2] + 0)))
    }'; then
    echo "$binary requires macOS $minimum_macos, above declared 15.0" >&2
    audit_failed=1
  fi
  if strings -a "$binary" | grep -Fq "$project_root"; then
    echo "source checkout path leaked into $binary" >&2
    audit_failed=1
  fi
  if strings -a "$binary" | grep -Eq '/Users/[^/]+/'; then
    echo "build-user path leaked into $binary" >&2
    audit_failed=1
  fi
done <"$seen_file"
[ "$audit_failed" -eq 0 ] || exit 1

# Application-owned contracts must not leak into the generic runtime package.
for forbidden in \
    rntester \
    NativeCxxModuleExampleCxx \
    ScreenshotManager \
    RNTReportFullyDrawnView \
    RNTMyNativeView \
    RNTMyLegacyNativeView \
    AndroidPopupMenu; do
  if find "$install_root/bin" "$install_root/lib" -type f -print | \
      while IFS= read -r binary; do
        file "$binary" | grep -q 'Mach-O' || continue
        strings -a "$binary"
      done | grep -Fqi "$forbidden"; then
    echo "RN Tester contract leaked into runtime package: $forbidden" >&2
    exit 1
  fi
done

mkdir -p "$output_dir"
archive_name="rnsim-v${release_version}-macos-arm64.tar.gz"
archive="$output_dir/$archive_name"
SOURCE_DATE_EPOCH=$(git -C "$project_root" show -s --format=%ct HEAD) \
  "$project_root/tools/release/create-reproducible-tar.sh" \
  "$stage_root" rnsim "$archive"
(cd "$output_dir" && \
  shasum -a 256 "$archive_name" >"$archive_name.sha256")
echo "Wrote $archive"
