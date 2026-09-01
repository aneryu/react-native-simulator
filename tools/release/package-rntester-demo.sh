#!/bin/sh

set -eu

project_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
build_dir=${1:-"$project_root/build/release"}
rntester_dir=${2:-"$project_root/build/release-rntester"}
output_dir=${3:-"$project_root/dist"}
release_version=$(sed -n \
  's/^project(ReactNativeSimulator VERSION \([^ ]*\) .*/\1/p' \
  "$project_root/CMakeLists.txt")
react_native_version=$(sed -n \
  's/^[[:space:]]*"version": "\([^"]*\)",/\1/p' \
  "$project_root/third_party/react-native/packages/react-native/package.json" | \
  sed -n '1p')
hermes_version=$(sed -n \
  's/^set(RNS_EXPECTED_HERMES_TAG "hermes-v\([^"]*\)").*/\1/p' \
  "$project_root/cmake/DependencyVersions.cmake")
addon_abi=$(sed -n \
  's/.*kSimulatorAddonAbiVersion = \([0-9][0-9]*\);/\1/p' \
  "$project_root/runtime/include/react-native-simulator/SimulatorAddon.h")
if [ -z "$release_version" ] || [ -z "$react_native_version" ] || \
    [ -z "$hermes_version" ] || [ -z "$addon_abi" ]; then
  echo "Cannot resolve release compatibility metadata" >&2
  exit 1
fi

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
  exit 1
fi

