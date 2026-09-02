#!/bin/sh

set -eu

# Re-sign a packaged tar.gz with Developer ID, notarize it, staple a DMG, and
# replace the archive in place. Ad-hoc packaging stays in package-macos.sh so
# those tarballs remain byte-reproducible; Apple timestamps make this step
# inherently non-reproducible.
#
# Usage: sign-and-notarize.sh ARCHIVE.tar.gz
#
# Required:
#   RNS_CODESIGN_IDENTITY   Developer ID Application: Name (TEAMID)
#
# Notary auth, one of:
#   RNS_NOTARY_KEYCHAIN_PROFILE   default: rnsim-notary
#   RNS_NOTARY_KEY + RNS_NOTARY_KEY_ID + RNS_NOTARY_ISSUER_ID
#     (key path or PEM contents)
#
# Optional:
#   RNS_SKIP_DMG=1   notarize a zip only (no staple)

if [ "$#" -ne 1 ]; then
  echo "Usage: sign-and-notarize.sh ARCHIVE.tar.gz" >&2
  exit 2
fi

archive=$1
case "$archive" in
  /*) ;;
  *) archive=$PWD/$archive ;;
esac
if [ ! -f "$archive" ]; then
  echo "Archive does not exist: $archive" >&2
  exit 1
fi
case "$archive" in
  *.tar.gz) ;;
  *)
    echo "Expected a .tar.gz archive: $archive" >&2
    exit 1
    ;;
esac

script_dir=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
project_root=$(CDPATH='' cd -- "$script_dir/../.." && pwd)
RNS_CODESIGN_LIB_DIR=$script_dir
# shellcheck disable=SC1091
. "$RNS_CODESIGN_LIB_DIR/macos-codesign.sh"

if rns_codesign_is_adhoc; then
  echo "RNS_CODESIGN_IDENTITY must be set to a Developer ID Application identity." >&2
  echo "Apple Development certificates cannot be notarized for Gatekeeper." >&2
  exit 1
fi

identity=$(rns_codesign_identity)
if ! security find-identity -v -p codesigning | grep -Fq "$identity"; then
  echo "Signing identity is not in the keychain: $identity" >&2
  security find-identity -v -p codesigning >&2
  exit 1
fi
if ! security find-identity -v -p codesigning | grep -F "$identity" | \
    grep -Fq "Developer ID Application"; then
  echo "Identity is not a Developer ID Application certificate: $identity" >&2
  exit 1
fi

if [ -n "${RNS_NOTARY_KEY:-}" ]; then
  if [ -z "${RNS_NOTARY_KEY_ID:-}" ] || [ -z "${RNS_NOTARY_ISSUER_ID:-}" ]; then
    echo "RNS_NOTARY_KEY requires RNS_NOTARY_KEY_ID and RNS_NOTARY_ISSUER_ID." >&2
    exit 1
  fi
fi

stage=$(mktemp -d "${TMPDIR:-/tmp}/rnsim-notarize.XXXXXX")
cleanup() {
  rm -rf "$stage"
}
trap cleanup EXIT HUP INT TERM
mkdir "$stage/extract" "$stage/auth"

tar xf "$archive" -C "$stage/extract"
top_level=$(find "$stage/extract" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')
top_files=$(find "$stage/extract" -mindepth 1 -maxdepth 1 -type f | wc -l | tr -d ' ')
if [ "$top_level" != 1 ] || [ "$top_files" != 0 ]; then
  echo "Archive must contain exactly one top-level directory: $archive" >&2
  find "$stage/extract" -mindepth 1 -maxdepth 1 >&2
  exit 1
fi
payload=$(find "$stage/extract" -mindepth 1 -maxdepth 1 -type d)
payload_name=$(basename "$payload")

rns_codesign_tree "$payload"
rns_codesign_verify_tree "$payload"

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
  input=$1
  cmd_status=0
  if [ -n "${RNS_NOTARY_KEY:-}" ]; then
    keyfile=$(rns_notary_keyfile)
    output=$(xcrun notarytool submit "$input" \
      --key "$keyfile" \
      --key-id "$RNS_NOTARY_KEY_ID" \
      --issuer "$RNS_NOTARY_ISSUER_ID" \
      --wait --timeout 30m 2>&1) || cmd_status=$?
  else
    output=$(xcrun notarytool submit "$input" \
      --keychain-profile "${RNS_NOTARY_KEYCHAIN_PROFILE:-rnsim-notary}" \
      --wait --timeout 30m 2>&1) || cmd_status=$?
  fi
  printf '%s\n' "$output"
  if [ "$cmd_status" -ne 0 ] || \
      ! printf '%s\n' "$output" | grep -Eq 'status:[[:space:]]*Accepted'; then
    printf '%s\n' "$output" >&2
    submission_id=$(printf '%s\n' "$output" | awk '/id: / {print $2; exit}')
    if [ -n "$submission_id" ]; then
      if [ -n "${RNS_NOTARY_KEY:-}" ]; then
        xcrun notarytool log "$submission_id" \
          --key "$(rns_notary_keyfile)" \
          --key-id "$RNS_NOTARY_KEY_ID" \
          --issuer "$RNS_NOTARY_ISSUER_ID" >&2 || true
      else
        xcrun notarytool log "$submission_id" \
          --keychain-profile "${RNS_NOTARY_KEYCHAIN_PROFILE:-rnsim-notary}" \
          >&2 || true
      fi
    fi
    echo "Notarization failed for $input" >&2
    exit 1
  fi
}

output_dir=$(dirname "$archive")
archive_name=$(basename "$archive")

if [ "${RNS_SKIP_DMG:-0}" = 1 ]; then
  zip="$stage/$payload_name.zip"
  ditto -c -k --keepParent "$payload" "$zip"
  rns_notary_submit "$zip"
else
  dmg_name=${archive_name%.tar.gz}.dmg
  dmg="$output_dir/$dmg_name"
  rm -f "$dmg"
  hdiutil create \
    -volname "$payload_name" \
    -srcfolder "$stage/extract" \
    -ov \
    -format UDZO \
    "$stage/unsigned.dmg" >/dev/null
  mv "$stage/unsigned.dmg" "$dmg"
  codesign --force --sign "$identity" --timestamp "$dmg"
  codesign --verify --verbose=2 "$dmg"
  rns_notary_submit "$dmg"
  xcrun stapler staple "$dmg"
  xcrun stapler validate "$dmg"
fi

SOURCE_DATE_EPOCH=$(git -C "$project_root" show -s --format=%ct HEAD) \
  "$script_dir/create-reproducible-tar.sh" \
  "$stage/extract" "$payload_name" "$archive"
(cd "$output_dir" && shasum -a 256 "$archive_name" >"$archive_name.sha256")

echo "Signed and notarized $archive"
if [ "${RNS_SKIP_DMG:-0}" != 1 ]; then
  echo "Wrote stapled $output_dir/${archive_name%.tar.gz}.dmg"
fi
