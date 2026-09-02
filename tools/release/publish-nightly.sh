#!/bin/sh

set -eu

project_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
dist_dir=${1:-"$project_root/dist"}
channel=$(sed -n 's/^set(RNS_RELEASE_CHANNEL "\([^"]*\)").*/\1/p' \
  "$project_root/CMakeLists.txt")
[ "$channel" = nightly ] || { echo "Only Nightly can be published." >&2; exit 1; }
[ "$(git -C "$project_root" branch --show-current)" = main ] || {
  echo "Nightly must be published from main." >&2; exit 1;
}
[ -z "$(git -C "$project_root" status --porcelain)" ] || {
  echo "Nightly publishing requires a clean working tree." >&2; exit 1;
}
if git -C "$project_root" submodule status --recursive | grep -Eq '^[+-U]'; then
  echo "Nightly publishing requires pinned, clean submodules." >&2
  exit 1
fi

dmg="$dist_dir/rnsim-nightly-macos-arm64.dmg"
checksum="$dmg.sha256"
for asset in "$dmg" "$checksum"; do
  [ -f "$asset" ] || { echo "Missing Nightly asset: $asset" >&2; exit 1; }
done
"$project_root/tools/release/verify-release.sh" "$dist_dir"

commit=$(git -C "$project_root" rev-parse HEAD)
git -C "$project_root" push origin HEAD:main
git -C "$project_root" tag -f nightly "$commit"
git -C "$project_root" push --force origin refs/tags/nightly

repo=aneryu/react-native-simulator
notes="$project_root/docs/releases/nightly.md"
if gh release view nightly --repo "$repo" >/dev/null 2>&1; then
  gh release edit nightly --repo "$repo" --latest --prerelease=false \
    --title "React Native Simulator Nightly" --notes-file "$notes"
  gh release upload nightly "$dmg" "$checksum" --repo "$repo" --clobber
else
  gh release create nightly "$dmg" "$checksum" --repo "$repo" \
    --verify-tag --latest --title "React Native Simulator Nightly" \
    --notes-file "$notes"
fi

for old_asset in $(gh release view nightly --repo "$repo" \
    --json assets --jq '.assets[].name'); do
  case "$old_asset" in
    rnsim-nightly-macos-arm64.dmg|rnsim-nightly-macos-arm64.dmg.sha256) ;;
    *) gh release delete-asset nightly "$old_asset" --repo "$repo" --yes ;;
  esac
done

echo "Published rolling Nightly at commit $commit"
