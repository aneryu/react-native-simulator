#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <react-native-simulator/Scene.h>
#include <react-native-simulator/Interaction.h>

namespace ReactNativeSimulator {

enum class SimulatorMode {
  Headless,
  Interactive,
  Conformance,
};

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
  // JSON object forwarded as AppRegistry.runApplication initialProps.
  std::string initialPropsJson{"{}"};
  bool autoRunApplication{false};
  float viewportWidth{300.0f};
  float viewportHeight{80.0f};
  float pointScaleFactor{1.0f};
  // WindowInsets-style system chrome around the RN window, in logical dp.
  // Pixel 4a (1080x2340 @ 2.75) uses 140px status+cutout and 128px 3-button nav.
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
  // Called on the runtime thread after each retained mounting update. The
  // snapshot is immutable and may be handed to a frontend thread. The callback
  // must not access JSI or call back into this Engine instance.
  std::function<void(std::shared_ptr<const SceneSnapshot>)> onSceneUpdate;
  std::function<void(const InteractionResult&)> onActionResult;
  DevToolsConfig devTools;
};

struct EngineResult {
  int exitCode{1};
  std::string metricsJson;
  std::string error;
  std::shared_ptr<const SceneSnapshot> scene;
};

// Snapshot of AppRegistry applications the host can run. Thread-safe while
// run() is active. `initialProps` JSON is a caller-owned object; rootTag and
// fabric stay host-owned.
struct ApplicationLaunchState {
  bool initialBundlesLoaded{false};
  bool appRegistryReady{false};
  bool pending{false};
  std::vector<std::string> appKeys;
  std::optional<std::string> configuredAppKey;
  std::string configuredInitialPropsJson{"{}"};
  std::optional<std::string> runningAppKey;
  std::string lastError;
};

// Public, Node-independent embedding boundary. Bundle requests are queued in
// order and executed in one Hermes/ReactInstance when run() is called.
class Engine final {
 public:
  explicit Engine(EngineConfig config = {});
  ~Engine();

  Engine(Engine&&) noexcept;
  Engine& operator=(Engine&&) noexcept;
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void addAddon(std::string addon);
  void loadBundle(const std::filesystem::path& path);
  void loadBundle(std::string bytes, std::string sourceUrl);
  void setSceneUpdateCallback(
      std::function<void(std::shared_ptr<const SceneSnapshot>)> callback);
  void setActionResultCallback(
      std::function<void(const InteractionResult&)> callback);
  // Thread-safe while run() is active. Returns a monotonically increasing
  // sequence number or throws when the bounded queue cannot accept a discrete
  // action.
  std::uint64_t enqueueAction(InteractionAction action);
  ApplicationLaunchState applicationLaunchState() const;
  // Thread-safe while run() is active. Queues AppRegistry.runApplication on
  // the runtime thread. `initialPropsJson` must be a JSON object or empty.
  void runApplication(std::string appKey, std::string initialPropsJson = "{}");
  void requestStop() noexcept;
  // Interactive sessions only. Tears down the running ReactInstance and
  // replays CLI/config bundles on a new Hermes VM. Headless/conformance
  // ignore the request so finite workloads stay single-shot.
  void requestReload() noexcept;
  EngineResult run();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ReactNativeSimulator
