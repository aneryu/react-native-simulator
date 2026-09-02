#!/bin/sh

set -eu

# Create a Developer ID signed, notarized, and stapled DMG containing exactly
# one executable named rnsim.
# Usage: sign-and-notarize.sh RNSIM OUTPUT.dmg

if [ "$#" -ne 2 ]; then
  echo "Usage: sign-and-notarize.sh RNSIM OUTPUT.dmg" >&2
  exit 2
fi

input=$1
output=$2
case "$input" in /*) ;; *) input=$PWD/$input ;; esac
case "$output" in /*) ;; *) output=$PWD/$output ;; esac
[ -f "$input" ] || { echo "rnsim does not exist: $input" >&2; exit 1; }
case "$output" in *.dmg) ;; *) echo "Output must end in .dmg: $output" >&2; exit 1 ;; esac

script_dir=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
RNS_CODESIGN_LIB_DIR=$script_dir
# shellcheck disable=SC1091
. "$RNS_CODESIGN_LIB_DIR/macos-codesign.sh"

if [ -z "${RNS_CODESIGN_IDENTITY:-}" ]; then
  identities=$(security find-identity -v -p codesigning | \
    sed -n 's/^.*"\(Developer ID Application: [^"]*\)".*$/\1/p')
  identity_count=$(printf '%s\n' "$identities" | sed '/^$/d' | wc -l | tr -d ' ')
  if [ "$identity_count" != 1 ]; then
    echo "Set RNS_CODESIGN_IDENTITY; expected one Developer ID Application identity, found $identity_count." >&2
    exit 1
  fi
  RNS_CODESIGN_IDENTITY=$identities
  export RNS_CODESIGN_IDENTITY
fi
identity=$(rns_codesign_identity)
security find-identity -v -p codesigning | grep -F "$identity" | \
  grep -Fq 'Developer ID Application' || {
    echo "Developer ID Application identity is unavailable: $identity" >&2
    exit 1
  }

if [ -n "${RNS_NOTARY_KEY:-}" ] && {
    [ -z "${RNS_NOTARY_KEY_ID:-}" ] || [ -z "${RNS_NOTARY_ISSUER_ID:-}" ];
  }; then
  echo "RNS_NOTARY_KEY requires RNS_NOTARY_KEY_ID and RNS_NOTARY_ISSUER_ID." >&2
  exit 1
fi

stage=$(mktemp -d "${TMPDIR:-/tmp}/rnsim-notarize.XXXXXX")
cleanup() { rm -rf "$stage"; }
trap cleanup EXIT HUP INT TERM
mkdir -p "$stage/volume" "$stage/auth" "$(dirname "$output")"
cp "$input" "$stage/volume/rnsim"
chmod 755 "$stage/volume/rnsim"
xattr -c "$stage/volume/rnsim" 2>/dev/null || true
rns_codesign_file "$stage/volume/rnsim"
rns_codesign_verify_file "$stage/volume/rnsim"

rns_notary_keyfile() {
  keyfile=$RNS_NOTARY_KEY
  if [ ! -f "$keyfile" ]; then
    keyfile="$stage/auth/AuthKey.p8"
    umask 077
    printf '%s\n' "$RNS_NOTARY_KEY" >"$keyfile"
  fi
  printf '%s\n' "$keyfile"
}

rns_notary_submit() {
  submission=$1
  status=0
  if [ -n "${RNS_NOTARY_KEY:-}" ]; then
    result=$(xcrun notarytool submit "$submission" \
      --key "$(rns_notary_keyfile)" \
      --key-id "$RNS_NOTARY_KEY_ID" \
      --issuer "$RNS_NOTARY_ISSUER_ID" \
      --wait --timeout 30m 2>&1) || status=$?
  else
    result=$(xcrun notarytool submit "$submission" \
      --keychain-profile "${RNS_NOTARY_KEYCHAIN_PROFILE:-rnsim-notary}" \
      --wait --timeout 30m 2>&1) || status=$?
  fi
  printf '%s\n' "$result"
  if [ "$status" -ne 0 ] || ! printf '%s\n' "$result" | \
      grep -Eq 'status:[[:space:]]*Accepted'; then
    echo "Notarization failed for $submission" >&2
    exit 1
  fi
}

unsigned="$stage/rnsim.dmg"
hdiutil create -volname 'React Native Simulator Nightly' \
  -srcfolder "$stage/volume" -ov -format UDZO "$unsigned" >/dev/null
codesign --force --sign "$identity" --timestamp "$unsigned"
codesign --verify --strict --verbose=2 "$unsigned"
rns_notary_submit "$unsigned"
xcrun stapler staple "$unsigned"
xcrun stapler validate "$unsigned"
mv "$unsigned" "$output"

echo "Wrote signed, notarized, and stapled $output"
