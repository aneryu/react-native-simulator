#!/bin/sh

set -eu

project_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
dist_dir=${1:-"$project_root/dist"}
channel=$(sed -n \
  's/^set(RNS_RELEASE_CHANNEL "\([^"]*\)").*/\1/p' \
  "$project_root/CMakeLists.txt")

if [ "$channel" != nightly ]; then
  echo "Rolling publisher requires the nightly release channel." >&2
  exit 1
fi
if [ "$(git -C "$project_root" branch --show-current)" != main ]; then
  echo "Nightly must be published from main." >&2
  exit 1
fi
if [ -n "$(git -C "$project_root" status --porcelain)" ]; then
  echo "Nightly publishing requires a clean working tree." >&2
  exit 1
fi
if git -C "$project_root" submodule status --recursive | grep -Eq '^[+-U]'; then
  echo "Nightly publishing requires pinned, clean submodules." >&2
  exit 1
fi

runtime_archive="$dist_dir/rnsim-nightly-macos-arm64.tar.gz"
demo_archive="$dist_dir/rnsim-rntester-demo-nightly-macos-arm64.tar.gz"
set -- \
  "$runtime_archive" \
  "$runtime_archive.sha256" \
  "$demo_archive" \
  "$demo_archive.sha256"
for asset in "$@"; do
  if [ ! -f "$asset" ]; then
    echo "Missing Nightly asset: $asset" >&2
    exit 1
  fi
done

"$project_root/tools/release/verify-release.sh" "$dist_dir"
commit=$(git -C "$project_root" rev-parse HEAD)
manifest=$(tar -xOf "$runtime_archive" rnsim/release-manifest.json)
printf '%s\n' "$manifest" | grep -Fq "\"gitCommit\": \"$commit\"" || {
  echo "Runtime manifest does not match $commit." >&2
  exit 1
}
printf '%s\n' "$manifest" | grep -Fq '"dirty": false' || {
  echo "Runtime manifest is not from a clean checkout." >&2
  exit 1
}

git -C "$project_root" push origin HEAD:main
git -C "$project_root" tag -f nightly "$commit"
git -C "$project_root" push --force origin refs/tags/nightly

notes="$project_root/docs/releases/nightly.md"
if gh release view nightly --repo aneryu/react-native-simulator >/dev/null 2>&1; then
  gh release edit nightly \
    --repo aneryu/react-native-simulator \
    --prerelease \
    --title "React Native Simulator Nightly" \
    --notes-file "$notes"
  gh release upload nightly "$@" \
    --repo aneryu/react-native-simulator \
    --clobber
else
  gh release create nightly "$@" \
    --repo aneryu/react-native-simulator \
    --verify-tag \
    --prerelease \
    --title "React Native Simulator Nightly" \
    --notes-file "$notes"
fi

echo "Published rolling Nightly at commit $commit"
