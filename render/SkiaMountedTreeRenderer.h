#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <react-native-simulator/Scene.h>

namespace folly {
struct dynamic;
}

namespace ReactNativeSimulator {

struct SkiaRenderedFrame {
  int width{0};
  int height{0};
  std::size_t rowBytes{0};
  std::vector<std::uint8_t> rgba;
  std::string error;

  explicit operator bool() const {
    return error.empty() && width > 0 && height > 0 && !rgba.empty();
  }
};

// CPU raster backend for a retained Fabric mounting snapshot. Coordinates stay
// in RN logical points; Skia rasterizes at the surface pointScaleFactor.
class SkiaMountedTreeRenderer final {
 public:
  explicit SkiaMountedTreeRenderer(
      std::filesystem::path androidFontDirectory = {});
  ~SkiaMountedTreeRenderer();

  SkiaMountedTreeRenderer(SkiaMountedTreeRenderer&&) noexcept;
  SkiaMountedTreeRenderer& operator=(SkiaMountedTreeRenderer&&) noexcept;
  SkiaMountedTreeRenderer(const SkiaMountedTreeRenderer&) = delete;
  SkiaMountedTreeRenderer& operator=(const SkiaMountedTreeRenderer&) = delete;

  SkiaRenderedFrame render(const folly::dynamic& metrics);
  // Render the independent Inspector scene wire payload. The payload is
  // separate from diagnostic metrics so rendering cannot depend on unrelated
  // runtime fields.
  SkiaRenderedFrame renderSceneWire(const folly::dynamic& sceneWire);
  SkiaRenderedFrame render(
      const SceneSnapshot& scene,
      std::optional<std::int64_t> animationTimeMs = std::nullopt);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ReactNativeSimulator
