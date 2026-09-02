#!/bin/sh

set -eu

project_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
dist_dir=${1:-"$project_root/dist"}
version=$(sed -n \
  's/^project(ReactNativeSimulator VERSION \([^ ]*\) .*/\1/p' \
  "$project_root/CMakeLists.txt")
runtime_archive="$dist_dir/rnsim-v${version}-macos-arm64.tar.gz"
demo_archive="$dist_dir/rnsim-rntester-demo-v${version}-macos-arm64.tar.gz"

for archive in "$runtime_archive" "$demo_archive"; do
  if [ ! -f "$archive" ] || [ ! -f "$archive.sha256" ]; then
    echo "Missing release asset or checksum: $archive" >&2
    exit 1
  fi
  (cd "$dist_dir" && shasum -a 256 -c "$(basename "$archive").sha256")
done

verification_root=$(mktemp -d "${TMPDIR:-/tmp}/rnsim-release-verify.XXXXXX")
cleanup() {
  if [ -n "${verification_root:-}" ] && [ -d "$verification_root" ]; then
    rm -rf "$verification_root"
  fi
}
trap cleanup EXIT HUP INT TERM

tar xf "$runtime_archive" -C "$verification_root"
tar xf "$demo_archive" -C "$verification_root"
macho_list="$verification_root/macho-files"
find "$verification_root/rnsim" "$verification_root/rnsim-rntester-demo" \
  -type f -print >"$macho_list"
while IFS= read -r candidate; do
  file "$candidate" | grep -q 'Mach-O' || continue
  codesign --verify --strict --verbose=2 "$candidate"
done <"$macho_list"
prefix="$verification_root/prefix"
"$verification_root/rnsim/install.sh" --yes --prefix "$prefix"
runtime="$prefix/bin/rnsim"

version_json=$($runtime --version --json)
printf '%s\n' "$version_json" | grep -Fq "\"version\":\"$version\""
printf '%s\n' "$version_json" | grep -Fq '"minimumMacOS":"15.0"'
doctor_json=$($runtime doctor --json)
printf '%s\n' "$doctor_json" | grep -Fq '"securitySandbox":false'
printf '%s\n' "$doctor_json" | grep -Fq '"installedDevToolsFrontend":true'
printf '%s\n' "$doctor_json" | grep -Fq '"project":{'

demo="$verification_root/rnsim-rntester-demo"
(cd "$demo" && "$runtime" headless \
  --config ./rnsim.json \
  --bundle ./RNTesterApp.android.hbc \
  --bundle ./rntester-startup-adapter.js \
  --timeout-ms 15000) >"$verification_root/headless.json"
grep -Fq '"bundlesLoaded":2' "$verification_root/headless.json"
grep -Fq '"reactFabric":true' "$verification_root/headless.json"
grep -Fq '"workloadComplete":true' "$verification_root/headless.json"
grep -Fq '"pendingWork":false' "$verification_root/headless.json"
grep -Fq '"jsErrors":0' "$verification_root/headless.json"

smoke_result="$verification_root/interactive-smoke.json"
(cd "$demo" && RNS_INTERACTIVE_SMOKE_OUTPUT="$smoke_result" \
  "$runtime" --config ./rnsim.json) >"$verification_root/interactive.log" 2>&1
grep -Fq 'running AppRegistry application RNTesterApp' \
  "$verification_root/interactive.log"
grep -Fq '"ready":true' "$smoke_result"
grep -Eq '"frameWidth":[1-9][0-9]*' "$smoke_result"
grep -Eq '"sceneRevision":[0-9]+' "$smoke_result"

echo "Verified packaged runtime install, headless RN Tester, and interactive Skia frame."
