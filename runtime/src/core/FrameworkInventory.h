#pragma once

#include "HeadlessRNModules.h"

#include <react-native-simulator/SimulatorAddon.h>

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <jsi/jsi.h>
#include <react/renderer/componentregistry/ComponentDescriptorProvider.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class SimulatorEventLoop;

namespace ReactNativeSimulator {

struct RuntimeProfileDescriptor {
  std::string name;
  std::string platform;
  std::string nativeReactNativeVersion{"0.87.0"};
  std::string compatibilityLevel;
};

struct ModuleContract {
  std::string name;
  RuntimeCapabilityClass classification{RuntimeCapabilityClass::HostAdapted};
  std::string owner;
  std::string note;
};

struct ComponentContract {
  std::string name;
  RuntimeCapabilityClass classification{RuntimeCapabilityClass::Implemented};
  std::string owner;
  AddonComponentKind kind{AddonComponentKind::FabricDescriptor};
  std::string note;
};

using TurboModuleFactory = std::function<std::shared_ptr<facebook::react::TurboModule>(
    facebook::jsi::Runtime&,
    const std::shared_ptr<facebook::react::CallInvoker>&)>;

struct FrameworkModuleEntry {
  ModuleContract contract;
  TurboModuleFactory factory;
};

struct FrameworkComponentEntry {
  ComponentContract contract;
  facebook::react::ComponentDescriptorProvider provider;
  std::shared_ptr<std::string> flavor;
};

struct FrameworkSurfaceInventory {
  RuntimeProfileDescriptor profile;
  std::vector<FrameworkModuleEntry> hostModules;
  std::vector<FrameworkModuleEntry> profileModules;
  std::vector<FrameworkComponentEntry> baseComponents;
  std::vector<FrameworkComponentEntry> officialComponents;
  std::vector<FrameworkComponentEntry> platformComponents;
};

struct ModuleOwnerRow {
  std::string name;
  std::string owner;
  std::optional<std::string> overlayOwner;
};

struct ComponentLedgerRow {
  std::string requestedName;
  std::string normalizedName;
  std::string canonicalName;
  facebook::react::ComponentHandle handle{0};
  std::string owner;
  AddonComponentKind kind{AddonComponentKind::FabricDescriptor};
  ComponentContract contract;
  std::vector<std::string> events;
  std::uint64_t generation{0};
};

struct InventoryRuntimeBindings {
  HeadlessRNModuleHost moduleHost;
  std::shared_ptr<SimulatorEventLoop> eventLoop;
};

FrameworkSurfaceInventory buildFrameworkSurfaceInventory(
    const std::string& profileName,
    const std::shared_ptr<InventoryRuntimeBindings>& bindings);

std::vector<FrameworkComponentEntry> allInventoryComponents(
    const FrameworkSurfaceInventory& inventory);

bool isKnownProfileName(const std::string& name);
RuntimeProfileDescriptor profileDescriptorFor(const std::string& name);

} // namespace ReactNativeSimulator
