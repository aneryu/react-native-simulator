#!/bin/sh

set -eu

package_root=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
release_version=$(sed -n '1p' "$package_root/VERSION")
prefix=${HOME:?HOME is required}/.local
assume_yes=0
operation=install
operation_version=
reinstall=0

usage() {
  echo "Usage: ./install.sh [--prefix DIR] [--yes] [--reinstall]"
  echo "       ./install.sh --activate VERSION [--prefix DIR] [--yes]"
  echo "       ./install.sh --uninstall VERSION [--prefix DIR] [--yes]"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --prefix)
      shift
      if [ "$#" -eq 0 ]; then
        echo "--prefix requires a directory" >&2
        exit 1
      fi
      prefix=$1
      ;;
    --yes)
      assume_yes=1
      ;;
    --reinstall)
      reinstall=1
      ;;
    --activate|--uninstall)
      operation=${1#--}
      shift
      if [ "$#" -eq 0 ]; then
        echo "--$operation requires a version" >&2
        exit 1
      fi
      operation_version=$1
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
  shift
done

case "$release_version" in
  ''|*[!0-9.]*)
    echo "Invalid release version in $package_root/VERSION" >&2
    exit 1
    ;;
esac
case "${operation_version:-$release_version}" in
  ''|*[!0-9.]*)
    echo "Invalid version: ${operation_version:-$release_version}" >&2
    exit 1
    ;;
esac
case "$prefix" in
  /*) ;;
  *) prefix=$PWD/$prefix ;;
esac

install_base="$prefix/lib/react-native-simulator"
install_root="$install_base/$release_version"
current_link="$install_base/current"
link="$prefix/bin/rnsim"

confirm() {
  prompt=$1
  if [ "$assume_yes" -eq 1 ]; then
    return
  fi
  if [ ! -t 0 ]; then
    echo "Interactive confirmation is required; use --yes after verifying the checksum." >&2
    exit 1
  fi
  printf '%s [y/N] ' "$prompt"
  read -r answer
  case "$answer" in
    y|Y|yes|YES) ;;
    *) echo "Operation cancelled"; exit 1 ;;
  esac
}

is_managed_path_link() {
  [ -L "$link" ] || return 1
  link_target=$(readlink "$link")
  case "$link_target" in
    ../lib/react-native-simulator/current/bin/rnsim|\
    "$install_base"/*/bin/rnsim|"$current_link"/bin/rnsim) return 0 ;;
    *) return 1 ;;
  esac
}

activate() {
  version=$1
  target="$install_base/$version"
  if [ ! -x "$target/bin/rnsim" ]; then
    echo "Installed version does not exist: $target" >&2
    exit 1
  fi
  if { [ -e "$link" ] || [ -L "$link" ]; } && ! is_managed_path_link; then
    echo "Refusing to replace an unmanaged PATH entry: $link" >&2
    exit 1
  fi
  mkdir -p "$install_base" "$prefix/bin"
  next_current="$install_base/.current.$$"
  ln -s "$version" "$next_current"
  mv -fh "$next_current" "$current_link"
  rm -f "$link"
  ln -s ../lib/react-native-simulator/current/bin/rnsim "$link"
}

if [ "$operation" = activate ]; then
  confirm "Activate React Native Simulator $operation_version?"
  activate "$operation_version"
  echo "Activated React Native Simulator $operation_version"
  exit 0
fi

if [ "$operation" = uninstall ]; then
  uninstall_root="$install_base/$operation_version"
  if [ ! -d "$uninstall_root" ]; then
    echo "Installed version does not exist: $uninstall_root" >&2
    exit 1
  fi
  confirm "Uninstall React Native Simulator $operation_version from $uninstall_root?"
  current_version=
  if [ -L "$current_link" ]; then
    current_version=$(readlink "$current_link")
  fi
  rm -rf "$uninstall_root"
  if [ "$current_version" = "$operation_version" ]; then
    rm -f "$current_link"
    if is_managed_path_link; then
      rm -f "$link"
    fi
    echo "The active version was removed. Use --activate VERSION to select another installation."
  fi
  echo "Uninstalled React Native Simulator $operation_version"
  exit 0
fi

runtime="$package_root/bin/rnsim"
if [ ! -x "$runtime" ] || \
    ! file "$runtime" | grep -q 'Mach-O 64-bit executable arm64'; then
  echo "Expected an arm64 rnsim at $runtime" >&2
  exit 1
fi

if [ -e "$install_root" ] || [ -L "$install_root" ]; then
  if [ "$reinstall" -ne 1 ]; then
    echo "Install target already exists: $install_root" >&2
    echo "Use --reinstall to replace this exact version." >&2
    exit 1
  fi
  confirm "Replace React Native Simulator $release_version at $install_root?"
  rm -rf "$install_root"
else
  confirm "Install trusted React Native Simulator $release_version to $install_root?"
fi

# The caller verifies the published SHA-256 before this point. Gatekeeper
# accepts Developer ID + notarized binaries; ad-hoc (or otherwise rejected)
# signatures still need an explicit quarantine removal after that checksum.
if ! spctl --assess --type execute "$runtime" >/dev/null 2>&1; then
  xattr -dr com.apple.quarantine "$package_root"
fi

mkdir -p "$install_base" "$prefix/bin"
stage_root=$(mktemp -d "$install_base/.install-${release_version}.XXXXXX")
cleanup() {
  if [ -n "${stage_root:-}" ] && [ -d "$stage_root" ]; then
    rm -rf "$stage_root"
  fi
}
trap cleanup 0 1 2 15
mkdir "$stage_root/rnsim"
cp -R "$package_root/." "$stage_root/rnsim/"
mv "$stage_root/rnsim" "$install_root"
rmdir "$stage_root"
stage_root=
activate "$release_version"
trap - 0 1 2 15

echo "Installed React Native Simulator $release_version in $install_root"
echo "Linked $link"
case ":${PATH:-}:" in
  *":$prefix/bin:"*) ;;
  *) echo "Add this to your shell environment: export PATH=\"$prefix/bin:\$PATH\"" ;;
esac
