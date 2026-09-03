#pragma once

#include <react-native-simulator/Engine.h>
#include <react-native-simulator/SimulatorAddon.h>

#include "LaunchPlan.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace facebook::jsi {
class Runtime;
}

namespace ReactNativeSimulator {

struct StagedAddonProvider {
  std::string addonName;
  facebook::react::ComponentDescriptorProvider provider;
};

class AddonFabricRegistrar::HostSession {
 public:
  const AddonManifest* manifest{nullptr};
  std::string addonName;
  std::vector<StagedAddonProvider> providers;
  std::unordered_map<std::string, AddonMountHandler> mountHandlers;
  std::unordered_map<std::string, std::unordered_map<std::string, AddonCommandHandler>>
      commandHandlers;
};

class AddonRuntimeExecutor::State {
 public:
  std::atomic<bool> open{true};
  std::thread::id runtimeThread;
  std::function<bool(std::function<void(facebook::jsi::Runtime&)>)> enqueue;
  std::atomic<std::uint64_t> droppedPosts{0};
};

class EngineAddonHost final : public AddonHost {
 public:
  explicit EngineAddonHost(AddonHostSnapshot snapshot)
      : snapshot_(std::move(snapshot)) {}
  const AddonHostSnapshot& snapshot() const noexcept override {
    return snapshot_;
  }
  AddonHostSnapshot snapshot_;
};

AddonRuntimeExecutor makeAddonRuntimeExecutor(
    std::shared_ptr<AddonRuntimeExecutor::State> state);

} // namespace ReactNativeSimulator
