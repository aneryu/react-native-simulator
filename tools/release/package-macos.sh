#!/bin/sh

set -eu

project_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
build_dir=${1:-"$project_root/build/release"}
output_dir=${2:-"$project_root/dist"}
case "$build_dir" in /*) ;; *) build_dir="$project_root/$build_dir" ;; esac
case "$output_dir" in /*) ;; *) output_dir="$project_root/$output_dir" ;; esac

channel=$(sed -n 's/^set(RNS_RELEASE_CHANNEL "\([^"]*\)").*/\1/p' \
  "$project_root/CMakeLists.txt")
[ "$channel" = nightly ] || { echo "Only the nightly channel can be packaged." >&2; exit 1; }

commit=$(git -C "$project_root" rev-parse HEAD)
if [ -n "$(git -C "$project_root" status --porcelain)" ] && \
   [ "${RNS_ALLOW_DIRTY_PACKAGE:-0}" != 1 ]; then
  echo "Release packaging requires a clean working tree." >&2
  exit 1
fi
if git -C "$project_root" submodule status --recursive | grep -Eq '^[+-U]'; then
  echo "Release packaging requires pinned, clean submodules." >&2
  exit 1
fi

source_binary="$build_dir/runtime/rnsim"
[ -x "$source_binary" ] || { echo "rnsim is not built at $source_binary" >&2; exit 1; }
file "$source_binary" | grep -q 'Mach-O 64-bit executable arm64' || {
  file "$source_binary" >&2
  echo "Nightly requires an arm64 Mach-O executable." >&2
  exit 1
}
build_info=$("$source_binary" --version --json)
printf '%s\n' "$build_info" | grep -Fq "\"channel\":\"$channel\"" || {
  echo "Built rnsim channel does not match $channel." >&2; exit 1;
}
printf '%s\n' "$build_info" | grep -Fq "\"commit\":\"$commit\"" || {
  echo "Built rnsim commit does not match HEAD; reconfigure and rebuild." >&2; exit 1;
}
if [ "${RNS_ALLOW_DIRTY_PACKAGE:-0}" != 1 ]; then
  printf '%s\n' "$build_info" | grep -Fq '"dirty":false' || {
    echo "Official Nightly must be rebuilt from a clean checkout." >&2; exit 1;
  }
fi

stage=$(mktemp -d "${TMPDIR:-/tmp}/rnsim-package.XXXXXX")
cleanup() { rm -rf "$stage"; }
trap cleanup EXIT HUP INT TERM
cp "$source_binary" "$stage/rnsim"
chmod 755 "$stage/rnsim"
xattr -c "$stage/rnsim" 2>/dev/null || true
strip -x "$stage/rnsim"

non_system=$(otool -L "$stage/rnsim" | tail -n +2 | awk '{print $1}' | \
  grep -Ev '^(/System/|/usr/lib/)' || true)
if [ -n "$non_system" ]; then
  echo "rnsim is not a single self-contained executable:" >&2
  printf '%s\n' "$non_system" >&2
  exit 1
fi
if strings -a "$stage/rnsim" | grep -Fq "$project_root"; then
  echo "Source checkout path leaked into rnsim." >&2
  exit 1
fi

mkdir -p "$output_dir"
dmg_name="rnsim-${channel}-macos-arm64.dmg"
dmg="$output_dir/$dmg_name"
rm -f "$dmg" "$dmg.sha256"
"$project_root/tools/release/sign-and-notarize.sh" "$stage/rnsim" "$dmg"
(cd "$output_dir" && shasum -a 256 "$dmg_name" >"$dmg_name.sha256")

echo "Wrote $dmg and $dmg.sha256"
