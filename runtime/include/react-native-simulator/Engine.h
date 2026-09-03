#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <react-native-simulator/Interaction.h>
#include <react-native-simulator/Scene.h>
#include <react-native-simulator/SimulatorAddon.h>

namespace ReactNativeSimulator {

enum class EngineState { Draft, Planned, Running, Finished };

struct DevToolsConfig {
  bool enabled{false};
  bool open{false};
  uint16_t port{9229};
  int waitForDebuggerMs{10000};
  int keepAliveMs{30000};
  bool waitForDisconnect{false};
  std::string appName{"react-native-simulator"};
  std::string deviceName{"macOS headless"};
  std::optional<std::filesystem::path> frontendDirectory;
  std::optional<std::filesystem::path> shellPath;
};

struct EngineConfig {
  SimulatorMode mode{SimulatorMode::Headless};
  int iterations{20};
  int timeoutMs{5000};
  int settleMs{0};
  int seed{1};
  std::string workload{"mixed"};
  std::string profile{"macos-rn87"};
  std::optional<std::string> appKey;
  std::string initialPropsJson{"{}"};
  bool autoRunApplication{false};
  float viewportWidth{300.0f};
  float viewportHeight{80.0f};
  float pointScaleFactor{1.0f};
  float insetTop{24.0f};
  float insetBottom{0.0f};
  std::optional<std::filesystem::path> fontDirectory;
  std::optional<std::filesystem::path> assetDirectory;
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
  std::optional<std::filesystem::path> tracePath;
  bool requireReactFabric{false};
  bool requireNoPendingWork{false};
  bool failOnComponentFallback{false};
  std::optional<std::filesystem::path> inspectorSocket;
  std::function<void(std::shared_ptr<const SceneSnapshot>)> onSceneUpdate;
  std::function<void(const InteractionResult&)> onActionResult;
  DevToolsConfig devTools;
};

struct EngineResult {
  int exitCode{1};
  std::string error;
  std::string metricsJson;
  std::shared_ptr<const SceneSnapshot> scene;
};

struct ApplicationLaunchState {
  std::uint64_t runtimeGeneration{0};
  bool initialBundlesLoaded{false};
  bool appRegistryReady{false};
  bool pending{false};
  std::vector<std::string> appKeys;
  std::optional<std::string> configuredAppKey;
  std::string configuredInitialPropsJson{"{}"};
  std::optional<std::string> runningAppKey;
  std::string lastError;
};

enum class RuntimePhase {
  Idle,
  Initializing,
  LoadingBundle,
  StartingApplication,
  ChoosingApplication,
  Running,
  Reloading,
  PausedAfterError,
  Stopped,
};

enum class HMRStatus {
  Disabled,
  Enabling,
  Enabled,
  Failed,
};

enum class RuntimeDiagnosticKind {
  JavaScriptError,
  ApplicationError,
  MissingNativeModule,
  FallbackComponent,
};

struct RuntimeStackFrame {
  std::optional<std::string> file;
  std::string method;
  std::optional<int> line;
  std::optional<int> column;
};

struct RuntimeDiagnostic {
  RuntimeDiagnosticKind kind{RuntimeDiagnosticKind::JavaScriptError};
  std::string name;
  std::string message;
  bool fatal{false};
  std::vector<RuntimeStackFrame> stack;
};

struct RuntimeCapabilityUsage {
  std::string type;
  std::string name;
  std::string note;
  RuntimeCapabilityClass classification{RuntimeCapabilityClass::Implemented};
  std::string owner;
};

struct RuntimeStatus {
  std::uint64_t runtimeGeneration{0};
  RuntimePhase phase{RuntimePhase::Idle};
  HMRStatus hmr{HMRStatus::Disabled};
  std::string hmrError;
  std::vector<RuntimeDiagnostic> diagnostics;
  std::vector<RuntimeCapabilityUsage> capabilityUsages;
};

struct BuiltInAddonSpec {
  std::string catalogKey;
};
struct ModuleAddonSpec {
  std::filesystem::path path;
};
struct InProcessAddonSpec {
  std::unique_ptr<SimulatorAddon> addon;
  std::string diagnosticLabel;
};
using AddonSpec =
    std::variant<BuiltInAddonSpec, ModuleAddonSpec, InProcessAddonSpec>;

struct AddonRequest {
  AddonSpec spec;
  std::vector<AddonRequestOrigin> requestedBy;
};

struct AddonOrigin {
  AddonSource source;
  std::string locator;
  std::vector<AddonRequestOrigin> requestedBy;
};

struct InitialBundleSpec {
  std::string sourceUrl;
  std::optional<std::filesystem::path> path;
  std::optional<std::string> body;
};

struct ResolvedBundleCompatibility {
  std::string nativeReactNativeVersion{"0.87.0"};
  std::string targetFamily{"0.87.x"};
  std::string jsVisibleReactNativeVersion{"0.87.0"};
  std::string level{"native-headless"};
  std::optional<std::string> compatAddon;
  bool hbcTranslation{false};
};

class LaunchDraft;
class PreparedAddonCandidates;
class PreparedLaunchPlan;
PreparedAddonCandidates prepareExplicitAddons(LaunchDraft& draft);
PreparedLaunchPlan finalizeLaunchPlan(
    LaunchDraft&& draft,
    PreparedAddonCandidates&& candidates);

class TerminalLaunchPlanError : public std::runtime_error {
 public:
  explicit TerminalLaunchPlanError(std::string message);
};

class RetryableNetworkError : public std::runtime_error {
 public:
  explicit RetryableNetworkError(std::string message);
};

class AddonContractViolation : public std::runtime_error {
 public:
  AddonContractViolation(
      std::string addon,
      std::string operation,
      std::string surface,
      std::uint64_t generation,
      std::string message);
  const std::string& addon() const noexcept;
  const std::string& operation() const noexcept;
  const std::string& surface() const noexcept;
  std::uint64_t generation() const noexcept;

