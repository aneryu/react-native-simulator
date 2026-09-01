#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <jsi/jsi.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct HeadlessModuleCapability {
  std::string name;
  std::string fidelity;
};

struct HeadlessRNModuleHost {
  float viewportWidth{390.0f};
  float viewportHeight{844.0f};
  float pointScaleFactor{3.0f};
  float insetTop{24.0f};
  float insetBottom{0.0f};
  std::string scriptURL{"react-native-simulator://bundle"};
  std::filesystem::path assetDirectory;
};

std::shared_ptr<facebook::react::TurboModule> getHeadlessRNModule(
    facebook::jsi::Runtime& runtime,
    const std::string& name,
    const std::string& profile,
    const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker,
    const HeadlessRNModuleHost& host = {});

std::vector<std::string> getHeadlessRNModuleNames(const std::string& profile);
std::vector<HeadlessModuleCapability> getHeadlessRNModuleCapabilities(
    const std::string& profile);
