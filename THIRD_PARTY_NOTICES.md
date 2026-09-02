# Third-party notices

React Native Simulator Nightly is distributed under the MIT License. Its
macOS executable statically links the projects below. The exact simulator,
submodule, and build-dependency revisions are identified by the clean Git commit
reported by `rnsim --version --json`. This notice is linked from the rolling
Nightly release page; complete license texts remain at the listed source paths
in the repository and dependency sources for that commit.

| Component | Use | License source |
|---|---|---|
| React Native, including Yoga | Runtime contracts and Fabric/Yoga | `third_party/react-native/LICENSE` |
| Hermes | JavaScript engine and bytecode | `third_party/hermes/LICENSE` |
| fast_float | Numeric parsing | `third_party/fast_float/LICENSE-MIT` |
| Skia | Rendering and text layout | `third_party/skia/LICENSE` |
| HarfBuzz | Text shaping through Skia | Skia external source `COPYING` |
| ICU | Unicode services and filtered text data | Skia external source `LICENSE` |
| FreeType | Font rasterization through Skia | Skia external source `LICENSE.TXT` |
| zlib | Compression support through Skia | Skia external source `LICENSE` |
| libpng | PNG export through Skia | Skia external source `LICENSE` |
| Dear ImGui | Host chrome | `third_party/imgui/LICENSE.txt` |
| SDL | macOS window/input backend | `third_party/sdl/LICENSE.txt` |
| Folly | Native utility library | Homebrew Folly source `LICENSE` |
| fmt | Formatting dependency | Homebrew fmt source `LICENSE` |
| glog | Logging dependency | `third_party/glog/COPYING` |
| double-conversion | Numeric conversion dependency | Homebrew double-conversion source `LICENSE` |
| Boost | Header-only and context dependencies | Hermes external Boost `LICENSE_1_0.txt` |

This inventory describes the linked release executable, not every package on
the build machine. Apple frameworks and the macOS C/C++ runtime are supplied by
the operating system and are not embedded in `rnsim`.
