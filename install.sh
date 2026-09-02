#!/bin/sh

set -eu

repo=${RNS_RELEASE_REPOSITORY:-aneryu/react-native-simulator}
prefix=${RNS_INSTALL_PREFIX:-${HOME:?HOME is required}/.local}
operation=install

usage() {
  echo "Usage: install.sh [--prefix DIR]"
  echo "       install.sh --uninstall [--prefix DIR]"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --prefix)
      shift
      [ "$#" -gt 0 ] || { echo "--prefix requires a directory" >&2; exit 2; }
      prefix=$1
      ;;
    --uninstall) operation=uninstall ;;
    --help|-h) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done
case "$prefix" in /*) ;; *) prefix=$PWD/$prefix ;; esac
destination="$prefix/bin/rnsim"

is_managed_rnsim() {
  [ -f "$1" ] || return 1
  codesign -dv --verbose=4 "$1" 2>&1 | \
    grep -Fq 'Identifier=dev.reactnativesimulator.rnsim'
}

if [ "$operation" = uninstall ]; then
  if ! is_managed_rnsim "$destination"; then
    echo "No managed Nightly rnsim exists at $destination" >&2
    exit 1
  fi
  rm -f "$destination"
  echo "Removed $destination"
  exit 0
fi

command -v curl >/dev/null || { echo "curl is required" >&2; exit 1; }
command -v hdiutil >/dev/null || { echo "hdiutil is required" >&2; exit 1; }
if [ -e "$destination" ] && ! is_managed_rnsim "$destination"; then
  echo "Refusing to replace an unmanaged file: $destination" >&2
  exit 1
fi

asset=rnsim-nightly-macos-arm64.dmg
base="https://github.com/$repo/releases/download/nightly"
stage=$(mktemp -d "${TMPDIR:-/tmp}/rnsim-install.XXXXXX")
mountpoint="$stage/mount"
mkdir "$mountpoint"
attached=0
cleanup() {
  if [ "$attached" -eq 1 ]; then hdiutil detach "$mountpoint" -quiet || true; fi
  rm -rf "$stage"
}
trap cleanup EXIT HUP INT TERM

curl --proto '=https' --tlsv1.2 -fL --retry 3 \
  "$base/$asset" -o "$stage/$asset"
curl --proto '=https' --tlsv1.2 -fL --retry 3 \
  "$base/$asset.sha256" -o "$stage/$asset.sha256"
(cd "$stage" && shasum -a 256 -c "$asset.sha256")
hdiutil verify "$stage/$asset" >/dev/null
codesign --verify --strict --verbose=2 "$stage/$asset"
xcrun stapler validate "$stage/$asset"
spctl --assess --type open --context context:primary-signature \
  --verbose=2 "$stage/$asset"

hdiutil attach "$stage/$asset" -readonly -nobrowse \
  -mountpoint "$mountpoint" >/dev/null
attached=1
entries=$(find "$mountpoint" -mindepth 1 -maxdepth 1 ! -name '.DS_Store' -print)
[ "$entries" = "$mountpoint/rnsim" ] && [ -f "$mountpoint/rnsim" ] || {
  echo "Nightly DMG does not contain exactly one rnsim executable." >&2
  exit 1
}
codesign --verify --strict --verbose=2 "$mountpoint/rnsim"
signature=$(codesign -dv --verbose=4 "$mountpoint/rnsim" 2>&1)
printf '%s\n' "$signature" | grep -Fq 'Authority=Developer ID Application:'
printf '%s\n' "$signature" | grep -Eq 'flags=.*(runtime|0x10000)'

mkdir -p "$prefix/bin"
temporary=$(mktemp "$prefix/bin/.rnsim.XXXXXX")
cp "$mountpoint/rnsim" "$temporary"
chmod 755 "$temporary"
mv -f "$temporary" "$destination"
hdiutil detach "$mountpoint" -quiet
attached=0

echo "Installed React Native Simulator Nightly at $destination"
case ":${PATH:-}:" in
  *":$prefix/bin:"*) ;;
  *) echo "Add to PATH: export PATH=\"$prefix/bin:\$PATH\"" ;;
esac
