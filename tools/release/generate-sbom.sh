#!/bin/sh

set -eu

if [ "$#" -ne 2 ]; then
  echo "Usage: generate-sbom.sh OUTPUT runtime|demo" >&2
  exit 2
fi

output=$1
package_kind=$2
case "$package_kind" in runtime|demo) ;; *) exit 2 ;; esac
project_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
channel=$(sed -n \
  's/^set(RNS_RELEASE_CHANNEL "\([^"]*\)").*/\1/p' \
  "$project_root/CMakeLists.txt")
commit=$(git -C "$project_root" rev-parse HEAD)
rn_commit=$(git -C "$project_root/third_party/react-native" rev-parse HEAD)
hermes_commit=$(git -C "$project_root/third_party/hermes" rev-parse HEAD)
skia_commit=$(git -C "$project_root/third_party/skia" rev-parse HEAD)
fast_float_commit=$(git -C "$project_root/third_party/fast_float" rev-parse HEAD)
imgui_commit=$(git -C "$project_root/third_party/imgui" rev-parse HEAD)
sdl_commit=$(git -C "$project_root/third_party/sdl" rev-parse HEAD)
source_date_epoch=${SOURCE_DATE_EPOCH:-$(git -C "$project_root" show -s --format=%ct HEAD)}
created=$(date -u -r "$source_date_epoch" '+%Y-%m-%dT%H:%M:%SZ')

formula_version() {
  brew list --versions "$1" | awk '{print $2}'
}

folly_version=$(formula_version folly)
fmt_version=$(formula_version fmt)
glog_version=$(formula_version glog)
gflags_version=$(formula_version gflags)
double_conversion_version=$(formula_version double-conversion)
boost_version=$(formula_version boost)

{
  printf '{\n'
  printf '  "spdxVersion": "SPDX-2.3",\n'
  printf '  "dataLicense": "CC0-1.0",\n'
  printf '  "SPDXID": "SPDXRef-DOCUMENT",\n'
  printf '  "name": "react-native-simulator-%s-%s",\n' "$package_kind" "$channel"
  printf '  "documentNamespace": "https://react-native-simulator.invalid/spdx/%s/%s/%s",\n' "$channel" "$commit" "$package_kind"
  printf '  "creationInfo": {"created": "%s", "creators": ["Tool: tools/release/generate-sbom.sh"]},\n' "$created"
  printf '  "packages": [\n'
  printf '    {"SPDXID":"SPDXRef-rnsim","name":"react-native-simulator-%s","versionInfo":"%s","downloadLocation":"NOASSERTION","licenseConcluded":"MIT","licenseDeclared":"MIT","externalRefs":[{"referenceCategory":"PACKAGE-MANAGER","referenceType":"purl","referenceLocator":"pkg:generic/react-native-simulator@%s?vcs_url=git%%2B%s"}]},\n' "$package_kind" "$channel" "$channel" "$commit"
  printf '    {"SPDXID":"SPDXRef-react-native","name":"react-native","versionInfo":"0.87.0","downloadLocation":"NOASSERTION","licenseConcluded":"MIT","licenseDeclared":"MIT","sourceInfo":"git %s"},\n' "$rn_commit"
  printf '    {"SPDXID":"SPDXRef-hermes","name":"hermes","versionInfo":"260318099.0.1","downloadLocation":"NOASSERTION","licenseConcluded":"MIT","licenseDeclared":"MIT","sourceInfo":"git %s"},\n' "$hermes_commit"
  printf '    {"SPDXID":"SPDXRef-skia","name":"skia","versionInfo":"%s","downloadLocation":"NOASSERTION","licenseConcluded":"BSD-3-Clause","licenseDeclared":"BSD-3-Clause"},\n' "$skia_commit"
  printf '    {"SPDXID":"SPDXRef-fast-float","name":"fast_float","versionInfo":"%s","downloadLocation":"NOASSERTION","licenseConcluded":"MIT","licenseDeclared":"MIT"},\n' "$fast_float_commit"
  printf '    {"SPDXID":"SPDXRef-imgui","name":"dear-imgui","versionInfo":"%s","downloadLocation":"NOASSERTION","licenseConcluded":"MIT","licenseDeclared":"MIT"},\n' "$imgui_commit"
  printf '    {"SPDXID":"SPDXRef-sdl","name":"SDL","versionInfo":"%s","downloadLocation":"NOASSERTION","licenseConcluded":"Zlib","licenseDeclared":"Zlib"},\n' "$sdl_commit"
  printf '    {"SPDXID":"SPDXRef-folly","name":"folly","versionInfo":"%s","downloadLocation":"NOASSERTION","licenseConcluded":"Apache-2.0","licenseDeclared":"Apache-2.0"},\n' "$folly_version"
  printf '    {"SPDXID":"SPDXRef-fmt","name":"fmt","versionInfo":"%s","downloadLocation":"NOASSERTION","licenseConcluded":"MIT","licenseDeclared":"MIT"},\n' "$fmt_version"
  printf '    {"SPDXID":"SPDXRef-glog","name":"glog","versionInfo":"%s","downloadLocation":"NOASSERTION","licenseConcluded":"BSD-3-Clause","licenseDeclared":"BSD-3-Clause"},\n' "$glog_version"
  printf '    {"SPDXID":"SPDXRef-gflags","name":"gflags","versionInfo":"%s","downloadLocation":"NOASSERTION","licenseConcluded":"BSD-3-Clause","licenseDeclared":"BSD-3-Clause"},\n' "$gflags_version"
  printf '    {"SPDXID":"SPDXRef-double-conversion","name":"double-conversion","versionInfo":"%s","downloadLocation":"NOASSERTION","licenseConcluded":"BSD-3-Clause","licenseDeclared":"BSD-3-Clause"},\n' "$double_conversion_version"
  printf '    {"SPDXID":"SPDXRef-boost-system","name":"boost","versionInfo":"%s","downloadLocation":"NOASSERTION","licenseConcluded":"BSL-1.0","licenseDeclared":"BSL-1.0"},\n' "$boost_version"
  printf '    {"SPDXID":"SPDXRef-boost-context","name":"boost-context-vendored","versionInfo":"1.86.0","downloadLocation":"NOASSERTION","licenseConcluded":"BSL-1.0","licenseDeclared":"BSL-1.0"}\n'
  printf '  ]\n'
  printf '}\n'
} >"$output"
