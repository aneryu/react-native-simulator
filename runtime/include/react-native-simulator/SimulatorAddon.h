#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <folly/dynamic.h>
#include <jsi/jsi.h>
#include <react/renderer/componentregistry/ComponentDescriptorProvider.h>
#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/core/ShadowNode.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef RNS_EXPORT
#define RNS_EXPORT __attribute__((visibility("default")))
#endif

namespace ReactNativeSimulator {

enum class SimulatorMode {
  Headless,
  Interactive,
  Conformance,
};

enum class RuntimeCapabilityClass {
  Implemented,
  HostAdapted,
  Mocked,
  LayoutOnly,
  Unavailable,
};

enum class AddonComponentKind { DescriptorOnlyMock, FabricDescriptor };

enum class AddonRole { Application, Community, VersionCompat };

enum class AddonMountKind { Mounted, Updated, Unmounted };

enum class AddonSource { BuiltIn, Module, InProcess };

enum class AddonRequestOrigin { Auto, Config, Cli, Embedder, Test };

enum class AddonAutoPolicy { Always, Expo, Never };

enum class ProjectKind { Plain, Expo };

inline constexpr std::uint32_t kSimulatorAddonAbiVersion = 4;
inline constexpr const char* kSimulatorAddonEntryPoint =
    "react_native_simulator_addon_v4";

struct AddonModuleDeclaration {
  std::string name;
  RuntimeCapabilityClass classification{RuntimeCapabilityClass::HostAdapted};
  std::string note;
};

struct AddonModuleOverlayDeclaration {
  std::string moduleName;
  std::string note;
};

struct AddonComponentDeclaration {
  std::string name;
  RuntimeCapabilityClass classification{RuntimeCapabilityClass::HostAdapted};
  AddonComponentKind kind{AddonComponentKind::FabricDescriptor};
  std::vector<std::string> events;
  std::vector<std::string> commands;
  std::string note;
};

struct AddonNumericConstant {
  std::string name;
  double value{0};
};

struct AddonCommand {
  std::string name;
  std::int32_t id{0};
};

struct AddonViewManagerConfig {
  std::string name;
  std::vector<AddonNumericConstant> numericConstants;
  std::vector<AddonCommand> commands;
};

struct AddonBundleCompatibilityClaim {
  std::string targetFamily;
  std::string jsVisibleReactNativeVersion;
  std::string level;
};

struct AddonManifest {
  std::string name;
  std::string addonVersion;
  AddonRole role{AddonRole::Application};
  std::vector<std::string> allowedProfiles;
  std::vector<AddonModuleDeclaration> modules;
  std::vector<AddonModuleOverlayDeclaration> moduleOverlays;
  std::vector<AddonComponentDeclaration> components;
  std::vector<AddonViewManagerConfig> viewManagerConfigs;
  std::optional<AddonBundleCompatibilityClaim> bundleCompatibility;
};

struct AddonViewport {
  float width{0};
  float height{0};
  float pointScaleFactor{1};
  float insetTop{0};
  float insetRight{0};
  float insetBottom{0};
  float insetLeft{0};
};

struct AddonHostSnapshot {
  std::uint64_t revision{1};
  std::string profileName;
  std::string platform;
  std::string reactNativeVersion;
  std::string hermesVersion;
  std::string bundleTargetFamily;
  std::string jsVisibleReactNativeVersion;
  SimulatorMode mode{SimulatorMode::Headless};
  AddonViewport viewport;
  std::optional<std::filesystem::path> assetDirectory;
  std::optional<std::filesystem::path> fontDirectory;
  std::optional<std::string> initialUrl;
  std::string colorScheme{"light"};
  std::string appState{"active"};
  bool reduceMotion{false};
};

class AddonHost {
 public:
  virtual ~AddonHost() = default;
  virtual const AddonHostSnapshot& snapshot() const noexcept = 0;
};

struct AddonMountedNode {
  std::uint64_t generation{0};
  facebook::react::SurfaceId surfaceId{0};
  facebook::react::Tag tag{0};
  std::string componentName;
  std::shared_ptr<const facebook::react::ShadowNode> shadowNode;
  facebook::react::LayoutMetrics layoutMetrics{};
};

using AddonMountHandler =
    std::function<void(AddonMountKind kind, const AddonMountedNode& node)>;
using AddonCommandHandler = std::function<void(
    const AddonMountedNode& node,
    std::string_view command,
    const folly::dynamic& args)>;

class AddonFabricRegistrar {
 public:
  class HostSession;

  AddonFabricRegistrar(
      HostSession& session,
      std::string addonName,
      const AddonManifest& manifest);
  void registerDescriptor(facebook::react::ComponentDescriptorProvider provider);
  void onMount(std::string_view ownedComponent, AddonMountHandler handler);
  void onCommand(
      std::string_view ownedComponent,
      std::string_view declaredCommand,
      AddonCommandHandler handler);

 private:
  HostSession* session_{nullptr};
  std::string addonName_;
  const AddonManifest* manifest_{nullptr};
};

class AddonRuntimeExecutor {
 public:
  AddonRuntimeExecutor() = default;
  bool post(std::function<void(facebook::jsi::Runtime&)> fn) const noexcept;

  class State;

 private:
  friend class Engine;
  friend AddonRuntimeExecutor makeAddonRuntimeExecutor(
      std::shared_ptr<State> state);
  std::shared_ptr<State> state_;
};

struct AddonGenerationContext {
  std::uint64_t generation{0};
  AddonRuntimeExecutor executor;
};

class SimulatorAddon {
 public:
  virtual ~SimulatorAddon() noexcept = default;

  virtual AddonManifest manifest() const = 0;
  virtual void bind(const AddonHost& host) = 0;
  virtual void unbind() noexcept = 0;
  virtual std::shared_ptr<facebook::react::TurboModule> getTurboModule(
      const AddonGenerationContext& context,
      facebook::jsi::Runtime& runtime,
      const std::string& moduleName,
      const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker) = 0;
  virtual std::shared_ptr<facebook::react::TurboModule> wrapTurboModule(
      const AddonGenerationContext& context,
      facebook::jsi::Runtime& runtime,
      const std::string& moduleName,
      std::shared_ptr<facebook::react::TurboModule> framework,
      const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker) = 0;
  virtual void configureFabric(
      const AddonGenerationContext& context,
      AddonFabricRegistrar& registrar) = 0;
  virtual void installJSI(
      const AddonGenerationContext& context,
      facebook::jsi::Runtime& runtime,
      const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker) = 0;
  virtual void hostSnapshotChanged(const AddonHostSnapshot& snapshot) = 0;
  virtual void quiesceGeneration(std::uint64_t generation) noexcept = 0;
};

struct SimulatorAddonDescriptor {
  std::uint32_t descriptorSize;
  std::uint32_t abiVersion;
  const char* addonApiFingerprint;
  const char* name;
  const char* reactNativeVersion;
  const char* hermesVersion;
  SimulatorAddon* (*create)();
  void (*destroy)(SimulatorAddon*) noexcept;
};

using GetSimulatorAddonDescriptorV4 =
    const SimulatorAddonDescriptor* (*)() noexcept;

} // namespace ReactNativeSimulator

extern "C" RNS_EXPORT const ReactNativeSimulator::SimulatorAddonDescriptor*
react_native_simulator_addon_v4() noexcept;