case "$build_dir" in
  /*) ;;
  *) build_dir="$project_root/$build_dir" ;;
esac
case "$rntester_dir" in
  /*) ;;
  *) rntester_dir="$project_root/$rntester_dir" ;;
esac
case "$output_dir" in
  /*) ;;
  *) output_dir="$project_root/$output_dir" ;;
esac

bundle="$rntester_dir/RNTesterApp.android.jsbundle"
hermesc="$build_dir/bin/hermesc"
if [ ! -f "$bundle" ]; then
  echo "RN Tester bundle is missing: $bundle" >&2
  echo "Run: node tools/rntester/bundle.mjs --dev false --out-dir $rntester_dir --build-dir $build_dir" >&2
  exit 1
fi
if [ ! -f "$build_dir/runtime/rns-addon-rntester.dylib" ]; then
  echo "RN Tester addon is missing: $build_dir/runtime/rns-addon-rntester.dylib" >&2
  exit 1
fi
if [ ! -x "$hermesc" ]; then
  echo "Hermes compiler is missing: $hermesc" >&2
  exit 1
fi

stage_root=$(mktemp -d "${TMPDIR:-/tmp}/rntester-demo.XXXXXX")
trap 'rm -rf "$stage_root"' EXIT HUP INT TERM
demo_root="$stage_root/rnsim-rntester-demo"
mkdir -p "$demo_root"
cmake --install "$build_dir" \
  --prefix "$demo_root" \
  --component rntester-demo
addon="$demo_root/rns-addon-rntester.dylib"
if [ ! -f "$addon" ] || \
    ! file "$addon" | grep -q 'Mach-O 64-bit bundle arm64'; then
  echo "rntester-demo component did not install an arm64 addon at $addon" >&2
  exit 1
fi
if otool -L "$addon" | tail -n +2 | awk '{print $1}' | \
    grep -Ev '^(@|/System/|/usr/lib/)' >/dev/null; then
  echo "non-relocatable dependency remains in $addon" >&2
  otool -L "$addon" >&2
  exit 1
fi
if otool -l "$addon" | awk '
    $1 == "cmd" && $2 == "LC_RPATH" {want = 1; next}
    want && $1 == "path" {print $2; want = 0}
  ' | grep -Ev '^@' >/dev/null; then
  echo "non-relocatable rpath remains in $addon" >&2
  exit 1
fi
codesign --force --sign - "$addon"
codesign --verify --strict --verbose=2 "$addon"
minimum_macos=$(vtool -show-build "$addon" 2>/dev/null | \
  awk '$1 == "minos" {print $2; exit}')
if [ -z "$minimum_macos" ] || ! awk -v actual="$minimum_macos" 'BEGIN {
    split(actual, a, ".");
    exit !((a[1] + 0) < 15 || ((a[1] + 0) == 15 && (a[2] + 0) <= 0))
  }'; then
  echo "RN Tester addon requires macOS ${minimum_macos:-unknown}, above 15.0" >&2
  exit 1
fi
if strings -a "$addon" | grep -Fq "$project_root" || \
    strings -a "$addon" | grep -Eq '/Users/[^/]+/'; then
  echo "Build-user or checkout path leaked into RN Tester addon" >&2
  exit 1
fi

# Compile with the exact Hermes revision shipped by this runtime. Loading HBC
# avoids parsing and compiling the full RN Tester source bundle at startup.
"$hermesc" -w -O -emit-binary \
  -out "$demo_root/RNTesterApp.android.hbc" "$bundle"
cp "$rntester_dir/rntester-startup-adapter.js" "$demo_root/"
if [ -d "$rntester_dir/assets" ]; then
  cp -R "$rntester_dir/assets" "$demo_root/assets"

  # Metro emits a few byte-identical RN Tester fixtures under distinct asset
  # names. Preserve every lookup path while storing the bytes once.
  deduplicate_asset() {
    canonical=$1
    shift
    for duplicate in "$@"; do
      if ! cmp -s "$canonical" "$duplicate"; then
        echo "Expected duplicate RN Tester assets differ: $duplicate" >&2
        exit 1
      fi
      rm "$duplicate"
      ln "$canonical" "$duplicate"
    done
  }
  deduplicate_asset \
    "$demo_root/assets/drawable-mdpi/js_assets_largeimage1.png" \
    "$demo_root/assets/drawable-mdpi/js_assets_largeimage2.png" \
    "$demo_root/assets/drawable-mdpi/js_assets_largeimage3.png" \
    "$demo_root/assets/drawable-mdpi/js_assets_largeimage4.png"
  deduplicate_asset \
    "$demo_root/assets/drawable/legacy_image.png" \
    "$demo_root/assets/drawable-mdpi/js_assets_hawk.png"
fi

cat >"$demo_root/rnsim.json" <<EOF
{
  "schemaVersion": 1,
  "reactNative": "$react_native_version",
  "platform": "android",
  "appKey": "RNTesterApp",
  "bundle": "./RNTesterApp.android.hbc",
  "viewport": {
    "width": 392.7273,
    "height": 753.4545,
    "pointScaleFactor": 2.75
  },
  "addons": ["./rns-addon-rntester.dylib"]
}
EOF

"$project_root/tools/release/generate-release-manifest.sh" \
  "$demo_root/manifest.json" demo "$build_dir/runtime/rnsim"

cp "$project_root/LICENSE" "$project_root/NOTICE" \
  "$project_root/THIRD_PARTY_NOTICES.md" "$demo_root/"
"$project_root/tools/release/collect-licenses.sh" "$demo_root/licenses"
SOURCE_DATE_EPOCH=$(git -C "$project_root" show -s --format=%ct HEAD) \
  "$project_root/tools/release/generate-sbom.sh" \
  "$demo_root/SBOM.spdx.json" demo

printf 'RN Tester demo support package for React Native Simulator v%s\n\n' \
  "$release_version" >"$demo_root/README.txt"
cat >>"$demo_root/README.txt" <<'EOF'
This package contains executable arm64 addon code. Before extracting it, verify
the adjacent .sha256 file from the release. Because this release is ad-hoc
signed rather than Developer ID signed and notarized, remove quarantine after
you trust the downloaded asset:
  xattr -dr com.apple.quarantine rnsim-rntester-demo

Interactive:
  rnsim --config rnsim-rntester-demo/rnsim.json

Finite headless startup verification:
  cd rnsim-rntester-demo
  rnsim headless \
    --config ./rnsim.json \
    --bundle ./RNTesterApp.android.hbc \
    --bundle ./rntester-startup-adapter.js \
    --timeout-ms 15000

Compatibility is recorded in manifest.json. Use this package only with the
matching React Native Simulator runtime asset.

Typography uses the host macOS font fallback. This compact demo is not an
Android typography or pixel-certification artifact.
EOF

mkdir -p "$output_dir"
archive_name="rnsim-rntester-demo-v${release_version}-macos-arm64.tar.gz"
archive="$output_dir/$archive_name"
SOURCE_DATE_EPOCH=$(git -C "$project_root" show -s --format=%ct HEAD) \
  "$project_root/tools/release/create-reproducible-tar.sh" \
  "$stage_root" rnsim-rntester-demo "$archive"
(cd "$output_dir" && \
  shasum -a 256 "$archive_name" >"$archive_name.sha256")
echo "Wrote $archive"
