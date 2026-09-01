#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "${script_dir}/.." && pwd)"
skia_dir="${project_dir}/third_party/skia"

if [[ ! -f "${skia_dir}/BUILD.gn" ]]; then
  echo "Skia submodule is missing. Run: git submodule update --init third_party/skia" >&2
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

if [[ ! -x "${skia_dir}/bin/gn" ]]; then
  if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo "Automatic GN bootstrap currently supports Apple Silicon only." >&2
    exit 1
  fi
  gn_archive="$(mktemp /tmp/react-native-simulator-gn.XXXXXX.zip)"
  curl -fL \
    "https://chrome-infra-packages.appspot.com/dl/gn/gn/mac-arm64/+/git_revision:b2afae122eeb6ce09c52d63f67dc53fc517dbdc8" \
    -o "${gn_archive}"
  (cd "${skia_dir}" && unzip -o "${gn_archive}" gn -d bin && chmod 755 bin/gn)
fi

# Images are decoded through macOS ImageIO. Keep only PNG encoding for direct
# screenshots; compiling Skia's JPEG/PNG/WebP decoders adds redundant codec
# registries and can retain their third-party implementations in the engine.
gn_args='is_debug=false is_official_build=false target_cpu="arm64" skia_enable_ganesh=false skia_enable_graphite=false skia_enable_pdf=false skia_enable_skparagraph=true skia_enable_skshaper=true skia_enable_skshaper_tests=false skia_enable_skunicode=true skia_use_gl=false skia_use_metal=false skia_use_vulkan=false skia_use_dawn=false skia_use_harfbuzz=true skia_use_icu=true skia_use_freetype=true skia_use_system_freetype2=false skia_use_libjpeg_turbo_decode=false skia_use_libjpeg_turbo_encode=false skia_use_libpng_decode=false skia_use_libpng_encode=true skia_use_libwebp_decode=false skia_use_libwebp_encode=false'

(cd "${skia_dir}" && \
  bin/gn gen out/rnsim --args="${gn_args}" && \
  ninja -C out/rnsim skia skparagraph fontmgr_mac_ct fontmgr_custom_directory)

# SkParagraph needs Unicode character properties, bidi and line/word breaking;
# those semantics cannot be replaced by ASCII tables or CoreText without making
# Yoga measurement and paint disagree. Skia's macOS default embeds ICU's full
# desktop data set, including date, currency, time-zone and transliteration data
# that rnsim never calls. Repack the generated archive with ICU's pinned
# Flutter-desktop filter instead. It retains the multilingual text/break data
# used by SkParagraph while reducing the embedded read-only payload by ~9 MiB.
# This is a transitional reuse of an upstream ICU filter, not a Flutter runtime
# dependency. Skia's Android data is also linkable, but is ~9.3 MB and retains
# locale, currency, time-zone, unit, conversion and transliteration resources;
# it does not make a native macOS process more Android-compatible. The intended
# successor is an rnsim-owned text-only filter certified by multilingual tests.
# Keep the upstream archive untouched so a later Ninja invocation cannot make a
# partially rewritten Skia output look valid to other consumers.
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
if ! /usr/bin/ar -t "${upstream_icu_archive}" | \
    grep -qx 'libicu.icudtl_dat.o'; then
  echo "Skia ICU archive no longer contains libicu.icudtl_dat.o" >&2
  exit 1
fi

mkdir -p "${icu_work_dir}"
python3 "${icu_script}" "${icu_data}" "${icu_assembly}" --mac
xcrun clang -arch arm64 -c "${icu_assembly}" -o "${icu_object}"
cp "${upstream_icu_archive}" "${icu_archive}"
/usr/bin/ar -d "${icu_archive}" libicu.icudtl_dat.o
/usr/bin/ar -q "${icu_archive}" "${icu_object}"
/usr/bin/ranlib "${icu_archive}"

echo "Skia renderer dependencies are ready in ${skia_dir}/out/rnsim"
