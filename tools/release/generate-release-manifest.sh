#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
  echo "Usage: generate-release-manifest.sh OUTPUT runtime|demo RNSIM_BINARY" >&2
  exit 2
fi

output=$1
package_kind=$2
runtime=$3
case "$package_kind" in runtime|demo) ;; *) exit 2 ;; esac
project_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
channel=$(sed -n \
  's/^set(RNS_RELEASE_CHANNEL "\([^"]*\)").*/\1/p' \
  "$project_root/CMakeLists.txt")
commit=$(git -C "$project_root" rev-parse HEAD)
dirty=false
if [ -n "$(git -C "$project_root" status --porcelain)" ]; then dirty=true; fi
rn_commit=$(git -C "$project_root/third_party/react-native" rev-parse HEAD)
hermes_commit=$(git -C "$project_root/third_party/hermes" rev-parse HEAD)
fast_float_commit=$(git -C "$project_root/third_party/fast_float" rev-parse HEAD)
skia_commit=$(git -C "$project_root/third_party/skia" rev-parse HEAD)
imgui_commit=$(git -C "$project_root/third_party/imgui" rev-parse HEAD)
sdl_commit=$(git -C "$project_root/third_party/sdl" rev-parse HEAD)
clang_version=$(xcrun clang --version | sed -n '1p')
sdk_version=$(xcrun --show-sdk-version)
built_at=$(date -u -r "$(git -C "$project_root" show -s --format=%ct HEAD)" \
  '+%Y-%m-%dT%H:%M:%SZ')
build_info=$($runtime --version --json)

printf '%s\n' \
  '{' \
  "  \"schemaVersion\": 1," \
  "  \"package\": \"$package_kind\"," \
  "  \"channel\": \"$channel\"," \
  "  \"gitCommit\": \"$commit\"," \
  "  \"dirty\": $dirty," \
  "  \"sourceDate\": \"$built_at\"," \
  '  "reactNative": "0.87.0",' \
  '  "hermes": "260318099.0.1",' \
  '  "addonAbi": 2,' \
  '  "architecture": "arm64",' \
  '  "minimumMacOS": "15.0",' \
  "  \"toolchain\": \"$clang_version\"," \
  "  \"macosSdk\": \"$sdk_version\"," \
  '  "submodules": {' \
  "    \"reactNative\": \"$rn_commit\"," \
  "    \"hermes\": \"$hermes_commit\"," \
  "    \"fastFloat\": \"$fast_float_commit\"," \
  "    \"skia\": \"$skia_commit\"," \
  "    \"imgui\": \"$imgui_commit\"," \
  "    \"sdl\": \"$sdl_commit\"" \
  '  },' \
  '  "build":' \
  "  $build_info" \
  '}' >"$output"
