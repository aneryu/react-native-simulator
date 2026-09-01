#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct SimulatorEnvironmentConfig {
  std::optional<std::string> colorScheme;
  std::optional<std::string> appState;
  std::optional<bool> reduceMotion;
  std::optional<bool> invertColors;
  std::optional<bool> highTextContrast;
  std::optional<bool> screenReader;
  std::optional<bool> accessibilityService;
  std::optional<bool> grayscale;
  std::optional<bool> boldText;
  std::optional<bool> reduceTransparency;
  std::optional<bool> darkerSystemColors;
  std::optional<std::string> orientation;
};

struct SimulatorLocalConfig {
  int schemaVersion{1};
  std::string reactNative;
  std::string platform{"android"};
  std::optional<std::string> appKey;
  std::optional<std::string> initialPropsJson;
  std::optional<std::filesystem::path> bundle;
  std::optional<float> viewportWidth;
  std::optional<float> viewportHeight;
  std::optional<float> pointScaleFactor;
  std::optional<std::filesystem::path> fontDirectory;
  std::vector<std::filesystem::path> addons;
  SimulatorEnvironmentConfig environment;
};

SimulatorLocalConfig loadSimulatorConfig(const std::filesystem::path& path);
std::string normalizeInitialPropsJson(const std::string& json);
