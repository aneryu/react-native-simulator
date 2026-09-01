#include "SkiaPngExporter.h"

#include "SkiaMountedTreeRenderer.h"

#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/encode/SkPngEncoder.h"

#include <fstream>
#include <stdexcept>

namespace ReactNativeSimulator {

namespace {
SkiaExportedImage writeFrame(
    const SkiaRenderedFrame& frame,
    const std::filesystem::path& outputPath) {
  if (!frame) {
    throw std::runtime_error(frame.error);
  }

  const auto imageInfo = SkImageInfo::Make(
      frame.width, frame.height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
  const SkPixmap pixmap(imageInfo, frame.rgba.data(), frame.rowBytes);
  const auto encoded = SkPngEncoder::Encode(pixmap, {});
  if (encoded == nullptr) {
    throw std::runtime_error("SkPngEncoder failed");
  }

  if (!outputPath.parent_path().empty()) {
    std::filesystem::create_directories(outputPath.parent_path());
  }
  std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot create screenshot: " +
                             outputPath.string());
  }
  output.write(static_cast<const char*>(encoded->data()),
               static_cast<std::streamsize>(encoded->size()));
  if (!output) {
    throw std::runtime_error("failed to write screenshot: " +
                             outputPath.string());
  }
  return {.width = frame.width, .height = frame.height};
}
} // namespace

SkiaExportedImage exportSceneToPng(
    const SceneSnapshot& scene,
    const std::filesystem::path& outputPath,
    const std::filesystem::path& androidFontDirectory) {
  SkiaMountedTreeRenderer renderer(androidFontDirectory);
  return writeFrame(renderer.render(scene), outputPath);
}

} // namespace ReactNativeSimulator
