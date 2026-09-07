#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <jsi/jsi.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

struct HeadlessRNModuleHost {
  float viewportWidth{390.0f};
  float viewportHeight{844.0f};
  float pointScaleFactor{3.0f};
  float insetTop{24.0f};
  float insetBottom{0.0f};
  std::string scriptURL{"react-native-simulator://bundle"};
  std::filesystem::path assetDirectory;
  std::optional<std::string> initialUrl;
};

std::shared_ptr<facebook::react::TurboModule> getHeadlessRNModule(
    facebook::jsi::Runtime& runtime,
    const std::string& name,
    const std::string& profile,
    const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker,
    const HeadlessRNModuleHost& host = {});
