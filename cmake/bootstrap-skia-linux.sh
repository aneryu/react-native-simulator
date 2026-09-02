#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "${script_dir}/.." && pwd)"
skia_dir="${project_dir}/third_party/skia"

if [[ ! -f "${skia_dir}/BUILD.gn" ]]; then
  echo "Skia submodule is missing. Run: git submodule update --init third_party/skia" >&2
  exit 1
fi

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "Use cmake/bootstrap-skia-macos.sh on macOS." >&2
  exit 1
fi

skia_dependencies=(harfbuzz icu freetype)
missing_skia_dependency=false
for dependency in "${skia_dependencies[@]}"; do
  if [[ ! -d "${skia_dir}/third_party/externals/${dependency}" ]]; then
    missing_skia_dependency=true
  fi
done
if [[ "${missing_skia_dependency}" == true ]]; then
  if ! (cd "${skia_dir}" && python3 tools/git-sync-deps); then
    for dependency in "${skia_dependencies[@]}"; do
      if [[ ! -d "${skia_dir}/third_party/externals/${dependency}" ]]; then
        echo "Skia dependency sync failed before ${dependency} was available." >&2
        exit 1
      fi
    done
    echo "Required dependency sources are present; continuing after sync failure." >&2
  fi
fi

host_cpu="$(uname -m)"
case "${host_cpu}" in
  x86_64) gn_cpu="amd64"; skia_cpu="x64" ;;
  aarch64|arm64) gn_cpu="arm64"; skia_cpu="arm64" ;;
  *)
    echo "Unsupported Linux architecture: ${host_cpu}" >&2
    exit 1
    ;;
esac

if [[ ! -x "${skia_dir}/bin/gn" ]]; then
  gn_archive="$(mktemp /tmp/react-native-simulator-gn.XXXXXX.zip)"
  curl -fL \
    "https://chrome-infra-packages.appspot.com/dl/gn/gn/linux-${gn_cpu}/+/git_revision:b2afae122eeb6ce09c52d63f67dc53fc517dbdc8" \
    -o "${gn_archive}"
  (cd "${skia_dir}" && unzip -o "${gn_archive}" gn -d bin && chmod 755 bin/gn)
fi

# Linux has no ImageIO. Enable Skia PNG/JPEG/WebP decode so Metro assets paint.
gn_args="is_debug=false is_official_build=false target_cpu=\"${skia_cpu}\" skia_enable_ganesh=false skia_enable_graphite=false skia_enable_pdf=false skia_enable_skparagraph=true skia_enable_skshaper=true skia_enable_skshaper_tests=false skia_enable_skunicode=true skia_use_gl=false skia_use_metal=false skia_use_vulkan=false skia_use_dawn=false skia_use_harfbuzz=true skia_use_icu=true skia_use_freetype=true skia_use_system_freetype2=false skia_use_fontconfig=true skia_use_libjpeg_turbo_decode=true skia_use_libjpeg_turbo_encode=false skia_use_libpng_decode=true skia_use_libpng_encode=true skia_use_libwebp_decode=true skia_use_libwebp_encode=false"

(cd "${skia_dir}" && \
  bin/gn gen out/rnsim --args="${gn_args}" && \
  ninja -C out/rnsim skia skparagraph fontmgr_fontconfig fontmgr_custom_directory)

icu_data="${skia_dir}/third_party/externals/icu/flutter_desktop/icudtl.dat"
icu_script="${skia_dir}/third_party/externals/icu/scripts/make_data_assembly.py"
icu_work_dir="${skia_dir}/out/rnsim/rnsim-icu-data"
icu_assembly="${icu_work_dir}/icudtl_dat.S"
icu_object="${icu_work_dir}/libicu.icudtl_dat.o"
icu_archive="${skia_dir}/out/rnsim/libskunicode_icu_rnsim.a"
upstream_icu_archive="${skia_dir}/out/rnsim/libskunicode_icu.a"

for required_icu_path in "${icu_data}" "${icu_script}" "${upstream_icu_archive}"; do
  if [[ ! -f "${required_icu_path}" ]]; then
    echo "Filtered ICU input is missing: ${required_icu_path}" >&2
    exit 1
  fi
done
if ! ar -t "${upstream_icu_archive}" | grep -qx 'libicu.icudtl_dat.o'; then
  echo "Skia ICU archive no longer contains libicu.icudtl_dat.o" >&2
  exit 1
fi

mkdir -p "${icu_work_dir}"
python3 "${icu_script}" "${icu_data}" "${icu_assembly}"
cc -c "${icu_assembly}" -o "${icu_object}"
cp "${upstream_icu_archive}" "${icu_archive}"
ar -d "${icu_archive}" libicu.icudtl_dat.o
ar -q "${icu_archive}" "${icu_object}"
ranlib "${icu_archive}"

echo "Skia renderer dependencies are ready in ${skia_dir}/out/rnsim"
