#include "RuntimeProfile.h"

#include <stdexcept>

namespace jsi = facebook::jsi;
namespace react = facebook::react;

namespace {
class RegisteredRuntimeProfile final : public RuntimeProfile {
 public:
  RegisteredRuntimeProfile(
      std::string profileName,
      std::string platformName,
      std::string targetVersion,
      std::string compatibility,
      HeadlessRNModuleHost host)
      : name_(std::move(profileName)),
        platform_(std::move(platformName)),
        targetVersion_(std::move(targetVersion)),
        compatibility_(std::move(compatibility)),
        host_(std::move(host)) {}

  std::string name() const override {
    return name_;
  }
  std::string platform() const override {
    return platform_;
  }
  std::string bundleTargetReactNativeVersion() const override {
    return targetVersion_;
  }
  std::string compatibilityLevel() const override {
    return compatibility_;
  }
  std::shared_ptr<react::TurboModule> getTurboModule(
      jsi::Runtime& runtime,
      const std::string& moduleName,
      const std::shared_ptr<react::CallInvoker>& jsInvoker) const override {
    return getHeadlessRNModule(runtime, moduleName, name_, jsInvoker, host_);
  }
  std::vector<HeadlessModuleCapability> moduleCapabilities() const override {
    return getHeadlessRNModuleCapabilities(name_);
  }

 private:
  std::string name_;
  std::string platform_;
  std::string targetVersion_;
  std::string compatibility_;
  HeadlessRNModuleHost host_;
};
} // namespace

std::unique_ptr<RuntimeProfile> createRuntimeProfile(
    const std::string& name,
    HeadlessRNModuleHost host) {
  if (name == "macos-rn87") {
    return std::make_unique<RegisteredRuntimeProfile>(
        name, "macos", "0.87.0", "native-headless", std::move(host));
  }
  if (name == "android-rn87") {
    return std::make_unique<RegisteredRuntimeProfile>(
        name,
        "android",
        "0.87.0",
        "native-headless-platform-adapter",
        std::move(host));
  }
  if (name == "ios-rn87") {
    return std::make_unique<RegisteredRuntimeProfile>(
        name,
        "ios",
        "0.87.0",
        "native-headless-platform-adapter",
        std::move(host));
  }
  if (name == "android-rn73") {
    return std::make_unique<RegisteredRuntimeProfile>(
        name,
        "android",
        "0.73.10",
        "partial-compatibility-adapter",
        std::move(host));
  }
  throw std::invalid_argument("Unknown profile: " + name);
}
