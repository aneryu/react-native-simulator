#!/bin/sh

# Shared Mach-O signing for release staging. Source this file from another
# tools/release script, or invoke it directly with one or more file paths.
#
# RNS_CODESIGN_IDENTITY selects the signer. Empty or "-" remains available for
# local build-tree sealing, but official Nightly packaging requires a Developer
# ID Application identity and Hardened Runtime.

set -eu

rns_codesign_identifier_prefix=${RNS_CODESIGN_IDENTIFIER_PREFIX:-dev.reactnativesimulator}

rns_codesign_lib_dir=${RNS_CODESIGN_LIB_DIR:-$(CDPATH='' cd -- "$(dirname "$0")" && pwd)}
rns_codesign_entitlements="$rns_codesign_lib_dir/rnsim.entitlements"

rns_codesign_identity() {
  identity=${RNS_CODESIGN_IDENTITY:-}
  if [ -z "$identity" ]; then
    printf '%s\n' "-"
    return
  fi
  printf '%s\n' "$identity"
}

rns_codesign_is_adhoc() {
  identity=$(rns_codesign_identity)
  [ "$identity" = "-" ]
}

rns_codesign_identifier_for() {
  binary=$1
  base=$(basename "$binary")
  case "$base" in
    rnsim)
      printf '%s\n' "$rns_codesign_identifier_prefix.rnsim"
      ;;
    libreact-native-simulator-engine*)
      printf '%s\n' "$rns_codesign_identifier_prefix.engine"
      ;;
    rns-addon-*.dylib)
      name=${base#rns-addon-}
      name=${name%.dylib}
      printf '%s\n' "$rns_codesign_identifier_prefix.addon.$name"
      ;;
    lib*.dylib)
      lib=${base#lib}
      lib=${lib%.dylib}
      printf '%s\n' "$rns_codesign_identifier_prefix.lib.$lib"
      ;;
    *.dylib)
      printf '%s\n' "$rns_codesign_identifier_prefix.lib.${base%.dylib}"
      ;;
    *)
      printf '%s\n' "$rns_codesign_identifier_prefix.$(printf '%s' "$base" | tr -c 'A-Za-z0-9._-' '_')"
      ;;
  esac
}

rns_codesign_needs_entitlements() {
  base=$(basename "$1")
  [ "$base" = "rnsim" ]
}

rns_codesign_file() {
  binary=$1
  identity=$(rns_codesign_identity)
  identifier=$(rns_codesign_identifier_for "$binary")
  if [ ! -f "$rns_codesign_entitlements" ]; then
    echo "Missing entitlements file: $rns_codesign_entitlements" >&2
    exit 1
  fi
  xattr -c "$binary" 2>/dev/null || true
  if rns_codesign_is_adhoc; then
    # Apple Silicon requires a valid signature even without a Developer ID.
    # Keep ad-hoc signing timestamp-free so release tarballs stay reproducible.
    codesign --force --sign - --identifier "$identifier" "$binary"
    return
  fi
  case "$identity" in
    *Apple\ Development*|*Apple\ Distribution*)
      echo "RNS_CODESIGN_IDENTITY must be a Developer ID Application identity." >&2
      echo "Apple Development and Apple Distribution certificates cannot be notarized." >&2
      echo "Got: $identity" >&2
      exit 1
      ;;
  esac
  if rns_codesign_needs_entitlements "$binary"; then
    codesign --force --sign "$identity" \
      --timestamp --options runtime \
      --identifier "$identifier" \
      --entitlements "$rns_codesign_entitlements" \
      "$binary"
  else
    codesign --force --sign "$identity" \
      --timestamp --options runtime \
      --identifier "$identifier" \
      "$binary"
  fi
}

rns_codesign_verify_file() {
  binary=$1
  if ! codesign --verify --strict --verbose=2 "$binary"; then
    echo "invalid code signature in $binary" >&2
    return 1
  fi
  signature_info=$(codesign -dv --verbose=4 "$binary" 2>&1)
  if rns_codesign_is_adhoc; then
    printf '%s\n' "$signature_info" | grep -Fq 'Signature=adhoc' || {
      echo "expected an ad-hoc signature in $binary" >&2
      printf '%s\n' "$signature_info" >&2
      return 1
    }
    return 0
  fi
  printf '%s\n' "$signature_info" | grep -Fq 'Authority=Developer ID Application:' || {
    echo "expected a Developer ID Application signature in $binary" >&2
    printf '%s\n' "$signature_info" >&2
    return 1
  }
  printf '%s\n' "$signature_info" | grep -Eq 'flags=.*(runtime|0x10000)' || {
    echo "expected Hardened Runtime in $binary" >&2
    printf '%s\n' "$signature_info" >&2
    return 1
  }
  printf '%s\n' "$signature_info" | grep -Eq '^TeamIdentifier=[A-Z0-9]{10}$' || {
    echo "expected a TeamIdentifier in $binary" >&2
    printf '%s\n' "$signature_info" >&2
    return 1
  }
  if rns_codesign_needs_entitlements "$binary"; then
    entitlements_dump=$(mktemp)
    codesign -d --entitlements "$entitlements_dump" "$binary" >/dev/null 2>&1 || true
    if ! grep -Fq 'com.apple.security.cs.disable-library-validation' \
        "$entitlements_dump"; then
      rm -f "$entitlements_dump"
      echo "rnsim is missing disable-library-validation: $binary" >&2
      return 1
    fi
    rm -f "$entitlements_dump"
  fi
}

rns_each_macho() {
  root=$1
  find "$root" -type f -print | while IFS= read -r candidate; do
    file -b "$candidate" | grep -q '^Mach-O' || continue
    printf '%s\n' "$candidate"
  done
}

rns_codesign_tree() {
  root=$1
  # Notarization rejects resource forks and Finder info on any payload file.
  xattr -cr "$root"
  all_file=$(mktemp)
  libs_file=$(mktemp)
  bins_file=$(mktemp)
  rns_each_macho "$root" >"$all_file"
  while IFS= read -r binary; do
    [ -n "$binary" ] || continue
    if file -b "$binary" | grep -q '^Mach-O 64-bit executable'; then
      printf '%s\n' "$binary" >>"$bins_file"
    else
      printf '%s\n' "$binary" >>"$libs_file"
    fi
  done <"$all_file"
  while IFS= read -r binary; do
    [ -n "$binary" ] || continue
    rns_codesign_file "$binary"
  done <"$libs_file"
  while IFS= read -r binary; do
    [ -n "$binary" ] || continue
    rns_codesign_file "$binary"
  done <"$bins_file"
  rm -f "$all_file" "$libs_file" "$bins_file"
}

rns_codesign_verify_tree() {
  root=$1
  all_file=$(mktemp)
  verification_failed=0
  rns_each_macho "$root" >"$all_file"
  while IFS= read -r binary; do
    [ -n "$binary" ] || continue
    if ! rns_codesign_verify_file "$binary"; then
      verification_failed=1
      break
    fi
  done <"$all_file"
  rm -f "$all_file"
  [ "$verification_failed" -eq 0 ]
}
