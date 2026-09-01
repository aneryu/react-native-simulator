#pragma once

#include "HeadlessRNModules.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <jsi/jsi.h>

#include <memory>
#include <string>
#include <vector>

class RuntimeProfile {
 public:
  virtual ~RuntimeProfile() = default;
  virtual std::string name() const = 0;
  virtual std::string platform() const = 0;
  virtual std::string bundleTargetReactNativeVersion() const = 0;
  virtual std::string compatibilityLevel() const = 0;
  virtual std::shared_ptr<facebook::react::TurboModule> getTurboModule(
      facebook::jsi::Runtime& runtime,
      const std::string& moduleName,
      const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker) const = 0;
  virtual std::vector<HeadlessModuleCapability> moduleCapabilities() const = 0;
};

std::unique_ptr<RuntimeProfile> createRuntimeProfile(
    const std::string& name,
    HeadlessRNModuleHost host = {});
