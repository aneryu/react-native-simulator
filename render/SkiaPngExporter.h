#pragma once

#include <filesystem>
#include <react-native-simulator/Scene.h>

namespace ReactNativeSimulator {

struct SkiaExportedImage {
  int width{0};
  int height{0};
};

SkiaExportedImage
exportSceneToPng(const SceneSnapshot& scene,
                 const std::filesystem::path& outputPath,
                 const std::filesystem::path& androidFontDirectory = {});

} // namespace ReactNativeSimulator
