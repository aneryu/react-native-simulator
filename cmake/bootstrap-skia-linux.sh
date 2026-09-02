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

# Do not run Skia's full tools/git-sync-deps. That also fetches Dawn, Vulkan,
# Emscripten, and other GPU toolchains this CPU renderer never links. Ubuntu CI
# cannot hold that tree. Checkout only the DEPS pins Skia actually compiles.
python3 - "${skia_dir}" <<'PY'
import os
import subprocess
import sys

skia_dir = sys.argv[1]
needed = (
    "third_party/externals/freetype",
    "third_party/externals/harfbuzz",
    "third_party/externals/icu",
    "third_party/externals/libjpeg-turbo",
    "third_party/externals/libpng",
    "third_party/externals/libwebp",
)
ns = {}
with open(os.path.join(skia_dir, "DEPS"), encoding="utf-8") as handle:
    exec("def Var(x): return vars[x]\n" + handle.read(), ns)
deps = ns["deps"]
for path in needed:
    spec = deps[path]
    repo, commit = spec.split("@", 1)
    dest = os.path.join(skia_dir, path)
    if not os.path.isdir(os.path.join(dest, ".git")):
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        subprocess.check_call(
            ["git", "clone", "--quiet", "--depth=1", "--no-checkout", repo, dest])
    if subprocess.call(
            ["git", "rev-parse", "--verify", "--quiet", commit],
            cwd=dest,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL) != 0:
        subprocess.check_call(
            ["git", "fetch", "--quiet", "--depth=1", repo, commit], cwd=dest)
    subprocess.check_call(["git", "checkout", "--quiet", commit], cwd=dest)
    print(f"synced {path} @ {commit[:12]}")
PY

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
