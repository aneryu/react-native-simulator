#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
  echo "Usage: collect-licenses.sh DESTINATION" >&2
  exit 2
fi

project_root=$(CDPATH='' cd -- "$(dirname "$0")/../.." && pwd)
destination=$1
mkdir -p "$destination"

copy_license() {
  source_file=$1
  output_name=$2
  if [ ! -f "$source_file" ]; then
    echo "Required license is missing: $source_file" >&2
    exit 1
  fi
  cp "$source_file" "$destination/$output_name"
}

copy_license "$project_root/third_party/react-native/LICENSE" \
  react-native-MIT.txt
copy_license "$project_root/third_party/hermes/LICENSE" hermes-MIT.txt
copy_license "$project_root/third_party/fast_float/LICENSE-MIT" \
  fast_float-MIT.txt
copy_license "$project_root/third_party/glog/COPYING" \
  glog-BSD-3-Clause.txt
copy_license "$project_root/third_party/skia/LICENSE" \
  skia-BSD-3-Clause.txt
copy_license "$project_root/third_party/skia/third_party/externals/harfbuzz/COPYING" \
  harfbuzz-OLD-MIT.txt
copy_license "$project_root/third_party/skia/third_party/externals/icu/LICENSE" \
  icu-Unicode-3.0.txt
copy_license "$project_root/third_party/skia/third_party/externals/freetype/LICENSE.TXT" \
  freetype-FTL-or-GPL-2.0.txt
copy_license "$project_root/third_party/skia/third_party/externals/zlib/LICENSE" \
  zlib-Zlib.txt
copy_license "$project_root/third_party/skia/third_party/externals/libpng/LICENSE" \
  libpng-Libpng.txt
copy_license "$project_root/third_party/imgui/LICENSE.txt" imgui-MIT.txt
copy_license "$project_root/third_party/sdl/LICENSE.txt" sdl-Zlib.txt
copy_license "$project_root/third_party/hermes/external/boost/boost_1_86_0/LICENSE_1_0.txt" \
  boost-Boost-1.0.txt

for formula in folly fmt double-conversion; do
  formula_prefix=$(brew --prefix "$formula")
  case "$formula" in
    folly) license_path=LICENSE; output=folly-Apache-2.0.txt ;;
    fmt) license_path=LICENSE; output=fmt-MIT.txt ;;
    double-conversion)
      license_path=LICENSE; output=double-conversion-BSD-3-Clause.txt ;;
  esac
  copy_license "$formula_prefix/$license_path" "$output"
done

expected_count=16
actual_count=$(find "$destination" -type f | wc -l | tr -d ' ')
if [ "$actual_count" -ne "$expected_count" ]; then
  echo "License inventory contains $actual_count files; expected $expected_count" >&2
  exit 1
fi
