#!/bin/sh

set -eu

project_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
dist_dir=${1:-"$project_root/dist"}
channel=$(sed -n 's/^set(RNS_RELEASE_CHANNEL "\([^"]*\)").*/\1/p' \
  "$project_root/CMakeLists.txt")
dmg="$dist_dir/rnsim-${channel}-macos-arm64.dmg"
checksum="$dmg.sha256"
[ -f "$dmg" ] && [ -f "$checksum" ] || {
  echo "Missing Nightly DMG or checksum in $dist_dir." >&2; exit 1;
}
(cd "$dist_dir" && shasum -a 256 -c "$(basename "$checksum")")
hdiutil verify "$dmg" >/dev/null
codesign --verify --strict --verbose=2 "$dmg"
xcrun stapler validate "$dmg"
spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"

stage=$(mktemp -d "${TMPDIR:-/tmp}/rnsim-verify.XXXXXX")
mountpoint="$stage/mount"
mkdir "$mountpoint"
attached=0
cleanup() {
  if [ "$attached" -eq 1 ]; then hdiutil detach "$mountpoint" -quiet || true; fi
  rm -rf "$stage"
}
trap cleanup EXIT HUP INT TERM
hdiutil attach "$dmg" -readonly -nobrowse -mountpoint "$mountpoint" >/dev/null
attached=1
entries=$(find "$mountpoint" -mindepth 1 -maxdepth 1 ! -name '.DS_Store' -print)
[ "$entries" = "$mountpoint/rnsim" ] && [ -f "$mountpoint/rnsim" ] || {
  echo "Nightly DMG must contain exactly one file named rnsim." >&2
  find "$mountpoint" -mindepth 1 -maxdepth 1 -print >&2
  exit 1
}
cp "$mountpoint/rnsim" "$stage/rnsim"
hdiutil detach "$mountpoint" -quiet
attached=0
chmod 755 "$stage/rnsim"

file "$stage/rnsim" | grep -q 'Mach-O 64-bit executable arm64'
non_system=$(otool -L "$stage/rnsim" | tail -n +2 | awk '{print $1}' | \
  grep -Ev '^(/System/|/usr/lib/)' || true)
[ -z "$non_system" ] || {
  echo "Packaged rnsim has non-system dynamic dependencies:" >&2
  printf '%s\n' "$non_system" >&2
  exit 1
}
codesign --verify --strict --verbose=2 "$stage/rnsim"
signature=$(codesign -dv --verbose=4 "$stage/rnsim" 2>&1)
printf '%s\n' "$signature" | grep -Fq 'Authority=Developer ID Application:'
printf '%s\n' "$signature" | grep -Eq 'flags=.*(runtime|0x10000)'

version=$("$stage/rnsim" --version --json)
commit=$(git -C "$project_root" rev-parse HEAD)
printf '%s\n' "$version" | grep -Fq "\"channel\":\"$channel\""
printf '%s\n' "$version" | grep -Fq "\"commit\":\"$commit\""
printf '%s\n' "$version" | grep -Fq '"dirty":false'
printf '%s\n' "$version" | grep -Fq '"minimumMacOS":"15.0"'
doctor=$(cd "$project_root/tests/fixtures/doctor-project" && "$stage/rnsim" doctor --json)
printf '%s\n' "$doctor" | grep -Fq '"securitySandbox":false'

smoke="$stage/smoke.json"
"$stage/rnsim" headless \
  --bundle "$project_root/tests/fixtures/runtime-smoke.js" \
  --iterations 5 --timeout-ms 1000 >"$smoke"
grep -Fq '"workloadComplete":true' "$smoke"
grep -Fq '"pendingWork":false' "$smoke"
grep -Fq '"jsErrors":0' "$smoke"

echo "Verified one-file Developer ID signed, notarized, and stapled Nightly DMG."
