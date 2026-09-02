# Third-party notices

React Native Simulator Nightly is distributed under the MIT License. Its
macOS runtime and RN Tester support package also redistribute or statically
link the projects below. Exact source revisions are recorded in the release
manifest and SPDX SBOM shipped with each archive; complete license texts are
in the adjacent `licenses/` directory.

| Component | Use | License file in archive |
|---|---|---|
| React Native, including Yoga and RN Tester sources | Runtime contracts, Fabric/Yoga, demo bundle | `react-native-MIT.txt` |
| Hermes | JavaScript engine and bytecode | `hermes-MIT.txt` |
| fast_float | Numeric parsing | `fast_float-MIT.txt` |
| Skia | Rendering and text layout | `skia-BSD-3-Clause.txt` |
| HarfBuzz | Text shaping through Skia | `harfbuzz-OLD-MIT.txt` |
| ICU | Unicode services and filtered text data | `icu-Unicode-3.0.txt` |
| FreeType | Font rasterization through Skia | `freetype-FTL-or-GPL-2.0.txt` |
| zlib | Compression support through Skia | `zlib-Zlib.txt` |
| libpng | PNG export through Skia | `libpng-Libpng.txt` |
| Dear ImGui | Host chrome | `imgui-MIT.txt` |
| SDL | macOS window/input backend | `sdl-Zlib.txt` |
| Folly | Native utility library | `folly-Apache-2.0.txt` |
| fmt | Formatting dependency | `fmt-MIT.txt` |
| glog | Logging dependency | `glog-BSD-3-Clause.txt` |
| gflags | glog dependency | `gflags-BSD-3-Clause.txt` |
| double-conversion | Numeric conversion dependency | `double-conversion-BSD-3-Clause.txt` |
| Boost | Header-only and context dependencies | `boost-Boost-1.0.txt` |

This inventory describes the release build, not every package present on the
build machine. Apple system frameworks and the macOS C/C++ runtime are supplied
by the operating system and are not copied into the archive.