 private:
  std::string addon_;
  std::string operation_;
  std::string surface_;
  std::uint64_t generation_{0};
};

class LaunchDraft {
 public:
  explicit LaunchDraft(EngineConfig config = {});
  LaunchDraft(LaunchDraft&&) noexcept;
  LaunchDraft& operator=(LaunchDraft&&) noexcept;
  ~LaunchDraft();
  LaunchDraft(const LaunchDraft&) = delete;
  LaunchDraft& operator=(const LaunchDraft&) = delete;

  void setProjectKind(ProjectKind kind);
  void addBuiltInAddon(
      std::string_view catalogKey,
      AddonRequestOrigin origin = AddonRequestOrigin::Embedder);
  void addAddonPath(
      const std::filesystem::path& path,
      AddonRequestOrigin origin = AddonRequestOrigin::Embedder);
  void addAddon(
      std::unique_ptr<SimulatorAddon> addon,
      std::string diagnosticLabel,
      AddonRequestOrigin origin = AddonRequestOrigin::Embedder);
  void disableAddon(std::string_view catalogKey);
  void setAutoAddons(bool enabled);
  void addBundle(InitialBundleSpec bundle);
  void setInitialUrl(std::optional<std::string> url);

  const EngineConfig& config() const noexcept;
  EngineConfig& config() noexcept;

  class Impl;

 private:
  friend class PreparedAddonCandidates;
  friend class PreparedLaunchPlan;
  friend PreparedAddonCandidates prepareExplicitAddons(LaunchDraft& draft);
  friend PreparedLaunchPlan finalizeLaunchPlan(
      LaunchDraft&& draft,
      PreparedAddonCandidates&& candidates);
  std::unique_ptr<Impl> impl_;
};

class PreparedAddonCandidates {
 public:
  PreparedAddonCandidates();
  PreparedAddonCandidates(PreparedAddonCandidates&&) noexcept;
  PreparedAddonCandidates& operator=(PreparedAddonCandidates&&) noexcept;
  ~PreparedAddonCandidates();
  PreparedAddonCandidates(const PreparedAddonCandidates&) = delete;
  PreparedAddonCandidates& operator=(const PreparedAddonCandidates&) = delete;
  explicit operator bool() const noexcept;

  class Impl;

 private:
  friend PreparedAddonCandidates prepareExplicitAddons(LaunchDraft& draft);
  friend PreparedLaunchPlan finalizeLaunchPlan(
      LaunchDraft&& draft,
      PreparedAddonCandidates&& candidates);
  friend class Engine;
  std::unique_ptr<Impl> impl_;
};

class PreparedLaunchPlan {
 public:
  PreparedLaunchPlan();
  PreparedLaunchPlan(PreparedLaunchPlan&&) noexcept;
  PreparedLaunchPlan& operator=(PreparedLaunchPlan&&) noexcept;
  ~PreparedLaunchPlan();
  PreparedLaunchPlan(const PreparedLaunchPlan&) = delete;
  PreparedLaunchPlan& operator=(const PreparedLaunchPlan&) = delete;
  explicit operator bool() const noexcept;

  class Impl;

 private:
  friend PreparedLaunchPlan finalizeLaunchPlan(
      LaunchDraft&& draft,
      PreparedAddonCandidates&& candidates);
  friend class Engine;
  std::unique_ptr<Impl> impl_;
};

PreparedAddonCandidates prepareExplicitAddons(LaunchDraft& draft);
PreparedLaunchPlan finalizeLaunchPlan(
    LaunchDraft&& draft,
    PreparedAddonCandidates&& candidates);

class Engine final {
 public:
  Engine();
  ~Engine();

  Engine(Engine&&) noexcept;
  Engine& operator=(Engine&&) noexcept;
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  EngineState state() const noexcept;
  void applyLaunchPlan(PreparedLaunchPlan&& plan);
  void setSceneUpdateCallback(
      std::function<void(std::shared_ptr<const SceneSnapshot>)> callback);
  void setActionResultCallback(
      std::function<void(const InteractionResult&)> callback);
  std::uint64_t enqueueAction(InteractionAction action);
  ApplicationLaunchState applicationLaunchState() const;
  RuntimeStatus runtimeStatus() const;
  void runApplication(std::string appKey, std::string initialPropsJson = "{}");
  void requestStop() noexcept;
  void requestReload() noexcept;
  EngineResult run();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ReactNativeSimulator
