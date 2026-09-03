#pragma once

#include <react-native-simulator/Engine.h>

#include "FrameworkInventory.h"

#include <folly/dynamic.h>

#include <dlfcn.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ReactNativeSimulator {

struct DlLibrary {
  void* handle{nullptr};
  std::filesystem::path path;
  ~DlLibrary() {
    if (handle != nullptr) {
      dlclose(handle);
      handle = nullptr;
    }
  }
};

struct AddonDeleter {
  void (*destroy)(SimulatorAddon*) noexcept {nullptr};
  void operator()(SimulatorAddon* addon) const noexcept {
    if (addon == nullptr) {
      return;
    }
    if (destroy != nullptr) {
      destroy(addon);
    } else {
      delete addon;
    }
  }
};

using AddonPtr = std::unique_ptr<SimulatorAddon, AddonDeleter>;

struct CommittedAddon {
  AddonOrigin origin;
  std::shared_ptr<DlLibrary> library;
  AddonPtr addon;
  AddonManifest manifest;
  bool bindEntered{false};
};

struct FileIdentity {
  std::filesystem::path canonical;
  std::uint64_t device{0};
  std::uint64_t inode{0};
};

class LaunchDraft::Impl {
 public:
  EngineConfig config;
  ProjectKind projectKind{ProjectKind::Plain};
  bool autoAddons{true};
  std::vector<std::string> disabled;
  std::vector<AddonRequest> requests;
  std::vector<InitialBundleSpec> bundles;
  std::optional<std::string> initialUrl;
  bool explicitPrepared{false};
  std::string explicitFingerprint;
  bool explicitMutatedAfterPrepare{false};
};

class PreparedAddonCandidates::Impl {
 public:
  bool valid{false};
  std::string fingerprint;
  std::vector<CommittedAddon> addons;
};

class PreparedLaunchPlan::Impl {
 public:
  bool valid{false};
  EngineConfig config;
  ProjectKind projectKind{ProjectKind::Plain};
  std::shared_ptr<InventoryRuntimeBindings> bindings;
  FrameworkSurfaceInventory inventory;
  std::vector<CommittedAddon> addons;
  std::vector<InitialBundleSpec> bundles;
  std::optional<std::string> initialUrl;
  ResolvedBundleCompatibility compatibility;
  std::unordered_map<std::string, ModuleOwnerRow> moduleOwners;
  std::unordered_map<std::string, std::string> overlays;
  std::vector<ComponentLedgerRow> expectedComponents;
  std::unordered_set<std::string> frameworkModuleNames;
  std::unordered_set<std::string> frameworkComponentNames;
};

void destroyCommittedAddons(std::vector<CommittedAddon>& addons) noexcept;
std::string describeAddonOrigin(const AddonOrigin& origin);
std::string componentNameByReactViewNameGuarded(std::string_view name);

} // namespace ReactNativeSimulator
