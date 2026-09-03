#!/usr/bin/env bash
# Cloud Agent bootstrap for the Linux source-build host.
#
# Mirrors .github/workflows/linux.yml (core + gui jobs) so a Cloud Agent gets a
# ready-to-use React Native Simulator build. Idempotent: safe to re-run.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

read_pin() {
  sed -n "s/^set(${1} \"\([^\"]*\)\").*/\1/p" cmake/DependencyVersions.cmake
}

echo "==> Installing native and window-system dependencies"
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  ninja-build g++ libstdc++-14-dev libboost-all-dev libfmt-dev \
  libdouble-conversion-dev libssl-dev libcurl4-openssl-dev libpng-dev \
  zlib1g-dev uuid-dev libevent-dev libicu-dev pkg-config python3 \
  xxd unzip curl libfontconfig-dev fonts-dejavu-core \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
  libxtst-dev libxss-dev libxkbcommon-dev libwayland-dev libdbus-1-dev \
  libudev-dev libdrm-dev libgbm-dev libgl1-mesa-dev libegl1-mesa-dev \
  libgles2-mesa-dev libvulkan-dev libasound2-dev libpulse-dev xvfb

# This base image points the c++/cc alternatives at a clang that cannot find
# libstdc++. The engine is validated with GCC on Linux, so make GCC the default
# compiler for the documented `cmake --preset release` flow.
if update-alternatives --list c++ 2>/dev/null | grep -q '/usr/bin/g++'; then
  sudo update-alternatives --set c++ /usr/bin/g++ || true
fi
if update-alternatives --list cc 2>/dev/null | grep -q '/usr/bin/gcc'; then
  sudo update-alternatives --set cc /usr/bin/gcc || true
fi
export CC=gcc CXX=g++

echo "==> Initializing pinned submodules (engine + GUI)"
git submodule sync -- \
  third_party/hermes third_party/react-native third_party/fast_float \
  third_party/glog third_party/imgui third_party/sdl third_party/skia
git submodule update --init --depth 1 -- \
  third_party/hermes third_party/react-native third_party/fast_float \
  third_party/glog third_party/imgui third_party/sdl third_party/skia

hermes_tag="$(read_pin RNS_EXPECTED_HERMES_TAG)"
git -C third_party/hermes fetch --depth 1 origin tag "${hermes_tag}"
git -C third_party/hermes checkout --detach "${hermes_tag}"
fast_float_tag="$(read_pin RNS_EXPECTED_FAST_FLOAT_TAG)"
git -C third_party/fast_float fetch --depth 1 origin tag "${fast_float_tag}"
git -C third_party/fast_float checkout --detach "${fast_float_tag}"
glog_commit="$(read_pin RNS_EXPECTED_GLOG_COMMIT)"
git -C third_party/glog fetch --depth 1 origin "${glog_commit}"
git -C third_party/glog checkout --detach "${glog_commit}"
imgui_tag="$(read_pin RNS_EXPECTED_IMGUI_TAG)"
git -C third_party/imgui fetch --depth 1 origin tag "${imgui_tag}"
sdl_tag="$(read_pin RNS_EXPECTED_SDL_TAG)"
git -C third_party/sdl fetch --depth 1 origin tag "${sdl_tag}"

echo "==> Bootstrapping the Skia renderer (cached after first run)"
cmake/bootstrap-skia-linux.sh

echo "==> Configuring and building the interactive release (Skia + ImGui)"
cmake --preset release
cmake --build --preset release

./build/release/runtime/rnsim --version
echo "==> React Native Simulator is ready (build/release/runtime/rnsim)"
