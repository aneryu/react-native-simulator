#include <hermes/hermes.h>
#include <hermes/inspector-modern/chrome/HermesRuntimeTargetDelegate.h>
#include <cxxreact/JSBigString.h>
#include <folly/json.h>
#include <glog/logging.h>
#include <jsi/instrumentation.h>
#include <jsi/jsi.h>
#include <jsi/JSIDynamic.h>
#include <ReactCommon/TurboModuleBinding.h>
#include <react/renderer/runtimescheduler/RuntimeSchedulerCallInvoker.h>
#include <react/nativemodule/core/ReactCommon/TurboModuleUtils.h>
#include <react/runtime/JSRuntimeFactory.h>
#include <react/runtime/ReactInstance.h>
#include <react/runtime/TimerManager.h>

#include <react-native-simulator/Engine.h>

#include "SimulatorEventLoop.h"
#include "HostChrome.h"
#include "HeadlessBlob.h"
#include "HostEnvironment.h"
#include "HeadlessBackPress.h"
#include "HeadlessKeyboard.h"
#include "HeadlessHttp.h"
#include "HeadlessWebSocket.h"
#include "HttpBundleLoader.h"
#include "PackagerConnection.h"
#include "DevToolsHost.h"
#include "InspectorTransport.h"
#include <react-native-simulator/SimulatorAddon.h>
#include "HeadlessFabric.h"
#include "HeadlessReactFabric.h"
#include "HeadlessOfficialComponents.h"
#include "HeadlessRNModules.h"
#include "HeadlessTurboModules.h"
#include "RuntimeProfile.h"
#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsDynamicProvider.h>
#if RNS_ENABLE_SKIA
#include "SkiaTextLayoutEngine.h"
#endif

#include <cmath>
#include <filesystem>
#include <system_error>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <mach/mach.h>
#include <sys/resource.h>
#include <unistd.h>

namespace jsi = facebook::jsi;
namespace react = facebook::react;
namespace rns = ReactNativeSimulator;

jsi::Object makeLegacyViewManagerConfig(
    jsi::Runtime& runtime,
    const rns::SimulatorAddonViewManagerConfig* addonConfig) {
  jsi::Object config(runtime);
  jsi::Object nativeProps(runtime);
  jsi::Object commands(runtime);
  jsi::Object constants(runtime);
  jsi::Object bubbling(runtime);
  jsi::Object direct(runtime);
  if (addonConfig != nullptr) {
    for (const auto& constant : addonConfig->numericConstants) {
      constants.setProperty(runtime, constant.name.c_str(), constant.value);
    }
    for (const auto& command : addonConfig->commands) {
      commands.setProperty(runtime, command.name.c_str(), command.id);
    }
  }
  config.setProperty(runtime, "NativeProps", std::move(nativeProps));
  config.setProperty(runtime, "Commands", std::move(commands));
  config.setProperty(runtime, "Constants", std::move(constants));
  config.setProperty(runtime, "bubblingEventTypes", std::move(bubbling));
  config.setProperty(runtime, "directEventTypes", std::move(direct));
  return config;
}

class SimulatorHermesRuntime final : public react::JSRuntime {
 public:
  explicit SimulatorHermesRuntime(
      std::unique_ptr<facebook::hermes::HermesRuntime> runtime)
      : runtime_(std::move(runtime)),
        hermesRuntime_(
            static_cast<facebook::hermes::HermesRuntime&>(*runtime_)) {}

  jsi::Runtime& getRuntime() noexcept override {
    return *runtime_;
  }

  react::jsinspector_modern::RuntimeTargetDelegate&
  getRuntimeTargetDelegate() override {
    if (!targetDelegate_) {
      targetDelegate_.emplace(runtime_, hermesRuntime_);
    }
    return *targetDelegate_;
  }

  void unstable_initializeOnJsThread() override {
    hermesRuntime_.registerForProfiling();
  }

 private:
  std::shared_ptr<jsi::Runtime> runtime_;
  facebook::hermes::HermesRuntime& hermesRuntime_;
  std::optional<react::jsinspector_modern::HermesRuntimeTargetDelegate>
      targetDelegate_;
};

struct InitialBundle {
  std::string sourceUrl;
  std::optional<std::filesystem::path> path;
  std::optional<std::string> bytes;
  bool http{false};
};

struct PendingActionBatch {
  std::shared_ptr<react::Promise> promise;
  std::vector<rns::InteractionResult> results;
  std::size_t remaining{0};
  bool rejected{false};
};

struct QueuedInteractionAction {
  rns::InteractionAction action;
  std::vector<std::uint64_t> sequences;
  std::shared_ptr<PendingActionBatch> batch;
  std::size_t batchIndex{0};
};

rns::RuntimeCapabilityClass classifyRuntimeCapability(
    std::string_view type,
    std::string_view name,
    std::string_view fidelity) {
  if (fidelity.find("unavailable") != std::string_view::npos) {
    return rns::RuntimeCapabilityClass::Unavailable;
  }
  if (type == "component" &&
      (fidelity.find("fallback") != std::string_view::npos ||
       fidelity.find("layout-only") != std::string_view::npos)) {
    return rns::RuntimeCapabilityClass::LayoutOnly;
  }
  static const std::unordered_set<std::string_view> kMockedModules{
      "SoundManager",
      "HeadlessJsTaskSupport",
      "FrameRateLogger",
      "ModalManager",
  };
  if (fidelity.find("mock") != std::string_view::npos ||
      fidelity.find("descriptor-only") != std::string_view::npos ||
      fidelity.find("tester-stub") != std::string_view::npos ||
      fidelity.find("fixed-fixture") != std::string_view::npos ||
      (type == "module" && kMockedModules.contains(name))) {
    return rns::RuntimeCapabilityClass::Mocked;
  }
  if (fidelity == "real-headless") {
    return rns::RuntimeCapabilityClass::Implemented;
  }
  static const std::unordered_set<std::string_view> kHostAdaptedModules{
      "NativePerformanceCxx",
      "DevSettings",
      "DeviceInfo",
      "SourceCode",
      "AppState",
      "Appearance",
      "WebSocketModule",
      "BlobModule",
      "FileReaderModule",
      "ImageLoader",
      "ImageEditingManager",
      "ImageStoreManager",
      "Clipboard",
      "KeyboardObserver",
      "AccessibilityInfo",
      "I18nManager",
      "Networking",
      "SegmentFetcher",
      "PlatformConstants",
      "StatusBarManager",
      "ToastAndroid",
      "DeviceEventManager",
      "ExceptionsManager",
      "LogBox",
      "DevLoadingView",
      "RedBox",
      "PushNotificationManager",
      "SettingsManager",
      "ReactDevToolsSettingsManager",
      "ReactDevToolsRuntimeSettingsModule",
  };
  if ((type == "module" && kHostAdaptedModules.contains(name)) ||
      fidelity.find("adapter") != std::string_view::npos ||
      fidelity.find("headless") != std::string_view::npos ||
      fidelity.find("host-") != std::string_view::npos ||
      fidelity.find("urlsession") != std::string_view::npos ||
      fidelity.find("nspasteboard") != std::string_view::npos ||
      fidelity.find("imageio") != std::string_view::npos ||
      fidelity.find("partial") != std::string_view::npos) {
    return rns::RuntimeCapabilityClass::HostAdapted;
  }
  return rns::RuntimeCapabilityClass::Implemented;
}

std::string componentFidelityForBuild(std::string_view fidelity) {
#if RNS_ENABLE_SKIA
  return std::string(fidelity);
#else
  if (fidelity.starts_with("skia-")) {
    return "unavailable-without-skia";
  }
  return std::string(fidelity);
#endif
}

rns::RuntimeCapabilityUsage makeRuntimeCapabilityUsage(
    std::string type,
    std::string name,
    std::string fidelity) {
  const auto classification =
      classifyRuntimeCapability(type, name, fidelity);
  return {
      .type = std::move(type),
      .name = std::move(name),
      .fidelity = std::move(fidelity),
      .classification = classification,
  };
}

class rns::Engine::Impl {
 public:
  explicit Impl(rns::EngineConfig configValue)
      : config(std::move(configValue)) {
#if !RNS_ENABLE_SKIA
    if (config.fontDirectory) {
      throw std::invalid_argument(
          "EngineConfig::fontDirectory requires RNS_ENABLE_SKIA=ON");
    }
#endif
    launchState.configuredAppKey = config.appKey;
    launchState.configuredInitialPropsJson =
        config.initialPropsJson.empty() ? "{}" : config.initialPropsJson;
  }

  rns::EngineConfig config;
  std::vector<std::string> addons;
  std::vector<InitialBundle> bundles;
  std::atomic<bool> ran{false};
  std::atomic<bool> running{false};
  std::atomic<bool> finished{false};
  std::atomic<bool> stopRequested{false};
  std::atomic<bool> reloadRequested{false};
  std::string lastInitialPropsJson{"{}"};
  std::mutex actionMutex;
  std::deque<QueuedInteractionAction> actions;
  std::uint64_t nextActionSequence{1};
  mutable std::mutex applicationMutex;
  rns::ApplicationLaunchState launchState;
  mutable std::mutex runtimeStatusMutex;
  rns::RuntimeStatus runtimeStatus;
  struct PendingApplication {
    std::string appKey;
    std::string initialPropsJson;
  };
  std::optional<PendingApplication> pendingApplication;
  std::optional<int> runningRootTag;

  void beginRuntimeGeneration(
      std::uint64_t generation,
      rns::RuntimePhase phase) {
    std::lock_guard lock(runtimeStatusMutex);
    runtimeStatus.runtimeGeneration = generation;
    runtimeStatus.phase = phase;
    runtimeStatus.hmr = rns::HMRStatus::Disabled;
    runtimeStatus.hmrError.clear();
    runtimeStatus.diagnostics.clear();
    runtimeStatus.capabilityUsages.clear();
  }

  void setRuntimePhase(rns::RuntimePhase phase) {
    std::lock_guard lock(runtimeStatusMutex);
    runtimeStatus.phase = phase;
  }

  void beginApplicationLaunch() {
    std::lock_guard lock(runtimeStatusMutex);
    if (runtimeStatus.phase != rns::RuntimePhase::Running &&
        runtimeStatus.phase != rns::RuntimePhase::ChoosingApplication) {
      throw std::logic_error(
          "runApplication requires runtime phase Running or "
          "ChoosingApplication");
    }
    runtimeStatus.phase = rns::RuntimePhase::StartingApplication;
  }

  void setHMRStatus(rns::HMRStatus status, std::string error = {}) {
    std::lock_guard lock(runtimeStatusMutex);
    runtimeStatus.hmr = status;
    runtimeStatus.hmrError = std::move(error);
  }

  void recordRuntimeDiagnostic(rns::RuntimeDiagnostic diagnostic) {
    std::lock_guard lock(runtimeStatusMutex);
    const auto duplicate = std::find_if(
        runtimeStatus.diagnostics.begin(),
        runtimeStatus.diagnostics.end(),
        [&](const auto& current) {
          return current.kind == diagnostic.kind &&
              current.name == diagnostic.name &&
              current.message == diagnostic.message;
        });
    if (duplicate == runtimeStatus.diagnostics.end()) {
      constexpr std::size_t kMaximumDiagnostics = 256;
      if (runtimeStatus.diagnostics.size() == kMaximumDiagnostics) {
        runtimeStatus.diagnostics.erase(runtimeStatus.diagnostics.begin());
      }
      runtimeStatus.diagnostics.push_back(std::move(diagnostic));
    }
  }

  void recordCapabilityUsage(rns::RuntimeCapabilityUsage usage) {
    std::lock_guard lock(runtimeStatusMutex);
    const auto duplicate = std::find_if(
        runtimeStatus.capabilityUsages.begin(),
        runtimeStatus.capabilityUsages.end(),
        [&](const auto& current) {
          return current.type == usage.type && current.name == usage.name;
        });
    if (duplicate == runtimeStatus.capabilityUsages.end()) {
      runtimeStatus.capabilityUsages.push_back(std::move(usage));
    }
  }

  std::uint64_t enqueueRunningAction(rns::InteractionAction action) {
    std::lock_guard lock(runtimeStatusMutex);
    if (runtimeStatus.phase != rns::RuntimePhase::Running) {
      throw std::logic_error(
          "interaction actions require runtime phase Running");
    }
    return enqueueAction(std::move(action));
  }

  std::uint64_t enqueueAction(
      rns::InteractionAction action,
      std::shared_ptr<PendingActionBatch> batch = {},
      std::size_t batchIndex = 0) {
    std::lock_guard lock(actionMutex);
    const auto sequence = nextActionSequence++;
    const auto coalescable = !batch &&
        (action.type == rns::InteractionActionType::PointerMove ||
         action.type == rns::InteractionActionType::Scroll);
    if (coalescable && !actions.empty()) {
      auto& tail = actions.back();
      if (!tail.batch && tail.action.type == action.type &&
          (action.type != rns::InteractionActionType::PointerMove ||
           tail.action.pointerId == action.pointerId)) {
        if (action.type == rns::InteractionActionType::Scroll) {
          action.deltaX += tail.action.deltaX;
          action.deltaY += tail.action.deltaY;
        }
        tail.action = std::move(action);
        tail.sequences.push_back(sequence);
        return sequence;
      }
    }
    if (actions.size() >= 256) {
      throw std::overflow_error("interaction action queue is full");
    }
    actions.push_back({
        .action = std::move(action),
        .sequences = {sequence},
        .batch = std::move(batch),
        .batchIndex = batchIndex,
    });
    return sequence;
  }

  std::deque<QueuedInteractionAction> takeActions() {
    std::lock_guard lock(actionMutex);
    std::deque<QueuedInteractionAction> result;
    result.swap(actions);
    return result;
  }

  bool hasActions() {
    std::lock_guard lock(actionMutex);
    return !actions.empty();
  }

  void enqueueBatch(
      std::vector<rns::InteractionAction> batchActions,
      const std::shared_ptr<PendingActionBatch>& batch) {
    std::lock_guard lock(actionMutex);
    if (actions.size() + batchActions.size() > 256) {
      throw std::overflow_error("interaction action queue is full");
    }
    batch->remaining = batchActions.size();
    batch->results.resize(batchActions.size());
    for (std::size_t index = 0; index < batchActions.size(); ++index) {
      const auto sequence = nextActionSequence++;
      actions.push_back({
          .action = std::move(batchActions[index]),
          .sequences = {sequence},
          .batch = batch,
          .batchIndex = index,
      });
    }
  }

  void requestRunApplication(std::string appKey, std::string initialPropsJson) {
    std::lock_guard lock(applicationMutex);
    pendingApplication = PendingApplication{
        .appKey = std::move(appKey),
        .initialPropsJson = std::move(initialPropsJson),
    };
    launchState.pending = true;
    launchState.lastError.clear();
  }

  std::optional<PendingApplication> takeApplicationRequest() {
    std::lock_guard lock(applicationMutex);
    if (!pendingApplication) {
      return std::nullopt;
    }
    auto request = std::move(*pendingApplication);
    pendingApplication.reset();
    launchState.pending = true;
    return request;
  }

  bool hasApplicationRequest() {
    std::lock_guard lock(applicationMutex);
    return pendingApplication.has_value();
  }

  void refreshAppRegistry(jsi::Runtime& runtime);
  void applyHostApplication(
      jsi::Runtime& runtime,
      const std::string& appKey,
      const folly::dynamic& initialProps,
      std::string_view initialPropsJson);
};

namespace {
constexpr int kHostRootTag = 21;

folly::dynamic parseInitialPropsJson(std::string_view json) {
  size_t begin = 0;
  size_t end = json.size();
  while (begin < end &&
         std::isspace(static_cast<unsigned char>(json[begin]))) {
    ++begin;
  }
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(json[end - 1]))) {
    --end;
  }
  try {
    auto parsed = begin == end
        ? folly::dynamic::object()
        : folly::parseJson(std::string(json.substr(begin, end - begin)));
    if (!parsed.isObject()) {
      throw std::invalid_argument("initialProps must be a JSON object");
    }
    return parsed;
  } catch (const std::invalid_argument&) {
    throw;
  } catch (const std::exception& error) {
    throw std::invalid_argument(
        std::string("initialProps JSON: ") + error.what());
  }
}

std::vector<std::string> readRegisteredAppKeys(jsi::Runtime& runtime) {
  if (!runtime.global().hasProperty(runtime, "RN$AppRegistry")) {
    return {};
  }
  auto appRegistry =
      runtime.global().getPropertyAsObject(runtime, "RN$AppRegistry");
  if (!appRegistry.hasProperty(runtime, "getAppKeys") ||
      !appRegistry.getProperty(runtime, "getAppKeys").isObject() ||
      !appRegistry.getProperty(runtime, "getAppKeys")
           .getObject(runtime)
           .isFunction(runtime)) {
    return {};
  }
  auto getAppKeys = appRegistry.getPropertyAsFunction(runtime, "getAppKeys");
  auto keysValue = getAppKeys.callWithThis(runtime, appRegistry);
  if (!keysValue.isObject() || !keysValue.getObject(runtime).isArray(runtime)) {
    return {};
  }
  auto keys = keysValue.getObject(runtime).asArray(runtime);
  std::vector<std::string> result;
  const auto size = keys.size(runtime);
  result.reserve(size);
  for (size_t index = 0; index < size; ++index) {
    auto key = keys.getValueAtIndex(runtime, index);
    if (key.isString()) {
      result.push_back(key.getString(runtime).utf8(runtime));
    }
  }
  return result;
}

std::optional<std::string> resolveAutoRunAppKey(
    const std::vector<std::string>& keys,
    const std::optional<std::string>& configured) {
  if (configured) {
    const auto match = std::find(keys.begin(), keys.end(), *configured);
    return match == keys.end() ? std::nullopt
                               : std::optional<std::string>(*match);
  }
  std::vector<std::string> candidates;
  for (const auto& key : keys) {
    if (key != "LogBox") {
      candidates.push_back(key);
    }
  }
  if (candidates.size() != 1) {
    return std::nullopt;
  }
  return candidates.front();
}

void stopHostApplication(jsi::Runtime& runtime, int rootTag) {
  auto stop = runtime.global().getProperty(runtime, "RN$stopSurface");
  if (stop.isObject() && stop.getObject(runtime).isFunction(runtime)) {
    stop.getObject(runtime).getFunction(runtime).call(runtime, rootTag);
    return;
  }
  auto appRegistry = runtime.global().getProperty(runtime, "RN$AppRegistry");
  if (!appRegistry.isObject()) {
    return;
  }
  auto registryObject = appRegistry.getObject(runtime);
  if (!registryObject.hasProperty(
          runtime, "unmountApplicationComponentAtRootTag")) {
    return;
  }
  auto unmountValue = registryObject.getProperty(
      runtime, "unmountApplicationComponentAtRootTag");
  if (!unmountValue.isObject() ||
      !unmountValue.getObject(runtime).isFunction(runtime)) {
    return;
  }
  registryObject
      .getPropertyAsFunction(runtime, "unmountApplicationComponentAtRootTag")
      .callWithThis(runtime, registryObject, rootTag);
}

void startHostApplication(
    jsi::Runtime& runtime,
    const std::string& appKey,
    const folly::dynamic& initialProps) {
  if (!runtime.global().hasProperty(runtime, "RN$AppRegistry")) {
    throw std::runtime_error(
        "Bundle did not install global RN$AppRegistry");
  }
  auto appRegistry =
      runtime.global().getPropertyAsObject(runtime, "RN$AppRegistry");
  if (!appRegistry.hasProperty(runtime, "runApplication") ||
      !appRegistry.getProperty(runtime, "runApplication").isObject() ||
      !appRegistry.getProperty(runtime, "runApplication")
           .getObject(runtime)
           .isFunction(runtime)) {
    throw std::runtime_error(
        "RN$AppRegistry.runApplication is not available");
  }
  auto runApplication =
      appRegistry.getPropertyAsFunction(runtime, "runApplication");
  jsi::Object parameters(runtime);
  parameters.setProperty(runtime, "rootTag", kHostRootTag);
  parameters.setProperty(
      runtime, "initialProps", jsi::valueFromDynamic(runtime, initialProps));
  parameters.setProperty(runtime, "fabric", true);
  runApplication.callWithThis(
      runtime,
      appRegistry,
      jsi::String::createFromUtf8(runtime, appKey),
      parameters);
}

struct MetroDevServer {
  std::string host;
  uint16_t port{8081};
  std::string bundlePath;
  std::string platform{"android"};
};

std::optional<MetroDevServer> parseMetroDevServer(const std::string& url) {
  constexpr std::string_view scheme = "http://";
  if (!url.starts_with(scheme)) {
    return std::nullopt;
  }
  const auto authorityStart = scheme.size();
  const auto pathStart = url.find('/', authorityStart);
  if (pathStart == std::string::npos) {
    return std::nullopt;
  }
  const auto authority = url.substr(authorityStart, pathStart - authorityStart);
  if (authority.empty() || authority.find('@') != std::string::npos) {
    return std::nullopt;
  }
  const auto colon = authority.rfind(':');
  MetroDevServer metro;
  metro.host = colon == std::string::npos ? authority
                                          : authority.substr(0, colon);
  const auto portText = colon == std::string::npos
      ? std::string{"80"}
      : authority.substr(colon + 1);
  if ((metro.host != "localhost" && metro.host != "127.0.0.1") ||
      portText.empty()) {
    return std::nullopt;
  }
  try {
    const auto port = std::stoul(portText);
    if (port < 1 || port > 65535) {
      return std::nullopt;
    }
    metro.port = static_cast<uint16_t>(port);
  } catch (const std::exception&) {
    return std::nullopt;
  }
  const auto queryStart = url.find('?', pathStart);
  const auto path = queryStart == std::string::npos
      ? url.substr(pathStart)
      : url.substr(pathStart, queryStart - pathStart);
  metro.bundlePath =
      path.starts_with('/') ? path.substr(1) : path;
  if (metro.bundlePath.empty()) {
    return std::nullopt;
  }
  if (queryStart != std::string::npos) {
    const auto query = url.substr(queryStart + 1);
    const auto platformKey = std::string{"platform="};
    auto found = query.find(platformKey);
    while (found != std::string::npos) {
      if (found == 0 || query[found - 1] == '&') {
        auto value = query.substr(found + platformKey.size());
        const auto amp = value.find('&');
        if (amp != std::string::npos) {
          value = value.substr(0, amp);
        }
        if (value == "android" || value == "ios") {
          metro.platform = value;
        }
        break;
      }
      found = query.find(platformKey, found + 1);
    }
  }
  return metro;
}

void installGlobalEvalWithSourceUrl(jsi::Runtime& runtime) {
  runtime.global().setProperty(
      runtime,
      "globalEvalWithSourceUrl",
      jsi::Function::createFromHostFunction(
          runtime,
          jsi::PropNameID::forAscii(runtime, "globalEvalWithSourceUrl"),
          2,
          [](jsi::Runtime& rt,
             const jsi::Value&,
             const jsi::Value* args,
             size_t count) {
            if (count < 1 || !args[0].isString()) {
              throw jsi::JSError(
                  rt, "globalEvalWithSourceUrl requires a string");
            }
            auto code = args[0].getString(rt).utf8(rt);
            std::string sourceURL{"unknown"};
            if (count > 1 && args[1].isString()) {
              sourceURL = args[1].getString(rt).utf8(rt);
            }
            return rt.evaluateJavaScript(
                std::make_shared<jsi::StringBuffer>(std::move(code)),
                sourceURL);
          }));
}

void setupHMRClient(
    react::ReactInstance& instance,
    const MetroDevServer& metro) {
  instance.callFunctionOnModule(
      "HMRClient",
      "setup",
      folly::dynamic::array(
          metro.platform,
          metro.bundlePath,
          metro.host,
          static_cast<double>(metro.port),
          true));
}

} // namespace

void rns::Engine::Impl::refreshAppRegistry(jsi::Runtime& runtime) {
  if (!runtime.global().hasProperty(runtime, "RN$AppRegistry")) {
    return;
  }
  auto keys = readRegisteredAppKeys(runtime);
  std::lock_guard lock(applicationMutex);
  launchState.appKeys = std::move(keys);
  launchState.appRegistryReady = true;
}

void rns::Engine::Impl::applyHostApplication(
    jsi::Runtime& runtime,
    const std::string& appKey,
    const folly::dynamic& initialProps,
    std::string_view initialPropsJson) {
  refreshAppRegistry(runtime);
  std::optional<int> previousRootTag;
  {
    std::lock_guard lock(applicationMutex);
    previousRootTag = runningRootTag;
  }
  if (previousRootTag) {
    try {
      stopHostApplication(runtime, *previousRootTag);
    } catch (const std::exception&) {
    }
    std::lock_guard lock(applicationMutex);
    runningRootTag.reset();
    launchState.runningAppKey.reset();
  }
  startHostApplication(runtime, appKey, initialProps);
  std::lock_guard lock(applicationMutex);
  runningRootTag = kHostRootTag;
  launchState.runningAppKey = appKey;
  lastInitialPropsJson =
      initialPropsJson.empty() ? "{}" : std::string(initialPropsJson);
  launchState.lastError.clear();
  launchState.pending = false;
}

struct BundleRecord {
  std::string path;
  std::string hash;
  size_t bytes{0};
  double readMs{0};
  double evaluationMs{0};
  bool requestedByJS{false};
  bool loaded{false};
  std::string error;
  std::chrono::steady_clock::time_point readStart;
  std::chrono::steady_clock::time_point readEnd;
  std::chrono::steady_clock::time_point evaluationStart;
  std::chrono::steady_clock::time_point evaluationEnd;
};

struct BundleRequest {
  std::filesystem::path path;
  std::shared_ptr<react::Promise> promise;
};

static std::string hashBundle(const std::string& bundle) {
  uint64_t hash = 1469598103934665603ULL;
  for (const auto byte : bundle) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= 1099511628211ULL;
  }
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

struct WorkloadResult {
  double iterations{0};
  double renderIterations{0};
  double cpuIterations{0};
  double renderMs{0};
  double cpuMs{0};
  double checksum{0};
  bool timeoutFired{false};
  bool canceledTimerFired{true};
  double intervalCount{0};
  bool microtaskFired{false};
  double nativeModuleSum{0};
  std::string nativeModuleEcho;
  double customComponentExpectedValue{0};
  double customComponentEventValue{-1};
};

struct WorkloadMark {
  std::string name;
  std::chrono::steady_clock::time_point timestamp;
};

struct JsErrorRecord {
  std::string message;
  bool fatal{false};
  std::chrono::steady_clock::time_point timestamp;
  std::vector<react::JsErrorHandler::ProcessedError::StackFrame> stack;
};

static void writeTrace(
    const std::filesystem::path& path,
    std::chrono::steady_clock::time_point processStart,
    std::chrono::steady_clock::time_point initializationEnd,
    std::chrono::steady_clock::time_point bundleEnd,
    const std::vector<BundleRecord>& bundles,
    const std::vector<WorkloadMark>& marks,
    const std::vector<JsErrorRecord>& errors,
    const HeadlessReactFabricResult& fabric) {
  const auto micros = [processStart](auto value) {
    return std::chrono::duration<double, std::micro>(value - processStart)
        .count();
  };
  folly::dynamic events = folly::dynamic::array;
  const auto addSpan = [&](const std::string& name,
                           const std::string& category,
                           auto start,
                           auto end,
                           folly::dynamic args = folly::dynamic::object) {
    events.push_back(folly::dynamic::object
        ("name", name)
        ("cat", category)
        ("ph", "X")
        ("ts", micros(start))
        ("dur", std::max(0.0, micros(end) - micros(start)))
        ("pid", 1)
        ("tid", 1)
        ("args", std::move(args)));
  };
  addSpan("Runtime initialization", "runtime", processStart, initializationEnd);
  for (std::size_t index = 0; index < bundles.size(); ++index) {
    const auto& bundle = bundles[index];
    folly::dynamic args = folly::dynamic::object
        ("bundle", bundle.path)
        ("index", index)
        ("bytes", bundle.bytes)
        ("hash", bundle.hash);
    addSpan("Bundle read", "bundle", bundle.readStart, bundle.readEnd, args);
    if (bundle.evaluationStart != std::chrono::steady_clock::time_point{}) {
      addSpan(
          "Bundle evaluation",
          "bundle",
          bundle.evaluationStart,
          bundle.evaluationEnd,
          std::move(args));
    }
  }
  for (const auto& mark : marks) {
    events.push_back(folly::dynamic::object
        ("name", mark.name)
        ("cat", "workload")
        ("ph", "i")
        ("s", "t")
        ("ts", micros(mark.timestamp))
        ("pid", 1)
        ("tid", 1));
  }
  for (const auto& error : errors) {
    events.push_back(folly::dynamic::object
        ("name", error.message)
        ("cat", "javascript.error")
        ("ph", "i")
        ("s", "t")
        ("ts", micros(error.timestamp))
        ("pid", 1)
        ("tid", 1)
        ("args", folly::dynamic::object("fatal", error.fatal)));
  }
  for (const auto& span : fabric.traceSpans) {
    addSpan(
        span.name,
        "fabric",
        span.start,
        span.end,
        folly::dynamic::object("transaction", span.transaction));
  }
  addSpan("Measured run", "runtime", processStart, bundleEnd);
  folly::dynamic trace = folly::dynamic::object
      ("traceEvents", std::move(events))
      ("displayTimeUnit", "ms")
      ("metadata", folly::dynamic::object
          ("format", "react-native-simulator.chrome-trace")
          ("schemaVersion", 1));
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("Cannot open trace file: " + path.string());
  }
  output << folly::toJson(trace) << '\n';
}

struct RuntimeMetrics {
  int64_t heapAllocatedBytes{0};
  int64_t heapSizeBytes{0};
  int64_t heapExternalBytes{0};
  int64_t heapTotalAllocatedBytes{0};
  int64_t gcCollections{0};
  double gcTotalMs{0};
  double gcMaxPauseMs{0};
  double gcCpuMs{0};
  uint64_t residentBytes{0};
  uint64_t peakResidentBytes{0};
  double processUserCpuMs{0};
  double processSystemCpuMs{0};
};

static folly::dynamic serializeTransform(
    const ReactNativeSimulator::SceneNode& node) {
  if (!node.hasTransform) {
    return nullptr;
  }
  folly::dynamic matrix = folly::dynamic::array;
  for (const auto value : node.transformM) {
    matrix.push_back(value);
  }
  return folly::dynamic::object
      ("hasTransform", true)
      ("matrix", std::move(matrix))
      ("a", node.transformM[0])
      ("b", node.transformM[1])
      ("c", node.transformM[4])
      ("d", node.transformM[5])
      ("tx", node.transformM[12])
      ("ty", node.transformM[13])
      ("p0", node.transformM[3])
      ("p1", node.transformM[7])
      ("p2", node.transformM[15]);
}

static folly::dynamic serializeColor(
    bool has,
    float red,
    float green,
    float blue,
    float alpha) {
  if (!has) {
    return nullptr;
  }
  return folly::dynamic::object
      ("red", red)
      ("green", green)
      ("blue", blue)
      ("alpha", alpha);
}

static folly::dynamic serializeBorderColors(
    const ReactNativeSimulator::SceneNode& node) {
  if (!node.hasBorderTopColor && !node.hasBorderRightColor &&
      !node.hasBorderBottomColor && !node.hasBorderLeftColor) {
    return nullptr;
  }
  return folly::dynamic::object
      ("top",
       serializeColor(
           node.hasBorderTopColor,
           node.borderTopRed,
           node.borderTopGreen,
           node.borderTopBlue,
           node.borderTopAlpha))
      ("right",
       serializeColor(
           node.hasBorderRightColor,
           node.borderRightRed,
           node.borderRightGreen,
           node.borderRightBlue,
           node.borderRightAlpha))
      ("bottom",
       serializeColor(
           node.hasBorderBottomColor,
           node.borderBottomRed,
           node.borderBottomGreen,
           node.borderBottomBlue,
           node.borderBottomAlpha))
      ("left",
       serializeColor(
           node.hasBorderLeftColor,
           node.borderLeftRed,
           node.borderLeftGreen,
           node.borderLeftBlue,
           node.borderLeftAlpha));
}

static folly::dynamic serializeBorderStyles(
    const ReactNativeSimulator::SceneNode& node) {
  return folly::dynamic::object
      ("top", node.borderStyleTop)
      ("right", node.borderStyleRight)
      ("bottom", node.borderStyleBottom)
      ("left", node.borderStyleLeft);
}

static folly::dynamic serializeCornerRadius(float x, float y) {
  return folly::dynamic::object("x", x)("y", y);
}

static folly::dynamic serializeBorderRadii(
    const ReactNativeSimulator::SceneNode& node) {
  const auto axis = [](float x, float y, float circular, bool wantX) {
    if (x <= 0.0f && y <= 0.0f) {
      return circular;
    }
    return wantX ? x : y;
  };
  return folly::dynamic::object
      ("topLeft",
       serializeCornerRadius(
           axis(node.borderRadiusTopLeftX, node.borderRadiusTopLeftY,
                node.borderRadiusTopLeft, true),
           axis(node.borderRadiusTopLeftX, node.borderRadiusTopLeftY,
                node.borderRadiusTopLeft, false)))
      ("topRight",
       serializeCornerRadius(
           axis(node.borderRadiusTopRightX, node.borderRadiusTopRightY,
                node.borderRadiusTopRight, true),
           axis(node.borderRadiusTopRightX, node.borderRadiusTopRightY,
                node.borderRadiusTopRight, false)))
      ("bottomRight",
       serializeCornerRadius(
           axis(node.borderRadiusBottomRightX, node.borderRadiusBottomRightY,
                node.borderRadiusBottomRight, true),
           axis(node.borderRadiusBottomRightX, node.borderRadiusBottomRightY,
                node.borderRadiusBottomRight, false)))
      ("bottomLeft",
       serializeCornerRadius(
           axis(node.borderRadiusBottomLeftX, node.borderRadiusBottomLeftY,
                node.borderRadiusBottomLeft, true),
           axis(node.borderRadiusBottomLeftX, node.borderRadiusBottomLeftY,
                node.borderRadiusBottomLeft, false)));
}

static folly::dynamic serializeBoxShadowLayer(
    float offsetX,
    float offsetY,
    float blur,
    float spread,
    float red,
    float green,
    float blue,
    float alpha,
    bool inset) {
  return folly::dynamic::object
      ("offsetX", offsetX)
      ("offsetY", offsetY)
      ("blurRadius", blur)
      ("spreadDistance", spread)
      ("inset", inset)
      ("color", folly::dynamic::object
          ("red", red)
          ("green", green)
          ("blue", blue)
          ("alpha", alpha));
}

static folly::dynamic serializeBoxShadows(
    const ReactNativeSimulator::SceneNode& node) {
  folly::dynamic layers = folly::dynamic::array;
  if (!node.boxShadows.empty()) {
    for (const auto& shadow : node.boxShadows) {
      layers.push_back(serializeBoxShadowLayer(
          shadow.offsetX,
          shadow.offsetY,
          shadow.blur,
          shadow.spread,
          shadow.red,
          shadow.green,
          shadow.blue,
          shadow.alpha,
          shadow.inset));
    }
  } else if (node.hasBoxShadow) {
    layers.push_back(serializeBoxShadowLayer(
        node.boxShadowOffsetX,
        node.boxShadowOffsetY,
        node.boxShadowBlur,
        node.boxShadowSpread,
        node.boxShadowRed,
        node.boxShadowGreen,
        node.boxShadowBlue,
        node.boxShadowAlpha,
        node.boxShadowInset));
  }
  return layers;
}

static folly::dynamic serializePrimaryBoxShadow(
    const ReactNativeSimulator::SceneNode& node) {
  const auto layers = serializeBoxShadows(node);
  return layers.empty() ? folly::dynamic(nullptr) : layers[0];
}

static folly::dynamic serializeNativeRipple(
    const ReactNativeSimulator::SceneNode& node) {
  if (!node.nativeRipple) {
    return nullptr;
  }
  return folly::dynamic::object
      ("pressed", node.nativeRipplePressed)
      ("borderless", node.nativeRippleBorderless)
      ("color", folly::dynamic::object
          ("red", node.nativeRippleRed)
          ("green", node.nativeRippleGreen)
          ("blue", node.nativeRippleBlue)
          ("alpha", node.nativeRippleAlpha));
}

static folly::dynamic serializeHeadlessViewNodes(
    const std::vector<ReactNativeSimulator::SceneNode>& sourceNodes) {
  folly::dynamic nodes = folly::dynamic::array;
  for (const auto& node : sourceNodes) {
    folly::dynamic backgroundColor = nullptr;
    if (node.hasBackgroundColor) {
      backgroundColor = folly::dynamic::object
          ("red", node.backgroundRed)
          ("green", node.backgroundGreen)
          ("blue", node.backgroundBlue)
          ("alpha", node.backgroundAlpha);
    }
    folly::dynamic customProps = nullptr;
    if (node.customValue) {
      customProps = folly::dynamic::object
          ("value", *node.customValue)
          ("label", node.customLabel);
    }
    nodes.push_back(folly::dynamic::object
        ("tag", node.tag)
        ("parentTag", node.parentTag ? folly::dynamic(*node.parentTag)
                                     : folly::dynamic(nullptr))
        ("index", node.childIndex)
        ("depth", node.depth)
        ("componentName", node.componentName)
        ("nativeId", node.nativeId)
        ("layoutable", node.layoutable)
        ("layout", folly::dynamic::object
            ("x", node.x)
            ("y", node.y)
            ("width", node.width)
            ("height", node.height)
            ("absoluteX", node.absoluteX)
            ("absoluteY", node.absoluteY)
            ("contentInsets", folly::dynamic::object
                ("top", node.contentInsetTop)
                ("right", node.contentInsetRight)
                ("bottom", node.contentInsetBottom)
                ("left", node.contentInsetLeft))
            ("borderWidth", folly::dynamic::object
                ("top", node.borderTop)
                ("right", node.borderRight)
                ("bottom", node.borderBottom)
                ("left", node.borderLeft))
            ("display", node.display)
            ("position", node.position))
        ("props", folly::dynamic::object
            ("opacity", node.opacity)
            ("transform", serializeTransform(node))
            ("backgroundColor", std::move(backgroundColor))
            ("boxShadow", serializePrimaryBoxShadow(node))
            ("boxShadows", serializeBoxShadows(node))
            ("borderColor", serializeColor(
                node.hasBorderColor,
                node.borderRed,
                node.borderGreen,
                node.borderBlue,
                node.borderAlpha))
            ("borderColors", serializeBorderColors(node))
            ("borderStyles", serializeBorderStyles(node))
            ("outline", node.outlineWidth > 0 || node.hasOutlineColor
                ? folly::dynamic::object
                    ("width", node.outlineWidth)
                    ("offset", node.outlineOffset)
                    ("style", node.outlineStyle)
                    ("color", serializeColor(
                        node.hasOutlineColor,
                        node.outlineRed,
                        node.outlineGreen,
                        node.outlineBlue,
                        node.outlineAlpha))
                : folly::dynamic(nullptr))
            ("borderRadius", node.borderRadius)
            ("borderRadii", serializeBorderRadii(node))
            ("backfaceHidden", node.backfaceHidden)
            ("needsOffscreenAlphaCompositing",
             node.needsOffscreenAlphaCompositing)
            ("nativeRipple", serializeNativeRipple(node))
            ("zIndex", node.zIndex ? folly::dynamic(*node.zIndex)
                                    : folly::dynamic(nullptr))
            ("pointerEvents", node.pointerEvents)
            ("clipsContentToBounds", node.clipsContentToBounds)
            ("hitSlop", folly::dynamic::object
                ("top", node.hitSlopTop)
                ("right", node.hitSlopRight)
                ("bottom", node.hitSlopBottom)
                ("left", node.hitSlopLeft))
            ("collapsable", node.collapsable)
            ("activityIndicator", node.activityIndicator
                ? folly::dynamic::object
                    ("animating", node.activityIndicatorAnimating)
                    ("hidesWhenStopped", node.activityIndicatorHidesWhenStopped)
                    ("horizontal", node.activityIndicatorHorizontal)
                    ("progress", node.activityIndicatorProgress)
                    ("color", node.hasActivityIndicatorColor
                        ? folly::dynamic::object
                            ("red", node.activityIndicatorRed)
                            ("green", node.activityIndicatorGreen)
                            ("blue", node.activityIndicatorBlue)
                            ("alpha", node.activityIndicatorAlpha)
                        : folly::dynamic(nullptr))
                : folly::dynamic(nullptr))
            ("androidSwitch", node.androidSwitch
                ? folly::dynamic::object
                    ("on", node.androidSwitchOn)
                    ("enabled", node.androidSwitchEnabled)
                    ("thumbColor", node.hasSwitchThumbColor
                        ? folly::dynamic::object
                            ("red", node.switchThumbRed)
                            ("green", node.switchThumbGreen)
                            ("blue", node.switchThumbBlue)
                            ("alpha", node.switchThumbAlpha)
                        : folly::dynamic(nullptr))
                    ("trackColor", node.hasSwitchTrackColor
                        ? folly::dynamic::object
                            ("red", node.switchTrackRed)
                            ("green", node.switchTrackGreen)
                            ("blue", node.switchTrackBlue)
                            ("alpha", node.switchTrackAlpha)
                        : folly::dynamic(nullptr))
                : folly::dynamic(nullptr))
            ("modal", node.modalHost
                ? folly::dynamic::object
                    ("transparent", node.modalTransparent)
                : folly::dynamic(nullptr))
            ("custom", std::move(customProps))
            ("text", node.text)
            ("fontSize", node.fontSize)
            ("fontWeight", node.fontWeight)
            ("fontFamily", node.fontFamily)
            ("preparedText", node.preparedText != nullptr)
#if RNS_ENABLE_SKIA
            ("preparedTextMeasured", node.preparedText != nullptr &&
                node.preparedText->wasMeasured())
#else
            ("preparedTextMeasured", false)
#endif
            ("lineHeight", node.hasExplicitLineHeight
                ? folly::dynamic(node.lineHeight)
                : folly::dynamic(nullptr))
            ("includeFontPadding", node.includeFontPadding)
            ("textAlignVertical", node.textAlignVertical)
            ("subpixelText", node.subpixelText)
            ("textColor", node.hasTextColor
                ? folly::dynamic::object
                    ("red", node.textRed)
                    ("green", node.textGreen)
                    ("blue", node.textBlue)
                    ("alpha", node.textAlpha)
                : folly::dynamic(nullptr))
            ("textInput", node.textInput
                ? folly::dynamic::object
                    ("editable", node.editable)
                    ("multiline", node.multiline)
                    ("focused", node.focused)
                    ("placeholder", node.placeholder)
                    ("selectionStart", node.selectionStart)
                    ("selectionEnd", node.selectionEnd)
                : folly::dynamic(nullptr))
            ("scroll", node.scrollable
                ? folly::dynamic::object
                    ("offsetX", node.scrollOffsetX)
                    ("offsetY", node.scrollOffsetY)
                    ("contentWidth", node.scrollContentWidth)
                    ("contentHeight", node.scrollContentHeight)
                : folly::dynamic(nullptr))));
  }
  return nodes;
}

static folly::dynamic makeFabricTreeMetadata(
    const HeadlessReactFabricResult& fabric) {
  folly::dynamic shadowTree = folly::dynamic::object
      ("surfaceId", fabric.shadowTreeSurfaceId)
      ("revision", fabric.shadowTreeRevision)
      ("rootTag", fabric.shadowTreeRootTag)
      ("nodeCount", fabric.shadowTreeNodes.size())
      ("nodes", serializeHeadlessViewNodes(fabric.shadowTreeNodes));
  folly::dynamic errors = folly::dynamic::array;
  for (const auto& error : fabric.mountingErrors) {
    errors.push_back(error);
  }
  folly::dynamic mounted = folly::dynamic::object
      ("surfaceId", fabric.shadowTreeSurfaceId)
      ("revision", fabric.mountingRevision)
      ("rootTag", fabric.mountedRootTag)
      ("nodeCount", fabric.mountedViewNodes.size())
      ("errors", std::move(errors))
      ("nodes", serializeHeadlessViewNodes(fabric.mountedViewNodes));
  return folly::dynamic::object
      ("shadowTree", std::move(shadowTree))
      ("mountedViewTree", std::move(mounted));
}

static std::shared_ptr<const rns::SceneSnapshot> makeSceneSnapshot(
    const rns::EngineConfig& options,
    const HeadlessReactFabricResult& fabric,
    std::uint64_t runtimeGeneration) {
  auto scene = std::make_shared<rns::SceneSnapshot>();
  scene->runtimeGeneration = runtimeGeneration;
  scene->surfaceId = fabric.shadowTreeSurfaceId;
  scene->revision = fabric.mountingRevision;
  scene->rootTag = fabric.mountedRootTag;
  scene->viewportWidth = options.viewportWidth;
  scene->viewportHeight = options.viewportHeight;
  scene->pointScaleFactor = options.pointScaleFactor;
  scene->insetTop = options.insetTop;
  scene->insetBottom = options.insetBottom;
  scene->nodes = fabric.mountedViewNodes;
  scene->shadowRevision = fabric.shadowTreeRevision;
  scene->shadowRootTag = fabric.shadowTreeRootTag;
  scene->shadowNodes = fabric.shadowTreeNodes;
  scene->mountingErrors = fabric.mountingErrors;
  const auto& chrome = hostChrome();
  scene->statusBarHidden = chrome.statusBarHidden;
  scene->statusBarHeight = chrome.statusBarHeight;
  scene->statusBarRed = chrome.statusBarRed;
  scene->statusBarGreen = chrome.statusBarGreen;
  scene->statusBarBlue = chrome.statusBarBlue;
  scene->statusBarAlpha = chrome.statusBarAlpha;
  if (chrome.toastVisible()) {
    scene->toastMessage = chrome.toastMessage;
    scene->toastGravity = chrome.toastGravity;
    scene->toastOffsetX = chrome.toastOffsetX;
    scene->toastOffsetY = chrome.toastOffsetY;
    scene->toastUntilMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              chrome.toastUntil.time_since_epoch())
                              .count();
  }
  return scene;
}

static folly::dynamic makeSceneWireSnapshot(
    const rns::EngineConfig& options,
    const HeadlessReactFabricResult& fabric) {
  folly::dynamic errors = folly::dynamic::array;
  for (const auto& error : fabric.mountingErrors) {
    errors.push_back(error);
  }
  return folly::dynamic::object
      ("schemaVersion", 1)
      ("surfaceId", fabric.shadowTreeSurfaceId)
      ("revision", fabric.mountingRevision)
      ("rootTag", fabric.mountedRootTag)
      ("viewport", folly::dynamic::object
          ("width", options.viewportWidth)
          ("height", options.viewportHeight)
          ("pointScaleFactor", options.pointScaleFactor))
      ("mountingErrors", std::move(errors))
      ("nodes", serializeHeadlessViewNodes(fabric.mountedViewNodes));
}

static folly::dynamic makeLiveInspectorSnapshot(
    const rns::EngineConfig& options,
    const std::string& platform,
    const HeadlessReactFabricResult& fabric,
    const std::string& state,
    std::uint64_t sequence,
    bool schedulerInstalled) {
  auto trees = makeFabricTreeMetadata(fabric);
  return folly::dynamic::object
      ("host", "react-native-simulator")
      ("schemaVersion", 2)
      ("engine", "Hermes")
      ("reactNativeVersion", RNS_REACT_NATIVE_VERSION)
      ("hermesVersion", RNS_HERMES_VERSION)
      ("workload", options.workload)
      ("validationMode", "live")
      ("profile", options.profile)
      ("platformProfile", platform)
      ("runtimeState", state)
      ("sequence", sequence)
      ("viewport", folly::dynamic::object
          ("width", options.viewportWidth)
          ("height", options.viewportHeight)
          ("pointScaleFactor", options.pointScaleFactor))
      ("runtimeScheduler", schedulerInstalled)
      ("reactFabric", fabric.passed)
      ("reactFabricTransactions", fabric.transactions)
      ("reactFabricCreates", fabric.creates)
      ("reactFabricInserts", fabric.inserts)
      ("reactFabricUpdates", fabric.updates)
      ("reactFabricRemoves", fabric.removes)
      ("reactFabricDeletes", fabric.deletes)
      ("reactYoga", fabric.hasExpectedYogaWidths)
      ("eventDispatcher", fabric.eventDispatcherInstalled)
      ("bundleLoaded", true)
      ("workloadReady", true)
      ("workloadComplete", state == "complete")
      ("workloadTimedOut", false)
      ("pendingWork", false)
      ("jsErrors", 0)
      ("commitMs", fabric.commitMs)
      ("layoutMs", fabric.layoutMs)
      ("diffMs", fabric.diffMs)
      ("shadowTree", trees["shadowTree"])
      ("mountedViewTree", trees["mountedViewTree"]);
}

static std::string_view firstJsonObject(std::string_view text) {
  const auto start = text.find('{');
  if (start == std::string_view::npos) {
    return {};
  }
  std::size_t depth = 0;
  bool inString = false;
  bool escaped = false;
  for (std::size_t index = start; index < text.size(); ++index) {
    const char value = text[index];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (value == '\\') {
        escaped = true;
      } else if (value == '"') {
        inString = false;
      }
      continue;
    }
    if (value == '"') {
      inString = true;
    } else if (value == '{') {
      ++depth;
    } else if (value == '}' && --depth == 0) {
      return text.substr(start, index - start + 1);
    }
  }
  return {};
}

static rns::InteractionAction parseInteractionAction(
    const folly::dynamic& value,
    std::size_t index) {
  const auto fail = [index](const std::string& message) -> void {
    throw std::invalid_argument(
        "interaction action[" + std::to_string(index) + "]: " + message);
  };
  if (!value.isObject()) {
    fail("must be an object");
  }
  static const std::unordered_set<std::string> allowedFields{
      "type", "x", "y", "deltaX", "deltaY", "pointerId", "button",
      "buttons", "ctrlKey", "shiftKey", "altKey", "metaKey", "text",
      "key"};
  for (const auto& [key, _] : value.items()) {
    if (!key.isString() || !allowedFields.contains(key.asString())) {
      fail("contains unsupported field " +
           (key.isString() ? key.asString() : std::string("<non-string>")));
    }
  }
  const auto* typeValue = value.get_ptr("type");
  if (typeValue == nullptr || !typeValue->isString()) {
    fail("type must be a string");
  }
  rns::InteractionAction action;
  const auto type = typeValue->asString();
  if (type == "pointerDown") {
    action.type = rns::InteractionActionType::PointerDown;
  } else if (type == "pointerMove") {
    action.type = rns::InteractionActionType::PointerMove;
  } else if (type == "pointerUp") {
    action.type = rns::InteractionActionType::PointerUp;
  } else if (type == "pointerCancel") {
    action.type = rns::InteractionActionType::PointerCancel;
  } else if (type == "scroll") {
    action.type = rns::InteractionActionType::Scroll;
  } else if (type == "textInput") {
    action.type = rns::InteractionActionType::TextInput;
  } else if (type == "keyDown") {
    action.type = rns::InteractionActionType::KeyDown;
  } else if (type == "hardwareBackPress") {
    action.type = rns::InteractionActionType::HardwareBackPress;
  } else {
    fail("unsupported type " + type);
  }
  const auto number = [&](const char* name, double fallback) {
    const auto* field = value.get_ptr(name);
    if (field == nullptr) {
      return fallback;
    }
    if (!field->isNumber() || !std::isfinite(field->asDouble())) {
      fail(std::string(name) + " must be a finite number");
    }
    return field->asDouble();
  };
  const auto boolean = [&](const char* name) {
    const auto* field = value.get_ptr(name);
    if (field == nullptr) {
      return false;
    }
    if (!field->isBool()) {
      fail(std::string(name) + " must be a boolean");
    }
    return field->asBool();
  };
  action.x = static_cast<float>(number("x", 0));
  action.y = static_cast<float>(number("y", 0));
  action.deltaX = static_cast<float>(number("deltaX", 0));
  action.deltaY = static_cast<float>(number("deltaY", 0));
  action.pointerId = static_cast<int>(number("pointerId", 1));
  action.button = static_cast<int>(number("button", 0));
  action.buttons = static_cast<int>(number("buttons", 0));
  action.ctrlKey = boolean("ctrlKey");
  action.shiftKey = boolean("shiftKey");
  action.altKey = boolean("altKey");
  action.metaKey = boolean("metaKey");
  const auto coordinateAction =
      action.type == rns::InteractionActionType::PointerDown ||
      action.type == rns::InteractionActionType::PointerMove ||
      action.type == rns::InteractionActionType::PointerUp ||
      action.type == rns::InteractionActionType::PointerCancel ||
      action.type == rns::InteractionActionType::Scroll;
  if (coordinateAction &&
      (value.get_ptr("x") == nullptr || value.get_ptr("y") == nullptr)) {
    fail("pointer and scroll actions require x and y");
  }
  if (action.pointerId < 0 || action.button < 0 || action.button > 4 ||
      action.buttons < 0) {
    fail("pointerId, button, or buttons is outside the supported range");
  }
  if (const auto* text = value.get_ptr("text")) {
    if (!text->isString()) {
      fail("text must be a string");
    }
    action.text = text->asString();
  }
  if (const auto* key = value.get_ptr("key")) {
    if (!key->isString()) {
      fail("key must be a string");
    }
    action.key = key->asString();
  }
  if (action.type == rns::InteractionActionType::TextInput &&
      action.text.empty()) {
    fail("textInput requires non-empty text");
  }
  if (action.type == rns::InteractionActionType::KeyDown &&
      action.key != "Backspace" && action.key != "Delete" &&
      action.key != "ArrowLeft" && action.key != "ArrowRight" &&
      action.key != "Enter") {
    fail("unsupported key " + action.key);
  }
  return action;
}

static RuntimeMetrics readRuntimeMetrics(jsi::Runtime& runtime) {
  RuntimeMetrics result;
  const auto heap = runtime.instrumentation().getHeapInfo(false);
  const auto heapValue = [&](const char* name) -> int64_t {
    const auto found = heap.find(name);
    return found == heap.end() ? 0 : found->second;
  };
  result.heapAllocatedBytes = heapValue("hermes_allocatedBytes");
  result.heapSizeBytes = heapValue("hermes_heapSize");
  result.heapExternalBytes = heapValue("hermes_externalBytes");
  result.heapTotalAllocatedBytes = heapValue("hermes_totalAllocatedBytes");
  result.gcCollections = heapValue("hermes_numCollections");

  const auto gcStats = runtime.instrumentation().getRecordedGCStats();
  const auto gcStatsJson = firstJsonObject(gcStats);
  if (!gcStatsJson.empty()) {
    const auto parsed = folly::parseJson(gcStatsJson);
    if (const auto* general = parsed.get_ptr("general")) {
      const auto secondsAsMs = [&](const char* name) {
        const auto* value = general->get_ptr(name);
        return value != nullptr && value->isNumber()
            ? value->asDouble() * 1000.0
            : 0.0;
      };
      result.gcTotalMs = secondsAsMs("totalGCTime");
      result.gcMaxPauseMs = secondsAsMs("maxGCPause");
      result.gcCpuMs = secondsAsMs("totalGCCPUTime");
    }
  }

  mach_task_basic_info_data_t taskInfo{};
  mach_msg_type_number_t taskInfoCount = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(
          mach_task_self(),
          MACH_TASK_BASIC_INFO,
          reinterpret_cast<task_info_t>(&taskInfo),
          &taskInfoCount) == KERN_SUCCESS) {
    result.residentBytes = taskInfo.resident_size;
  }
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    result.peakResidentBytes = static_cast<uint64_t>(usage.ru_maxrss);
    result.processUserCpuMs =
        usage.ru_utime.tv_sec * 1000.0 + usage.ru_utime.tv_usec / 1000.0;
    result.processSystemCpuMs =
        usage.ru_stime.tv_sec * 1000.0 + usage.ru_stime.tv_usec / 1000.0;
  }
  return result;
}

static RuntimeMetrics subtractRuntimeMetrics(
    const RuntimeMetrics& after,
    const RuntimeMetrics& before) {
  RuntimeMetrics result;
  result.heapAllocatedBytes =
      after.heapAllocatedBytes - before.heapAllocatedBytes;
  result.heapSizeBytes = after.heapSizeBytes - before.heapSizeBytes;
  result.heapExternalBytes =
      after.heapExternalBytes - before.heapExternalBytes;
  result.heapTotalAllocatedBytes =
      after.heapTotalAllocatedBytes - before.heapTotalAllocatedBytes;
  result.gcCollections = after.gcCollections - before.gcCollections;
  result.gcTotalMs = after.gcTotalMs - before.gcTotalMs;
  // Hermes exposes only the process-wide maximum pause. A workload-local
  // maximum cannot be derived from two cumulative snapshots.
  result.gcMaxPauseMs = 0;
  result.gcCpuMs = after.gcCpuMs - before.gcCpuMs;
  result.residentBytes = after.residentBytes >= before.residentBytes
      ? after.residentBytes - before.residentBytes
      : 0;
  result.peakResidentBytes = after.peakResidentBytes >= before.peakResidentBytes
      ? after.peakResidentBytes - before.peakResidentBytes
      : 0;
  result.processUserCpuMs =
      after.processUserCpuMs - before.processUserCpuMs;
  result.processSystemCpuMs =
      after.processSystemCpuMs - before.processSystemCpuMs;
  return result;
}

static WorkloadResult readWorkloadResult(jsi::Runtime& runtime) {
  WorkloadResult result;
  auto value = runtime.global().getProperty(runtime, "RN$SimulatorWorkloadResult");
  if (!value.isObject()) {
    return result;
  }
  auto object = value.getObject(runtime);
  const auto number = [&](const char* name) {
    auto property = object.getProperty(runtime, name);
    return property.isNumber() ? property.getNumber() : 0.0;
  };
  const auto boolean = [&](const char* name) {
    auto property = object.getProperty(runtime, name);
    return property.isBool() && property.getBool();
  };
  const auto string = [&](const char* name) {
    auto property = object.getProperty(runtime, name);
    return property.isString()
        ? property.getString(runtime).utf8(runtime)
        : std::string{};
  };
  result.iterations = number("iterations");
  result.renderIterations = number("renderIterations");
  result.cpuIterations = number("cpuIterations");
  result.renderMs = number("renderMs");
  result.cpuMs = number("cpuMs");
  result.checksum = number("checksum");
  result.timeoutFired = boolean("timeoutFired");
  result.canceledTimerFired = boolean("canceledTimerFired");
  result.intervalCount = number("intervalCount");
  result.microtaskFired = boolean("microtaskFired");
  result.nativeModuleSum = number("nativeModuleSum");
  result.nativeModuleEcho = string("nativeModuleEcho");
  result.customComponentExpectedValue =
      number("customComponentExpectedValue");
  result.customComponentEventValue = number("customComponentEventValue");
  return result;
}

static std::string readFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("Cannot open bundle: " + path.string());
  }
  return {std::istreambuf_iterator<char>(stream), {}};
}

static std::string valueToString(jsi::Runtime& runtime, const jsi::Value& value) {
  if (value.isString()) {
    return value.getString(runtime).utf8(runtime);
  }
  auto stringFunction = runtime.global().getPropertyAsFunction(runtime, "String");
  return stringFunction.call(runtime, value).getString(runtime).utf8(runtime);
}

static void installConsole(
    jsi::Runtime& runtime,
    const std::string& profile) {
  jsi::Object console(runtime);
  for (const auto* method : {"debug", "log", "info", "warn", "error"}) {
    auto output = jsi::Function::createFromHostFunction(
        runtime,
        jsi::PropNameID::forAscii(runtime, method),
        1,
        [method, profile](
            jsi::Runtime& runtime,
            const jsi::Value&,
            const jsi::Value* args,
            size_t count) -> jsi::Value {
          std::string message;
          for (size_t index = 0; index < count; ++index) {
            if (index > 0) {
              message.push_back(' ');
              std::cout << ' ';
            }
            const auto text = valueToString(runtime, args[index]);
            message.append(text);
            std::cout << text;
          }
          std::cout << '\n';
          if (std::string_view(method) == "error" &&
              message.find("React Native version mismatch") !=
                  std::string::npos) {
            std::cerr
                << "rnsim: JS bundle RN version does not match profile "
                << profile << " (native " RNS_REACT_NATIVE_VERSION ").\n";
            if (profile.find("rn87") != std::string::npos) {
              std::cerr << "If this Metro bundle is RN 0.73, restart with "
                           "--profile android-rn73.\n";
            }
          }
          return jsi::Value::undefined();
        });
    console.setProperty(runtime, method, std::move(output));
  }
  runtime.global().setProperty(runtime, "console", std::move(console));
}

rns::Engine::Engine(rns::EngineConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {
  static std::once_flag loggingInitialized;
  std::call_once(loggingInitialized, [] {
    google::InitGoogleLogging("react-native-simulator");
    FLAGS_minloglevel = 2;
  });
}

rns::Engine::~Engine() = default;
rns::Engine::Engine(Engine&&) noexcept = default;
rns::Engine& rns::Engine::operator=(
    Engine&&) noexcept = default;

void rns::Engine::addAddon(std::string addon) {
  if (impl_->ran) {
    throw std::logic_error("cannot add an addon after the runtime has run");
  }
  impl_->addons.push_back(std::move(addon));
}

void rns::Engine::loadBundle(const std::filesystem::path& path) {
  if (impl_->ran) {
    throw std::logic_error("cannot queue a bundle after the runtime has run");
  }
  std::error_code error;
  auto resolved = std::filesystem::absolute(path, error);
  if (error) {
    resolved = path;
  } else if (const auto canonical =
                 std::filesystem::weakly_canonical(resolved, error);
             !error) {
    resolved = canonical;
  }
  impl_->bundles.push_back({
      .sourceUrl = std::string("file://") + resolved.generic_string(),
      .path = resolved,
  });
  const auto assets = resolved.parent_path() / "assets";
  if (!impl_->config.assetDirectory &&
      std::filesystem::is_directory(assets)) {
    impl_->config.assetDirectory = std::filesystem::weakly_canonical(assets);
  }
}

void rns::Engine::loadBundle(
    std::string bytes,
    std::string sourceUrl) {
  if (impl_->ran) {
    throw std::logic_error("cannot queue a bundle after the runtime has run");
  }
  if (sourceUrl.empty()) {
    throw std::invalid_argument("bundle sourceUrl must not be empty");
  }
  const bool http = sourceUrl.starts_with("http://");
  impl_->bundles.push_back({
      .sourceUrl = std::move(sourceUrl),
      .bytes = std::move(bytes),
      .http = http,
  });
}

void rns::Engine::setSceneUpdateCallback(
    std::function<void(std::shared_ptr<const SceneSnapshot>)> callback) {
  if (impl_->ran) {
    throw std::logic_error("cannot change callbacks after the runtime has run");
  }
  impl_->config.onSceneUpdate = std::move(callback);
}

void rns::Engine::setActionResultCallback(
    std::function<void(const InteractionResult&)> callback) {
  if (impl_->ran) {
    throw std::logic_error("cannot change callbacks after the runtime has run");
  }
  impl_->config.onActionResult = std::move(callback);
}

std::uint64_t rns::Engine::enqueueAction(rns::InteractionAction action) {
  if (!impl_->running || impl_->finished) {
    throw std::logic_error("interaction actions require a running Engine");
  }
  return impl_->enqueueRunningAction(std::move(action));
}

rns::ApplicationLaunchState rns::Engine::applicationLaunchState() const {
  std::lock_guard lock(impl_->applicationMutex);
  return impl_->launchState;
}

rns::RuntimeStatus rns::Engine::runtimeStatus() const {
  std::lock_guard lock(impl_->runtimeStatusMutex);
  return impl_->runtimeStatus;
}

void rns::Engine::runApplication(
    std::string appKey,
    std::string initialPropsJson) {
  if (!impl_->running || impl_->finished) {
    throw std::logic_error("runApplication requires a running Engine");
  }
  if (appKey.empty()) {
    throw std::invalid_argument("appKey must not be empty");
  }
  parseInitialPropsJson(initialPropsJson);
  impl_->beginApplicationLaunch();
  impl_->requestRunApplication(std::move(appKey), std::move(initialPropsJson));
}

void rns::Engine::requestStop() noexcept {
  impl_->stopRequested.store(true);
}

void rns::Engine::requestReload() noexcept {
  if (impl_->config.mode != rns::SimulatorMode::Interactive) {
    return;
  }
  impl_->reloadRequested.store(true);
}

rns::EngineResult rns::Engine::run() {
  if (impl_->ran.exchange(true)) {
    return {.exitCode = 1, .error = "Engine can only run once"};
  }
  impl_->running.store(true);
  impl_->setRuntimePhase(rns::RuntimePhase::Initializing);
  struct RunState final {
    rns::Engine::Impl& impl;
    ~RunState() {
      setDevSettingsReloadHandler(nullptr);
      impl.setRuntimePhase(rns::RuntimePhase::Stopped);
      impl.running.store(false);
      impl.finished.store(true);
    }
  } runState{*impl_};
  if (impl_->bundles.empty()) {
    return {.exitCode = 1, .error = "at least one bundle is required"};
  }
#if RNS_ENABLE_SKIA
  if (!impl_->config.fontDirectory &&
      impl_->config.profile.rfind("android", 0) == 0) {
    const auto androidFonts =
        std::filesystem::current_path() / "build" / "android-fonts";
    std::error_code error;
    if (std::filesystem::is_directory(androidFonts, error) && !error) {
      try {
        rns::validateSkiaFontDirectory(androidFonts);
        impl_->config.fontDirectory = androidFonts;
      } catch (const std::exception&) {
      }
    }
  }
#endif
  {
    auto& config = impl_->config;
    // Pixel 4a window is 1080x2072 @ 2.75. RN 0.87 StatusBar.HEIGHT is the
    // WindowInsets top of statusBars|displayCutout (140px), not generic 24dp.
    if (std::abs(config.pointScaleFactor - 2.75f) <= 0.02f &&
        std::abs(config.viewportWidth * config.pointScaleFactor - 1080.0f) <=
            2.0f &&
        std::abs(config.viewportHeight * config.pointScaleFactor - 2072.0f) <=
            2.0f &&
        config.insetBottom == 0.0f) {
      config.insetTop = 140.0f / 2.75f;
      config.insetBottom = 128.0f / 2.75f;
    }
  }
  const auto& options = impl_->config;
  hostChrome().statusBarHeight = options.insetTop;
  headlessBackPress().invokeDefault =
      [impl = impl_.get()](jsi::Runtime& runtime) {
        std::optional<int> rootTag;
        {
          std::lock_guard lock(impl->applicationMutex);
          rootTag = impl->runningRootTag;
        }
        if (!rootTag) {
          return;
        }
        try {
          stopHostApplication(runtime, *rootTag);
        } catch (const std::exception&) {
        }
        std::lock_guard lock(impl->applicationMutex);
        impl->runningRootTag.reset();
        impl->launchState.runningAppKey.reset();
      };
  std::shared_ptr<InspectorTransport> inspectorTransport;
  const bool developmentMode =
      options.mode == rns::SimulatorMode::Interactive;
  const bool conformanceMode =
      options.mode == rns::SimulatorMode::Conformance;
  try {
    {
      static std::once_flag featureFlagsOnce;
      std::call_once(featureFlagsOnce, [] {
        react::ReactNativeFeatureFlags::override(
            std::make_unique<react::ReactNativeFeatureFlagsDynamicProvider>(
                folly::dynamic::object
                    ("cxxNativeAnimatedEnabled", true)
                    ("enableLayoutAnimationsOnAndroid", true)
                    ("useSharedAnimatedBackend", true)));
      });
    }
#if RNS_ENABLE_SKIA
    if (options.fontDirectory) {
      rns::validateSkiaFontDirectory(*options.fontDirectory);
    }
#endif
    // Verify the native Fabric/Yoga implementation outside every measured
    // runtime phase. Its allocations and CPU must not be attributed to the
    // caller's bundle or workload.
    const auto fabric = runHeadlessFabricPipeline();
    const auto processStart = std::chrono::steady_clock::now();
    if (options.inspectorSocket) {
      inspectorTransport =
          InspectorTransport::connect(*options.inspectorSocket);
      inspectorTransport->sendJson(folly::toJson(folly::dynamic::object
          ("type", "status")
          ("state", "starting")
          ("pid", static_cast<int>(::getpid()))));
    }

    std::shared_ptr<DevToolsHost> devTools;
    if (options.devTools.enabled) {
      devTools = DevToolsHost::start(options.devTools);
    }
    if (devTools) {
      devTools->setOnReload([impl = impl_.get(), developmentMode]() {
        if (developmentMode) {
          impl->reloadRequested.store(true);
        } else {
          std::cerr
              << "React Native DevTools requested reload; headless sessions "
                 "require a new process\n";
        }
      });
    }
    rns::SimulatorAddonRegistry addonRegistry;
    for (const auto& addonPath : impl_->addons) {
      addonRegistry.load(
          addonPath, RNS_REACT_NATIVE_VERSION, RNS_HERMES_VERSION);
    }
    if (developmentMode) {
      setDevSettingsReloadHandler([impl = impl_.get()]() {
        impl->reloadRequested.store(true);
      });
    }

    std::optional<std::string> rerunAppKey;
    std::string rerunInitialPropsJson = options.initialPropsJson;
    int sessionGeneration = 0;
    for (;;) {
      ++sessionGeneration;
      impl_->reloadRequested.store(false);
      impl_->beginRuntimeGeneration(
          static_cast<std::uint64_t>(sessionGeneration),
          sessionGeneration > 1 ? rns::RuntimePhase::Reloading
                                : rns::RuntimePhase::Initializing);
      {
        std::lock_guard lock(impl_->applicationMutex);
        impl_->launchState.runtimeGeneration =
            static_cast<std::uint64_t>(sessionGeneration);
      }
      if (sessionGeneration > 1) {
        std::cerr << "reloading ReactInstance (generation "
                  << sessionGeneration << ")\n";
        std::string reloadFetchError;
        for (auto& bundle : impl_->bundles) {
          if (!bundle.http) {
            continue;
          }
          try {
            bundle.bytes = fetchHttpBundle(
                bundle.sourceUrl,
                60000,
                [impl = impl_.get()] { return impl->stopRequested.load(); });
          } catch (const HttpRequestCancelled&) {
            if (impl_->stopRequested.load()) {
              return {.exitCode = 0};
            }
            throw;
          } catch (const std::exception& error) {
            reloadFetchError = "Cannot reload bundle from " +
                bundle.sourceUrl + ": " + error.what();
            break;
          }
        }
        if (!reloadFetchError.empty()) {
          {
            std::lock_guard lock(impl_->applicationMutex);
            impl_->launchState.lastError = reloadFetchError;
            impl_->launchState.pending = false;
          }
          impl_->recordRuntimeDiagnostic({
              .kind = rns::RuntimeDiagnosticKind::ApplicationError,
              .message = reloadFetchError,
          });
          impl_->setRuntimePhase(rns::RuntimePhase::PausedAfterError);
          std::cerr << reloadFetchError << '\n'
                    << "fix Metro, then use Reload to retry\n";
          while (!impl_->stopRequested.load() &&
                 !impl_->reloadRequested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
          }
          if (impl_->stopRequested.load()) {
            return {.exitCode = 1, .error = reloadFetchError};
          }
          continue;
        }
        {
          std::lock_guard lock(impl_->applicationMutex);
          impl_->launchState.initialBundlesLoaded = false;
          impl_->launchState.appRegistryReady = false;
          impl_->launchState.pending = false;
          impl_->launchState.appKeys.clear();
          impl_->launchState.lastError.clear();
          impl_->launchState.runningAppKey.reset();
          impl_->runningRootTag.reset();
          impl_->pendingApplication.reset();
        }
        (void)impl_->takeActions();
      }

    auto eventLoop = std::make_shared<SimulatorEventLoop>();
    auto timerRegistry = eventLoop->createTimerRegistry();
    auto* timerRegistryRaw = timerRegistry.get();
    auto timerManager =
        std::make_shared<react::TimerManager>(std::move(timerRegistry));
    timerRegistryRaw->setTimerManager(timerManager);

    auto hermesRuntime = facebook::hermes::makeHermesRuntime(
        ::hermes::vm::RuntimeConfig::Builder()
            .withMicrotaskQueue(true)
            .withGCConfig(
                ::hermes::vm::GCConfig::Builder()
                    .withShouldRecordStats(true)
                    .build())
            .build());
    auto* runtime = hermesRuntime.get();
    installConsole(*runtime, options.profile);
    std::size_t jsErrorCount = 0;
    std::vector<JsErrorRecord> jsErrors;
    bool workloadReady = false;
    bool workloadComplete = false;
    std::vector<double> workloadRootTags;
    std::vector<WorkloadMark> workloadMarks;
    std::deque<BundleRequest> requestedBundles;
    std::vector<BundleRecord> bundleRecords;
    {
      auto& environment = hostEnvironment();
      environment.reset();
      environment.setViewport(
          options.viewportWidth,
          options.viewportHeight,
          options.pointScaleFactor,
          options.insetTop,
          options.insetBottom);
      if (options.colorScheme) {
        environment.setColorScheme(*options.colorScheme);
      }
      if (options.appState) {
        environment.setAppState(*options.appState);
      }
      if (options.reduceMotion) {
        environment.setReduceMotion(*options.reduceMotion);
      }
      if (options.invertColors) {
        environment.setInvertColors(*options.invertColors);
      }
      if (options.highTextContrast) {
        environment.setHighTextContrast(*options.highTextContrast);
      }
      if (options.screenReader) {
        environment.setScreenReader(*options.screenReader);
      }
      if (options.accessibilityService) {
        environment.setAccessibilityService(*options.accessibilityService);
      }
      if (options.grayscale) {
        environment.setGrayscale(*options.grayscale);
      }
      if (options.boldText) {
        environment.setBoldText(*options.boldText);
      }
      if (options.reduceTransparency) {
        environment.setReduceTransparency(*options.reduceTransparency);
      }
      if (options.darkerSystemColors) {
        environment.setDarkerSystemColors(*options.darkerSystemColors);
      }
      if (options.orientation) {
        environment.setOrientation(*options.orientation);
      }
    }
    HeadlessRNModuleHost moduleHost{
        .viewportWidth = options.viewportWidth,
        .viewportHeight = options.viewportHeight,
        .pointScaleFactor = options.pointScaleFactor,
        .insetTop = options.insetTop,
        .insetBottom = options.insetBottom,
        .scriptURL = impl_->bundles.empty()
            ? std::string("react-native-simulator://bundle")
            : impl_->bundles.front().sourceUrl,
        .assetDirectory = options.assetDirectory.value_or(std::filesystem::path{}),
    };
    auto runtimeProfile = createRuntimeProfile(options.profile, moduleHost);
    std::unordered_map<std::string, std::string> moduleFidelities;
    for (const auto& capability : runtimeProfile->moduleCapabilities()) {
      moduleFidelities.emplace(capability.name, capability.fidelity);
    }
    for (const auto& capability : addonRegistry.moduleCapabilities()) {
      moduleFidelities[capability.name] = capability.fidelity;
    }
    std::unordered_map<std::string, std::string> componentFidelities{
        {"Root", "real-fabric-root"},
        {"RootView", "real-fabric-root"},
        {"View", "real-fabric-yoga"},
        {"RawText", "real-fabric-virtual-text"},
        {"Text", "real-fabric-virtual-text"},
#if RNS_ENABLE_SKIA
        {"Paragraph", "skia-prepared-text"},
#else
        {"Paragraph", "unavailable-without-skia"},
#endif
        {"ScrollView", "headless-viewport-state"},
#if RNS_ENABLE_SKIA
        {"Image", "skia-local-and-http-image"},
#else
        {"Image", "unavailable-without-skia"},
#endif
    };
    for (const auto& spec : react::kHeadlessOfficialComponents) {
      componentFidelities[spec.name] =
          componentFidelityForBuild(spec.fidelity);
    }
    componentFidelities[
        runtimeProfile->platform() == "ios" ? "TextInput"
                                             : "AndroidTextInput"] =
        "rn-fabric-headless-platform-adapter";
    for (const auto& capability : addonRegistry.componentCapabilities()) {
      componentFidelities[capability.name] = capability.fidelity;
    }
    auto instance = std::make_unique<react::ReactInstance>(
        std::make_unique<SimulatorHermesRuntime>(std::move(hermesRuntime)),
        eventLoop,
        timerManager,
        [impl = impl_.get(), &jsErrorCount, &jsErrors](
            jsi::Runtime&,
            const react::JsErrorHandler::ProcessedError& error) {
            ++jsErrorCount;
            jsErrors.push_back({
                .message = error.message,
                .fatal = error.isFatal,
                .timestamp = std::chrono::steady_clock::now(),
                .stack = error.stack,
            });
            {
              std::lock_guard lock(impl->applicationMutex);
              impl->launchState.lastError = error.message;
              impl->launchState.pending = false;
            }
            rns::RuntimeDiagnostic diagnostic{
                .kind = rns::RuntimeDiagnosticKind::JavaScriptError,
                .message = error.message,
                .fatal = error.isFatal,
            };
            diagnostic.stack.reserve(error.stack.size());
            for (const auto& frame : error.stack) {
              diagnostic.stack.push_back({
                  .file = frame.file,
                  .method = frame.methodName,
                  .line = frame.lineNumber,
                  .column = frame.column,
              });
            }
            impl->recordRuntimeDiagnostic(std::move(diagnostic));
            impl->setRuntimePhase(rns::RuntimePhase::PausedAfterError);
            std::cerr << "RN JS error: " << error.message << '\n';
            if (error.message.find(
                    "TurboModuleRegistry.getEnforcing") !=
                    std::string::npos &&
                error.message.find("could not be found") !=
                    std::string::npos) {
              std::cerr
                  << "rnsim: a required native module is missing. "
                     "RN 0.73 Metro bundles need --profile android-rn73; "
                     "application and third-party modules need an explicit "
                     "--addon path/to/addon.dylib.\n";
            }
        },
        devTools ? devTools->target() : nullptr);
    // Destroy module JS representations before ReactInstance tears down the
    // runtime they reference (locals are destroyed in reverse order).
    std::unordered_map<std::string, std::shared_ptr<react::TurboModule>>
        turboModuleCache;
    timerManager->setRuntimeExecutor(instance->getBufferedRuntimeExecutor());
    eventLoop->drainUntilIdle();

    const auto runtimeExecutor = instance->getUnbufferedRuntimeExecutor();
    eventLoop->setRuntimeExecutor(runtimeExecutor);
    headlessBackPress().runOnJs =
        [runtimeExecutor](std::function<void(jsi::Runtime&)> work) {
          runtimeExecutor(std::move(work));
        };
    const auto jsInvoker =
        std::make_shared<react::RuntimeSchedulerCallInvoker>(
            instance->getRuntimeScheduler());
    std::shared_ptr<HeadlessReactFabricHost> reactFabricHost;
    auto lastFabric = std::make_shared<HeadlessReactFabricResult>();
    bool runtimeInitialized = false;
    std::uint64_t inspectorSequence{0};
    std::int64_t lastPublishedSceneRevision{-1};
    instance->initializeRuntime(
        {.isProfiling = false},
        [&](jsi::Runtime& runtime) {
          runtime.global().setProperty(
              runtime, "RN$SimulatorEnvironment", true);
          installGlobalEvalWithSourceUrl(runtime);
          jsi::Object workloadConfig(runtime);
          workloadConfig.setProperty(
              runtime, "iterations", options.iterations);
          workloadConfig.setProperty(runtime, "seed", options.seed);
          workloadConfig.setProperty(
              runtime,
              "name",
              jsi::String::createFromUtf8(runtime, options.workload));
          runtime.global().setProperty(
              runtime,
              "RN$SimulatorWorkloadConfig",
              std::move(workloadConfig));
          jsi::Object workloadProtocol(runtime);
          workloadProtocol.setProperty(
              runtime,
              "ready",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(runtime, "ready"),
                  0,
                  [&workloadReady](
                      jsi::Runtime&,
                      const jsi::Value&,
                      const jsi::Value*,
                      size_t) {
                    workloadReady = true;
                    return jsi::Value::undefined();
                  }));
          workloadProtocol.setProperty(
              runtime,
              "complete",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(runtime, "complete"),
                  0,
                  [&workloadComplete](
                      jsi::Runtime&,
                      const jsi::Value&,
                      const jsi::Value*,
                      size_t) {
                    workloadComplete = true;
                    return jsi::Value::undefined();
                  }));
          workloadProtocol.setProperty(
              runtime,
              "mark",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(runtime, "mark"),
                  1,
                  [&workloadMarks](
                      jsi::Runtime& runtime,
                      const jsi::Value&,
                      const jsi::Value* args,
                      size_t count) {
                    if (count != 1 || !args[0].isString()) {
                      throw jsi::JSError(
                          runtime, "RN$SimulatorWorkload.mark expects a name");
                    }
                    workloadMarks.push_back({
                        .name = args[0].getString(runtime).utf8(runtime),
                        .timestamp = std::chrono::steady_clock::now(),
                    });
                    return jsi::Value::undefined();
                  }));
          workloadProtocol.setProperty(
              runtime,
              "registerRootTag",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(runtime, "registerRootTag"),
                  1,
                  [&workloadRootTags](
                      jsi::Runtime& runtime,
                      const jsi::Value&,
                      const jsi::Value* args,
                      size_t count) {
                    if (count != 1 || !args[0].isNumber()) {
                      throw jsi::JSError(
                          runtime,
                          "RN$SimulatorWorkload.registerRootTag expects a number");
                    }
                    workloadRootTags.push_back(args[0].asNumber());
                    return jsi::Value::undefined();
                  }));
          runtime.global().setProperty(
              runtime, "RN$SimulatorWorkload", std::move(workloadProtocol));
          jsi::Object runtimeAPI(runtime);
          runtimeAPI.setProperty(
              runtime,
              "loadBundle",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(runtime, "loadBundle"),
                  1,
                  [&requestedBundles](
                      jsi::Runtime& runtime,
                      const jsi::Value&,
                      const jsi::Value* args,
                      size_t count) {
                    if (count != 1 || !args[0].isString()) {
                      throw jsi::JSError(
                          runtime,
                          "RN$Simulator.loadBundle expects one path");
                    }
                    auto path =
                        args[0].getString(runtime).utf8(runtime);
                    return react::createPromiseAsJSIValue(
                        runtime,
                        [path = std::move(path), &requestedBundles](
                            jsi::Runtime&,
                            std::shared_ptr<react::Promise> promise) {
                          requestedBundles.push_back(
                              {.path = path, .promise = std::move(promise)});
                        });
                  }));
          runtimeAPI.setProperty(
              runtime,
              "getLoadedBundles",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(runtime, "getLoadedBundles"),
                  0,
                  [&bundleRecords](
                      jsi::Runtime& runtime,
                      const jsi::Value&,
                      const jsi::Value*,
                      size_t) {
                    jsi::Array result(runtime, bundleRecords.size());
                    for (size_t index = 0; index < bundleRecords.size();
                         ++index) {
                      result.setValueAtIndex(
                          runtime,
                          index,
                          jsi::String::createFromUtf8(
                              runtime,
                              bundleRecords[index].path));
                    }
                    return result;
                  }));
          runtimeAPI.setProperty(
              runtime,
              "dispatchActions",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(runtime, "dispatchActions"),
                  1,
                  [impl = impl_.get()](
                      jsi::Runtime& runtime,
                      const jsi::Value&,
                      const jsi::Value* args,
                      size_t count) {
                    if (count != 1 || !args[0].isObject() ||
                        !args[0].getObject(runtime).isArray(runtime)) {
                      throw jsi::JSError(
                          runtime,
                          "RN$Simulator.dispatchActions expects an array");
                    }
                    const auto dynamicActions =
                        jsi::dynamicFromValue(runtime, args[0]);
                    std::vector<rns::InteractionAction> actions;
                    actions.reserve(dynamicActions.size());
                    for (std::size_t index = 0;
                         index < dynamicActions.size(); ++index) {
                      actions.push_back(
                          parseInteractionAction(dynamicActions[index], index));
                    }
                    return react::createPromiseAsJSIValue(
                        runtime,
                        [impl, actions = std::move(actions)](
                            jsi::Runtime& runtime,
                            std::shared_ptr<react::Promise> promise) mutable {
                          if (actions.empty()) {
                            promise->resolve(jsi::Array(runtime, 0));
                            return;
                          }
                          auto batch = std::make_shared<PendingActionBatch>();
                          batch->promise = std::move(promise);
                          try {
                            impl->enqueueBatch(std::move(actions), batch);
                          } catch (const std::exception& error) {
                            batch->promise->reject(error.what());
                          }
                        });
                  }));
          runtime.global().setProperty(
              runtime, "RN$Simulator", std::move(runtimeAPI));
          runtime.global().setProperty(
              runtime,
              "requestAnimationFrame",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(
                      runtime, "requestAnimationFrame"),
                  1,
                  [eventLoop](
                      jsi::Runtime& runtime,
                      const jsi::Value&,
                      const jsi::Value* args,
                      size_t count) -> jsi::Value {
                    if (count < 1 || !args[0].isObject() ||
                        !args[0].asObject(runtime).isFunction(runtime)) {
                      throw jsi::JSError(
                          runtime,
                          "requestAnimationFrame expects a function");
                    }
                    auto callback = std::make_shared<jsi::Function>(
                        args[0].asObject(runtime).asFunction(runtime));
                    const auto handle = eventLoop->requestAnimationFrame(
                        [callback](
                            jsi::Runtime& runtime, double timestamp) {
                          callback->call(runtime, timestamp);
                        });
                    return static_cast<double>(handle);
                  }));
          runtime.global().setProperty(
              runtime,
              "cancelAnimationFrame",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(
                      runtime, "cancelAnimationFrame"),
                  1,
                  [eventLoop](
                      jsi::Runtime&,
                      const jsi::Value&,
                      const jsi::Value* args,
                      size_t count) -> jsi::Value {
                    if (count > 0 && args[0].isNumber()) {
                      eventLoop->cancelAnimationFrame(
                          static_cast<uint32_t>(args[0].asNumber()));
                    }
                    return jsi::Value::undefined();
                  }));
          const auto importEnvNumber = [&runtime](const char* name) {
            if (const char* value = std::getenv(name)) {
              runtime.global().setProperty(
                  runtime, name, std::strtod(value, nullptr));
            }
          };
          importEnvNumber("RNSIM_TEXT_SCROLL");
          importEnvNumber("RNSIM_VIEW_SCROLL");
          jsi::Object bridgeConfig(runtime);
          bridgeConfig.setProperty(
              runtime, "remoteModuleConfig", jsi::Array(runtime, 0));
          runtime.global().setProperty(
              runtime, "__fbBatchedBridgeConfig", std::move(bridgeConfig));
          auto moduleProvider =
              [jsInvoker,
               profile = runtimeProfile.get(),
               &addonRegistry,
               &turboModuleCache,
               &moduleFidelities,
               impl = impl_.get(),
               profileName = options.profile,
               eventLoop](
                  jsi::Runtime& runtime,
                  const std::string& name)
                  -> std::shared_ptr<react::TurboModule> {
            // React and Metro probe objects for metadata. These are property
            // reads on nativeModuleProxy, not native-module requests.
            if (name == "$$typeof" || name == "__esModule") {
              return nullptr;
            }
            if (const auto found = turboModuleCache.find(name);
                found != turboModuleCache.end()) {
              return found->second;
            }
            auto module = getHeadlessTurboModule(
                runtime, name, *profile, addonRegistry, jsInvoker, eventLoop);
            const auto fidelity = moduleFidelities.find(name);
            if (module) {
              turboModuleCache.emplace(name, module);
              impl->recordCapabilityUsage(makeRuntimeCapabilityUsage(
                  "module",
                  name,
                  fidelity == moduleFidelities.end()
                      ? "available"
                      : fidelity->second));
            } else {
              impl->recordCapabilityUsage(makeRuntimeCapabilityUsage(
                  "module", name, "unavailable"));
              impl->recordRuntimeDiagnostic({
                  .kind = rns::RuntimeDiagnosticKind::MissingNativeModule,
                  .name = name,
                  .message = "NativeModule '" + name +
                      "' is unavailable for " + profileName +
                      "; provide an explicit addon or continue this flow on "
                      "Android Emulator.",
              });
            }
            return module;
          };
          if (runtimeProfile->platform() == "android") {
            (void)moduleProvider(runtime, "DeviceEventManager");
          }
          react::TurboModuleBinding::install(
              runtime, std::move(moduleProvider));
          const auto addonComponents = addonRegistry.componentCapabilities();
          const auto addonViewManagerConfigs =
              addonRegistry.viewManagerConfigs();
          runtime.global().setProperty(
              runtime,
              "RN$LegacyInterop_UIManager_getConstants",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(
                      runtime, "RN$LegacyInterop_UIManager_getConstants"),
                  0,
                  [addonComponents, addonViewManagerConfigs](
                      jsi::Runtime& runtime,
                      const jsi::Value&,
                      const jsi::Value*,
                      size_t) -> jsi::Value {
                    jsi::Object constants(runtime);
                    constants.setProperty(
                        runtime, "genericBubblingEventTypes", jsi::Object(runtime));
                    constants.setProperty(
                        runtime, "genericDirectEventTypes", jsi::Object(runtime));
                    for (const auto& component : addonComponents) {
                      const auto config = std::find_if(
                          addonViewManagerConfigs.begin(),
                          addonViewManagerConfigs.end(),
                          [&](const auto& candidate) {
                            return candidate.name == component.name;
                          });
                      constants.setProperty(
                          runtime,
                          component.name.c_str(),
                          makeLegacyViewManagerConfig(
                              runtime,
                              config == addonViewManagerConfigs.end()
                                  ? nullptr
                                  : &*config));
                    }
                    return constants;
                  }));
          runtime.global().setProperty(
              runtime,
              "RN$LegacyInterop_UIManager_getConstantsForViewManager",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(
                      runtime,
                      "RN$LegacyInterop_UIManager_getConstantsForViewManager"),
                  1,
                  [addonViewManagerConfigs](
                      jsi::Runtime& runtime,
                      const jsi::Value&,
                      const jsi::Value* args,
                      size_t count) -> jsi::Value {
                    std::string name;
                    if (count > 0 && args[0].isString()) {
                      name = args[0].getString(runtime).utf8(runtime);
                    }
                    const auto config = std::find_if(
                        addonViewManagerConfigs.begin(),
                        addonViewManagerConfigs.end(),
                        [&](const auto& candidate) {
                          return candidate.name == name;
                        });
                    return makeLegacyViewManagerConfig(
                        runtime,
                        config == addonViewManagerConfigs.end()
                            ? nullptr
                            : &*config);
                  }));
          runtime.global().setProperty(
              runtime,
              "RN$LegacyInterop_UIManager_getDefaultEventTypes",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(
                      runtime,
                      "RN$LegacyInterop_UIManager_getDefaultEventTypes"),
                  0,
                  [](jsi::Runtime& runtime,
                     const jsi::Value&,
                     const jsi::Value*,
                     size_t) -> jsi::Value {
                    return jsi::Array(runtime, 0);
                  }));
          runtime.global().setProperty(
              runtime,
              "__nativeComponentRegistry__hasComponent",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(
                      runtime, "__nativeComponentRegistry__hasComponent"),
                  1,
                  [addonComponents](
                      jsi::Runtime& runtime,
                      const jsi::Value&,
                      const jsi::Value* args,
                      size_t count) -> jsi::Value {
                    if (count != 1 || !args[0].isString()) {
                      return false;
                    }
                    const auto name =
                        args[0].getString(runtime).utf8(runtime);
                    return std::any_of(
                        addonComponents.begin(),
                        addonComponents.end(),
                        [&name](const auto& component) {
                          return component.name == name;
                        });
                  }));
          // TurboModuleBinding exposes nativeModuleProxy in bridgeless mode,
          // while the public TurboModuleRegistry module captures
          // __turboModuleProxy during bundle evaluation. Keep both public RN
          // lookup paths backed by the same provider and identity cache.
          runtime.global().setProperty(
              runtime,
              "__turboModuleProxy",
              jsi::Function::createFromHostFunction(
                  runtime,
                  jsi::PropNameID::forAscii(
                      runtime, "__turboModuleProxy"),
                  1,
                  [](jsi::Runtime& runtime,
                     const jsi::Value&,
                     const jsi::Value* args,
                     size_t count) -> jsi::Value {
                    if (count < 1 || !args[0].isString()) {
                      throw jsi::JSError(
                          runtime,
                          "__turboModuleProxy expects a module name");
                    }
                    auto proxy = runtime.global().getPropertyAsObject(
                        runtime, "nativeModuleProxy");
                    const auto moduleName =
                        args[0].getString(runtime).utf8(runtime);
                    return proxy.getProperty(runtime, moduleName.c_str());
                  }));
          reactFabricHost =
              installHeadlessReactFabric(
                  runtime,
                  runtimeExecutor,
                  instance->getRuntimeScheduler(),
                  eventLoop,
                  options.viewportWidth,
                  options.viewportHeight,
                  options.pointScaleFactor,
                  options.insetTop,
                  options.insetBottom,
                  options.fontDirectory.value_or(std::filesystem::path{}),
                  options.assetDirectory.value_or(std::filesystem::path{}),
                  runtimeProfile->platform(),
                  addonRegistry.componentCapabilities(),
                  [impl = impl_.get(),
                   componentFidelities = std::move(componentFidelities),
                   inspectorTransport,
                   &options,
                   platform = runtimeProfile->platform(),
                   runtimeGeneration =
                       static_cast<std::uint64_t>(sessionGeneration),
                   &inspectorSequence,
                   &lastPublishedSceneRevision,
                   lastFabric](
                      const HeadlessReactFabricResult& result) mutable {
                    *lastFabric = result;
                    for (const auto& node : result.mountedViewNodes) {
                      const auto capability =
                          componentFidelities.find(node.componentName);
                      if (capability != componentFidelities.end()) {
                        impl->recordCapabilityUsage(makeRuntimeCapabilityUsage(
                            "component",
                            capability->first,
                            capability->second));
                      }
                    }
                    for (const auto& name : result.fallbackComponentNames) {
                      impl->recordCapabilityUsage(makeRuntimeCapabilityUsage(
                          "component", name, "fallback-descriptor"));
                      impl->recordRuntimeDiagnostic({
                          .kind = rns::RuntimeDiagnosticKind::FallbackComponent,
                          .name = name,
                          .message = "Fabric component '" + name +
                              "' is using a fallback descriptor; its pixels "
                              "and behavior are not Android-equivalent.",
                      });
                    }
                    if (options.onSceneUpdate) {
                      options.onSceneUpdate(makeSceneSnapshot(
                          options, result, runtimeGeneration));
                      lastPublishedSceneRevision = result.mountingRevision;
                    }
                    if (inspectorTransport) {
                      const auto sequence = ++inspectorSequence;
                      inspectorTransport->sendJson(folly::toJson(
                          folly::dynamic::object
                              ("type", "snapshot")
                              ("state", "running")
                              ("metrics", makeLiveInspectorSnapshot(
                                  options,
                                  platform,
                                  result,
                                  "running",
                                  sequence,
                                  true))
                              ("scene", makeSceneWireSnapshot(options, result))));
                    }
                  });
          hostChrome().onInvalidate = [
              eventLoop,
              lastFabric,
              &options,
              runtimeGeneration =
                  static_cast<std::uint64_t>(sessionGeneration)]() {
            eventLoop->runOnQueue([
                lastFabric, &options, runtimeGeneration]() {
              if (options.onSceneUpdate) {
                options.onSceneUpdate(makeSceneSnapshot(
                    options, *lastFabric, runtimeGeneration));
              }
            });
          };
          runtimeInitialized = true;
        });
    eventLoop->runUntil(
        [&] { return runtimeInitialized || jsErrorCount > 0; },
        std::chrono::milliseconds(options.timeoutMs));
    if (!runtimeInitialized) {
      throw std::runtime_error("ReactInstance initialization timed out");
    }
    impl_->setRuntimePhase(rns::RuntimePhase::LoadingBundle);
    if (inspectorTransport) {
      inspectorTransport->sendJson(folly::toJson(folly::dynamic::object
          ("type", "status")
          ("state", "ready")));
    }
    if (devTools && options.devTools.waitForDebuggerMs > 0) {
      const auto debuggerDeadline = std::chrono::steady_clock::now() +
          std::chrono::milliseconds(options.devTools.waitForDebuggerMs);
      while (!devTools->hasSession() &&
             std::chrono::steady_clock::now() < debuggerDeadline) {
        eventLoop->runFor(std::chrono::milliseconds(10));
      }
      if (!devTools->hasSession()) {
        std::cerr << "React Native DevTools did not attach within "
                  << options.devTools.waitForDebuggerMs << "ms\n";
      }
    }
    const auto initializationEnd = std::chrono::steady_clock::now();
    const auto workloadMetricsBaseline = readRuntimeMetrics(*runtime);

    const auto bundleStart = std::chrono::steady_clock::now();
    const auto workloadDeadline =
        bundleStart + std::chrono::milliseconds(options.timeoutMs);
    size_t eventLoopTasks = 0;
    size_t requestedBundleCount = impl_->bundles.size();
    bool bundleLoadFailed = false;
    bool hmrConfigured = false;
    std::unique_ptr<PackagerConnection> packager;
    std::string combinedBundleData;
    const auto remainingTimeout = [&]() {
      const auto now = std::chrono::steady_clock::now();
      return now >= workloadDeadline
          ? std::chrono::milliseconds::zero()
          : std::chrono::duration_cast<std::chrono::milliseconds>(
                workloadDeadline - now);
    };
    const auto loadBundle = [&](const InitialBundle& source,
                                bool requestedByJS,
                                const std::shared_ptr<react::Promise>& promise) {
      BundleRecord record{
          .path = source.sourceUrl,
          .requestedByJS = requestedByJS,
      };
      std::string contents;
      const auto readStart = std::chrono::steady_clock::now();
      record.readStart = readStart;
      try {
        contents = source.bytes
            ? *source.bytes
            : readFile(*source.path);
        record.bytes = contents.size();
        record.hash = hashBundle(contents);
        record.readMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - readStart)
                            .count();
        record.readEnd = std::chrono::steady_clock::now();
      } catch (const std::exception& error) {
        record.readEnd = std::chrono::steady_clock::now();
        record.error = error.what();
        {
          std::lock_guard lock(impl_->applicationMutex);
          impl_->launchState.lastError = record.error;
          impl_->launchState.pending = false;
        }
        impl_->recordRuntimeDiagnostic({
            .kind = rns::RuntimeDiagnosticKind::ApplicationError,
            .message = record.error,
        });
        impl_->setRuntimePhase(rns::RuntimePhase::PausedAfterError);
        bundleRecords.push_back(std::move(record));
        if (promise) {
          const auto message = bundleRecords.back().error;
          runtimeExecutor(
              [promise, message](jsi::Runtime&) { promise->reject(message); });
          eventLoopTasks += eventLoop->drainUntilIdle();
        }
        std::cerr << "Bundle load failed: " << error.what() << '\n';
        return false;
      }
      bool loaded = false;
      const auto errorsBeforeLoad = jsErrorCount;
      const auto evaluationStart = std::chrono::steady_clock::now();
      record.evaluationStart = evaluationStart;
      if (devTools) {
        devTools->registerSource(source.sourceUrl, contents);
      }
      instance->loadScript(
          std::make_unique<react::JSBigStdString>(contents),
          source.sourceUrl,
          nullptr,
          [&loaded](jsi::Runtime&) { loaded = true; });
      eventLoopTasks += eventLoop->runUntil(
          [&] { return loaded || jsErrorCount > errorsBeforeLoad; },
          remainingTimeout());
      record.loaded = loaded && jsErrorCount == errorsBeforeLoad;
      record.evaluationMs = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() -
                                evaluationStart)
                                .count();
      record.evaluationEnd = std::chrono::steady_clock::now();
      if (!record.loaded) {
        record.error = jsErrorCount > errorsBeforeLoad
            ? "JavaScript evaluation failed"
            : "Bundle load timed out";
        if (jsErrorCount == errorsBeforeLoad) {
          std::lock_guard lock(impl_->applicationMutex);
          impl_->launchState.lastError = record.error;
          impl_->launchState.pending = false;
        }
        if (jsErrorCount == errorsBeforeLoad) {
          impl_->recordRuntimeDiagnostic({
              .kind = rns::RuntimeDiagnosticKind::ApplicationError,
              .message = record.error,
          });
          impl_->setRuntimePhase(rns::RuntimePhase::PausedAfterError);
        }
      } else {
        combinedBundleData.append(contents);
        combinedBundleData.push_back('\0');
        runtimeExecutor([&](jsi::Runtime& runtime) {
          impl_->refreshAppRegistry(runtime);
        });
        eventLoopTasks += eventLoop->drainUntilIdle();
      }
      bundleRecords.push_back(std::move(record));
      if (promise) {
        if (bundleRecords.back().loaded) {
          const auto pathString = bundleRecords.back().path;
          const auto hash = bundleRecords.back().hash;
          const auto bytes = bundleRecords.back().bytes;
          runtimeExecutor([promise, pathString, hash, bytes](
                              jsi::Runtime& runtime) {
            jsi::Object result(runtime);
            result.setProperty(
                runtime,
                "path",
                jsi::String::createFromUtf8(runtime, pathString));
            result.setProperty(
                runtime,
                "hash",
                jsi::String::createFromUtf8(runtime, hash));
            result.setProperty(runtime, "bytes", static_cast<double>(bytes));
            promise->resolve(jsi::Value(std::move(result)));
          });
        } else {
          const auto message = bundleRecords.back().error;
          runtimeExecutor(
              [promise, message](jsi::Runtime&) { promise->reject(message); });
        }
        eventLoopTasks += eventLoop->drainUntilIdle();
      }
      return bundleRecords.back().loaded;
    };

    const auto processQueuedActions = [&]() {
      auto queued = impl_->takeActions();
      std::size_t processed = 0;
      for (auto& entry : queued) {
        if (entry.batch && entry.batch->rejected) {
          continue;
        }
        auto actionResult = dispatchHeadlessReactFabricAction(
            *reactFabricHost, entry.action, entry.sequences.back());
        eventLoopTasks += eventLoop->drainUntilIdle();
        actionResult.sceneRevision =
            getHeadlessReactFabricResult(*reactFabricHost).mountingRevision;
        for (const auto sequence : entry.sequences) {
          auto resultForSequence = actionResult;
          resultForSequence.sequence = sequence;
          if (options.onActionResult) {
            options.onActionResult(resultForSequence);
          }
        }
        if (entry.batch) {
          entry.batch->results[entry.batchIndex] = actionResult;
          if (!actionResult) {
            entry.batch->rejected = true;
            const auto promise = entry.batch->promise;
            const auto message =
                "interaction action[" +
                std::to_string(entry.batchIndex) + "]: " +
                actionResult.error;
            runtimeExecutor([promise, message](jsi::Runtime&) {
              promise->reject(message);
            });
          } else if (--entry.batch->remaining == 0) {
            folly::dynamic results = folly::dynamic::array;
            for (const auto& result : entry.batch->results) {
              results.push_back(folly::dynamic::object
                  ("sequence", static_cast<double>(result.sequence))
                  ("targetTag", result.targetTag
                      ? folly::dynamic(*result.targetTag)
                      : folly::dynamic(nullptr))
                  ("sceneRevision", result.sceneRevision));
            }
            const auto promise = entry.batch->promise;
            runtimeExecutor(
                [promise, results = std::move(results)](
                    jsi::Runtime& runtime) {
                  promise->resolve(jsi::valueFromDynamic(runtime, results));
                });
          }
        }
        ++processed;
      }
      if (processed > 0) {
        eventLoopTasks += eventLoop->drainUntilIdle();
      }
      return processed;
    };

    const auto processQueuedApplication = [&]() {
      auto request = impl_->takeApplicationRequest();
      if (!request) {
        return;
      }
      bool done = false;
      runtimeExecutor([&](jsi::Runtime& runtime) {
        try {
          const auto initialProps =
              parseInitialPropsJson(request->initialPropsJson);
          impl_->applyHostApplication(
              runtime,
              request->appKey,
              initialProps,
              request->initialPropsJson);
          impl_->setRuntimePhase(rns::RuntimePhase::Running);
          std::cerr << "running AppRegistry application " << request->appKey
                    << '\n';
        } catch (const jsi::JSError& error) {
          const auto message = error.getMessage();
          {
            std::lock_guard lock(impl_->applicationMutex);
            impl_->launchState.lastError = message;
            impl_->launchState.pending = false;
          }
          impl_->recordRuntimeDiagnostic({
              .kind = rns::RuntimeDiagnosticKind::ApplicationError,
              .message = message,
          });
          impl_->setRuntimePhase(rns::RuntimePhase::ChoosingApplication);
        } catch (const std::exception& error) {
          const std::string message = error.what();
          {
            std::lock_guard lock(impl_->applicationMutex);
            impl_->launchState.lastError = message;
            impl_->launchState.pending = false;
          }
          impl_->recordRuntimeDiagnostic({
              .kind = rns::RuntimeDiagnosticKind::ApplicationError,
              .message = message,
          });
          impl_->setRuntimePhase(rns::RuntimePhase::ChoosingApplication);
        }
        done = true;
      });
      eventLoopTasks += eventLoop->drainUntilIdle();
      if (!done) {
        eventLoopTasks += eventLoop->runUntil(
            [&] { return done; }, std::chrono::milliseconds(5000));
      }
    };

    for (const auto& source : impl_->bundles) {
      std::optional<MetroDevServer> metro;
      if (developmentMode && source.http && !hmrConfigured) {
        metro = parseMetroDevServer(source.sourceUrl);
        hmrConfigured = true;
        if (metro) {
          try {
            packager = PackagerConnection::connect(
                "ws://" + metro->host + ":" +
                    std::to_string(metro->port) + "/message",
                [impl = impl_.get()]() { impl->reloadRequested.store(true); });
          } catch (const std::exception& error) {
            std::cerr << "Metro reload connection failed: " << error.what()
                      << '\n';
          }
        }
      }
      if (!loadBundle(source, false, nullptr)) {
        bundleLoadFailed = true;
        break;
      }
      if (metro) {
        impl_->setHMRStatus(rns::HMRStatus::Enabling);
        const auto errorsBeforeHMRSetup = jsErrorCount;
        try {
          setupHMRClient(*instance, *metro);
          eventLoopTasks += eventLoop->drainUntilIdle();
          if (jsErrorCount != errorsBeforeHMRSetup) {
            const auto message = jsErrors.empty()
                ? std::string{"HMRClient.setup raised a JavaScript error"}
                : jsErrors.back().message;
            impl_->setHMRStatus(rns::HMRStatus::Failed, message);
            std::cerr << "HMRClient.setup failed: " << message << '\n';
          } else {
            impl_->setHMRStatus(rns::HMRStatus::Enabled);
            std::cerr << "Fast Refresh client enabled for "
                      << metro->host << ":" << metro->port << '\n';
          }
        } catch (const std::exception& error) {
          impl_->setHMRStatus(rns::HMRStatus::Failed, error.what());
          std::cerr << "HMRClient.setup failed: " << error.what() << '\n';
        }
      }
      processQueuedActions();
      processQueuedApplication();
    }
    {
      std::lock_guard lock(impl_->applicationMutex);
      impl_->launchState.initialBundlesLoaded = true;
    }
    if (developmentMode && !rerunAppKey && options.autoRunApplication &&
        !bundleLoadFailed && jsErrorCount == 0) {
      impl_->setRuntimePhase(rns::RuntimePhase::StartingApplication);
      bool applicationStarted = false;
      bool applicationSelectionRequired = false;
      std::string applicationStartError;
      runtimeExecutor([&](jsi::Runtime& runtime) {
        try {
          impl_->refreshAppRegistry(runtime);
          std::vector<std::string> keys;
          {
            std::lock_guard lock(impl_->applicationMutex);
            keys = impl_->launchState.appKeys;
          }
          const auto appKey = resolveAutoRunAppKey(keys, options.appKey);
          if (!appKey) {
            applicationSelectionRequired = true;
            if (options.appKey) {
              std::ostringstream message;
              message << "Configured AppRegistry key '" << *options.appKey
                      << "' is not registered";
              if (!keys.empty()) {
                message << "; registered keys:";
                for (const auto& key : keys) {
                  message << ' ' << key;
                }
              }
              applicationStartError = message.str();
            }
            return;
          }
          const auto initialProps =
              parseInitialPropsJson(options.initialPropsJson);
          impl_->applyHostApplication(
              runtime, *appKey, initialProps, options.initialPropsJson);
          applicationStarted = true;
          std::cerr << "running AppRegistry application " << *appKey << '\n';
        } catch (const jsi::JSError& error) {
          applicationStartError = error.getMessage();
        } catch (const std::exception& error) {
          applicationStartError = error.what();
        }
      });
      eventLoopTasks += eventLoop->runUntil(
          [&] {
            return applicationStarted || applicationSelectionRequired ||
                !applicationStartError.empty();
          },
          remainingTimeout());
      if (applicationSelectionRequired) {
        impl_->setRuntimePhase(rns::RuntimePhase::ChoosingApplication);
        if (!applicationStartError.empty()) {
          {
            std::lock_guard lock(impl_->applicationMutex);
            impl_->launchState.lastError = applicationStartError;
          }
          impl_->recordRuntimeDiagnostic({
              .kind = rns::RuntimeDiagnosticKind::ApplicationError,
              .message = applicationStartError,
          });
        }
        std::cerr
            << "multiple or no AppRegistry applications are available; "
               "select one from the App panel\n";
      } else if (!applicationStarted) {
        applicationStartError = applicationStartError.empty()
            ? "AppRegistry.runApplication timed out"
            : applicationStartError;
        {
          std::lock_guard lock(impl_->applicationMutex);
          impl_->launchState.lastError = applicationStartError;
          impl_->launchState.pending = false;
        }
        impl_->recordRuntimeDiagnostic({
            .kind = rns::RuntimeDiagnosticKind::ApplicationError,
            .message = applicationStartError,
        });
        impl_->setRuntimePhase(rns::RuntimePhase::PausedAfterError);
        bundleLoadFailed = true;
      } else {
        impl_->setRuntimePhase(rns::RuntimePhase::Running);
      }
    } else if (
        developmentMode && rerunAppKey && !bundleLoadFailed &&
        jsErrorCount == 0) {
      impl_->setRuntimePhase(rns::RuntimePhase::StartingApplication);
      bool applicationStarted = false;
      std::string applicationStartError;
      runtimeExecutor([&](jsi::Runtime& runtime) {
        try {
          const auto initialProps =
              parseInitialPropsJson(rerunInitialPropsJson);
          impl_->applyHostApplication(
              runtime,
              *rerunAppKey,
              initialProps,
              rerunInitialPropsJson);
          applicationStarted = true;
          std::cerr << "re-running AppRegistry application " << *rerunAppKey
                    << '\n';
        } catch (const jsi::JSError& error) {
          applicationStartError = error.getMessage();
        } catch (const std::exception& error) {
          applicationStartError = error.what();
        }
      });
      eventLoopTasks += eventLoop->runUntil(
          [&] { return applicationStarted || !applicationStartError.empty(); },
          remainingTimeout());
      if (!applicationStarted) {
        applicationStartError = applicationStartError.empty()
            ? "AppRegistry.runApplication timed out"
            : applicationStartError;
        std::cerr << "reload did not restore "
                  << *rerunAppKey << ": "
                  << applicationStartError
                  << '\n';
        {
          std::lock_guard lock(impl_->applicationMutex);
          impl_->launchState.lastError = applicationStartError;
          impl_->launchState.pending = false;
        }
        impl_->recordRuntimeDiagnostic({
            .kind = rns::RuntimeDiagnosticKind::ApplicationError,
            .message = applicationStartError,
        });
        impl_->setRuntimePhase(rns::RuntimePhase::PausedAfterError);
        bundleLoadFailed = true;
      } else {
        impl_->setRuntimePhase(rns::RuntimePhase::Running);
      }
    } else if (developmentMode && !bundleLoadFailed && jsErrorCount == 0) {
      impl_->setRuntimePhase(rns::RuntimePhase::ChoosingApplication);
    }
    while (!bundleLoadFailed && jsErrorCount == 0) {
      processQueuedActions();
      processQueuedApplication();
      while (!requestedBundles.empty()) {
        auto request = std::move(requestedBundles.front());
        requestedBundles.pop_front();
        ++requestedBundleCount;
        if (!loadBundle(
                InitialBundle{
                    .sourceUrl = request.path.string(),
                    .path = request.path,
                },
                true,
                request.promise)) {
          bundleLoadFailed = true;
          break;
        }
      }
      if (bundleLoadFailed ||
          (!developmentMode &&
           (workloadComplete || remainingTimeout().count() == 0)) ||
          (developmentMode &&
           (impl_->stopRequested || impl_->reloadRequested ||
            (options.devTools.waitForDisconnect &&
             (!devTools || !devTools->hasSession()))))) {
        break;
      }
      eventLoopTasks += eventLoop->runUntil(
          [&] {
            return (!developmentMode && workloadComplete) ||
                (developmentMode &&
                 (impl_->stopRequested || impl_->reloadRequested ||
                  (options.devTools.waitForDisconnect &&
                   (!devTools || !devTools->hasSession())))) ||
                jsErrorCount > 0 || !requestedBundles.empty() ||
                impl_->hasActions() || impl_->hasApplicationRequest();
          },
          developmentMode ? std::chrono::milliseconds(25)
                          : remainingTimeout());
    }
    if (developmentMode && (bundleLoadFailed || jsErrorCount > 0) &&
        !impl_->stopRequested.load() && !impl_->reloadRequested.load()) {
      impl_->setRuntimePhase(rns::RuntimePhase::PausedAfterError);
      std::cerr
          << "interactive runtime paused after an error; fix the bundle, then "
             "use Reload or Metro's r command\n";
      while (!impl_->stopRequested.load() &&
             !impl_->reloadRequested.load()) {
        eventLoop->runFor(std::chrono::milliseconds(25));
      }
    }
    const bool shouldReload = developmentMode &&
        impl_->reloadRequested.load() && !impl_->stopRequested.load();
    if (shouldReload) {
      impl_->setRuntimePhase(rns::RuntimePhase::Reloading);
      std::optional<int> rootTag;
      {
        std::lock_guard lock(impl_->applicationMutex);
        rerunAppKey = impl_->launchState.runningAppKey;
        rerunInitialPropsJson = impl_->lastInitialPropsJson;
        rootTag = impl_->runningRootTag;
      }
      packager.reset();
      if (rootTag) {
        runtimeExecutor([rootTag = *rootTag](jsi::Runtime& runtime) {
          try {
            stopHostApplication(runtime, rootTag);
          } catch (const std::exception&) {
          }
        });
        eventLoop->drainUntilIdle();
      }
      hostChrome().onInvalidate = nullptr;
      headlessKeyboard().emit = nullptr;
      headlessBackPress().emit = nullptr;
      headlessBackPress().invokeDefault = nullptr;
      headlessBackPress().runOnJs = nullptr;
      hostEnvironment().reset();
      headlessWebSocketReset();
      headlessBlobReset();
      headlessImageRequestsReset();
      reactFabricHost.reset();
      turboModuleCache.clear();
      timerManager.reset();
      if (devTools) {
        instance->unregisterFromInspector();
      }
      instance.reset();
      eventLoop->quitSynchronous();
      continue;
    }
    const bool bundleLoaded = !bundleLoadFailed &&
        bundleRecords.size() == requestedBundleCount &&
        std::all_of(
            bundleRecords.begin(), bundleRecords.end(), [](const auto& record) {
              return record.loaded;
            });
    const auto bundleHash = bundleRecords.size() == 1
        ? bundleRecords.front().hash
        : hashBundle(combinedBundleData);
    const auto bundleBytes = std::accumulate(
        bundleRecords.begin(),
        bundleRecords.end(),
        size_t{0},
        [](size_t total, const auto& record) { return total + record.bytes; });
    const auto bundlesLoaded = std::count_if(
        bundleRecords.begin(), bundleRecords.end(), [](const auto& record) {
          return record.loaded;
        });
    const bool workloadTimedOut = !developmentMode && !workloadComplete &&
        jsErrorCount == 0 && !bundleLoadFailed;
    // Capture the completed scene before Fabric teardown. Paper's
    // unmountApplicationComponentAtRootTag is a console.error in RN 0.87.
    const auto reactFabric = getHeadlessReactFabricResult(*reactFabricHost);
    if (!developmentMode && workloadComplete) {
      for (const auto rootTag : workloadRootTags) {
        runtimeExecutor([rootTag](jsi::Runtime& runtime) {
          auto stop = runtime.global().getProperty(runtime, "RN$stopSurface");
          if (stop.isObject() &&
              stop.getObject(runtime).isFunction(runtime)) {
            stop.getObject(runtime).getFunction(runtime).call(
                runtime, rootTag);
            return;
          }
          auto appRegistry =
              runtime.global().getProperty(runtime, "RN$AppRegistry");
          if (!appRegistry.isObject()) {
            return;
          }
          auto registryObject = appRegistry.getObject(runtime);
          if (!registryObject.hasProperty(
                  runtime, "unmountApplicationComponentAtRootTag") ||
              !registryObject.getProperty(
                                 runtime,
                                 "unmountApplicationComponentAtRootTag")
                   .isObject() ||
              !registryObject
                   .getProperty(
                       runtime, "unmountApplicationComponentAtRootTag")
                   .getObject(runtime)
                   .isFunction(runtime)) {
            return;
          }
          auto unmount = registryObject.getPropertyAsFunction(
              runtime, "unmountApplicationComponentAtRootTag");
          unmount.callWithThis(runtime, registryObject, rootTag);
        });
      }
      // complete() may be called from inside a scheduler/timer callback that
      // queues teardown work after signalling completion. Run only work that
      // is ready now; future timers remain visible to the pending-work gate.
      eventLoopTasks += eventLoop->drainUntilIdle();
    }
    if (!developmentMode && workloadComplete && options.settleMs > 0) {
      eventLoopTasks +=
          eventLoop->runFor(std::chrono::milliseconds(options.settleMs));
    }
    const bool pendingWork = eventLoop->hasPendingWork();
    const auto bundleEnd = std::chrono::steady_clock::now();
    if (options.tracePath) {
      writeTrace(
          *options.tracePath,
          processStart,
          initializationEnd,
          bundleEnd,
          bundleRecords,
          workloadMarks,
          jsErrors,
          reactFabric);
    }
    const bool workloadResultPresent = runtime->global()
                                      .getProperty(
                                          *runtime,
                                          "RN$SimulatorWorkloadResult")
                                      .isObject();
    const auto workload = readWorkloadResult(*runtime);
    const auto runtimeMetrics = readRuntimeMetrics(*runtime);
    const auto workloadRuntimeMetrics = subtractRuntimeMetrics(
        runtimeMetrics, workloadMetricsBaseline);
    const bool timersPassed = workload.timeoutFired &&
        !workload.canceledTimerFired && workload.intervalCount == 1 &&
        workload.microtaskFired;
    const bool nativeModulePassed = workload.nativeModuleSum == 42 &&
        workload.nativeModuleEcho == "react-native-simulator";
    const bool nativeComponentPassed = reactFabric.customComponentCreated &&
        reactFabric.customComponentValue ==
            static_cast<int>(workload.customComponentExpectedValue) &&
        reactFabric.customComponentLabel ==
            "iteration-" +
                std::to_string(
                    static_cast<int>(workload.customComponentExpectedValue)) &&
        reactFabric.customCommands >= 1 &&
        workload.customComponentEventValue == 77 &&
        reactFabric.mockedComponents >= 1;
    const auto milliseconds = [](auto start, auto end) {
      return std::chrono::duration<double, std::milli>(end - start).count();
    };

    const bool schedulerInstalled = !runtime->global()
                                         .getProperty(
                                             *runtime,
                                             "nativeRuntimeScheduler")
                                         .isUndefined();
    folly::dynamic bundleMetadata = folly::dynamic::array;
    for (const auto& record : bundleRecords) {
      bundleMetadata.push_back(folly::dynamic::object
          ("path", record.path)
          ("bytes", record.bytes)
          ("hash", record.hash)
          ("readMs", record.readMs)
          ("evaluationMs", record.evaluationMs)
          ("requestedByJS", record.requestedByJS)
          ("loaded", record.loaded)
          ("error", record.error));
    }
    folly::dynamic addonMetadata = folly::dynamic::array;
    for (const auto& addonName : addonRegistry.names()) {
      addonMetadata.push_back(addonName);
    }
    folly::dynamic frameworkModuleMetadata = folly::dynamic::array;
    for (const auto& capability : runtimeProfile->moduleCapabilities()) {
      frameworkModuleMetadata.push_back(capability.name);
    }
    folly::dynamic addonModuleMetadata = folly::dynamic::array;
    for (const auto& moduleName : addonRegistry.moduleNames()) {
      addonModuleMetadata.push_back(moduleName);
    }
    folly::dynamic frameworkComponentCapabilities = folly::dynamic::object
        ("RootView", "real-fabric-root")
        ("View", "real-fabric-yoga")
        ("RawText", "real-fabric-virtual-text")
        ("Text", "real-fabric-virtual-text")
#if RNS_ENABLE_SKIA
        ("Paragraph", "skia-prepared-text")
#else
        ("Paragraph", "unavailable-without-skia")
#endif
        ("ScrollView", "headless-viewport-state")
#if RNS_ENABLE_SKIA
        ("Image", "skia-local-and-http-image");
#else
        ("Image", "unavailable-without-skia");
#endif
    for (const auto& spec : react::kHeadlessOfficialComponents) {
      frameworkComponentCapabilities[spec.name] =
          componentFidelityForBuild(spec.fidelity);
    }
    if (runtimeProfile->platform() == "ios") {
      frameworkComponentCapabilities["TextInput"] =
          "rn-fabric-headless-platform-adapter";
    } else {
      frameworkComponentCapabilities["AndroidTextInput"] =
          "rn-fabric-headless-platform-adapter";
    }
    folly::dynamic moduleCapabilities = folly::dynamic::object;
    for (const auto& capability : runtimeProfile->moduleCapabilities()) {
      moduleCapabilities[capability.name] = capability.fidelity;
    }
    for (const auto& capability : addonRegistry.moduleCapabilities()) {
      moduleCapabilities[capability.name] = capability.fidelity;
    }
#if RNS_ENABLE_SKIA
    const auto textFontProfile = options.fontDirectory
        ? "configured-font-directory"
        : runtimeProfile->platform() == "ios"
        ? "ios-coretext-system"
        : runtimeProfile->platform() == "android"
        ? "android-macos-coretext-fallback-unverified"
        : "macos-coretext-system";
    const folly::dynamic textCapabilities = folly::dynamic::object
        ("engine", "skia-skparagraph")
        ("fontProfile", textFontProfile)
        ("naturalWritingDirection", "unicode-first-strong")
        ("clip", "supported")
        ("tailEllipsis", "supported")
        ("headEllipsis", "android-truncate-at-start")
        ("middleEllipsis", "android-truncate-at-middle")
        ("adjustsFontSizeToFit", "android-spannable-binary-search")
        ("androidFontPadding", runtimeProfile->platform() == "android"
            ? "android-font-metrics"
            : "unsupported")
        ("hyphenation", "camel-case-soft-hyphen")
        ("textBreakStrategy", "android-high-quality-paragraph")
        ("textTransform", "icu-case-map")
        ("textShadow", "skparagraph-shadow")
        ("fontVariant", "opentype-features")
        ("dataDetector", "underline-link-spans");
#else
    const folly::dynamic textCapabilities = folly::dynamic::object
        ("engine", "unavailable-without-skia")
        ("fontProfile", "unavailable-without-skia");
#endif
    folly::dynamic fallbackComponents = folly::dynamic::array;
    for (const auto& componentName : reactFabric.fallbackComponentNames) {
      fallbackComponents.push_back(componentName);
    }
    const auto serializeViewNodes = [](const auto& sourceNodes) {
      folly::dynamic nodes = folly::dynamic::array;
      for (const auto& node : sourceNodes) {
        folly::dynamic backgroundColor = nullptr;
        if (node.hasBackgroundColor) {
          backgroundColor = folly::dynamic::object
              ("red", node.backgroundRed)
              ("green", node.backgroundGreen)
              ("blue", node.backgroundBlue)
              ("alpha", node.backgroundAlpha);
        }
        folly::dynamic customProps = nullptr;
        if (node.customValue) {
          customProps = folly::dynamic::object
              ("value", *node.customValue)
              ("label", node.customLabel);
        }
        nodes.push_back(folly::dynamic::object
            ("tag", node.tag)
            ("parentTag", node.parentTag ? folly::dynamic(*node.parentTag)
                                         : folly::dynamic(nullptr))
            ("index", node.childIndex)
            ("depth", node.depth)
            ("componentName", node.componentName)
            ("nativeId", node.nativeId)
            ("layoutable", node.layoutable)
            ("layout", folly::dynamic::object
                ("x", node.x)
                ("y", node.y)
                ("width", node.width)
                ("height", node.height)
                ("absoluteX", node.absoluteX)
                ("absoluteY", node.absoluteY)
                ("contentInsets", folly::dynamic::object
                    ("top", node.contentInsetTop)
                    ("right", node.contentInsetRight)
                    ("bottom", node.contentInsetBottom)
                    ("left", node.contentInsetLeft))
                ("borderWidth", folly::dynamic::object
                    ("top", node.borderTop)
                    ("right", node.borderRight)
                    ("bottom", node.borderBottom)
                    ("left", node.borderLeft))
                ("display", node.display)
                ("position", node.position))
            ("props", folly::dynamic::object
                ("opacity", node.opacity)
                ("transform", serializeTransform(node))
                ("backgroundColor", std::move(backgroundColor))
                ("boxShadow", serializePrimaryBoxShadow(node))
                ("boxShadows", serializeBoxShadows(node))
                ("borderColor", serializeColor(
                    node.hasBorderColor,
                    node.borderRed,
                    node.borderGreen,
                    node.borderBlue,
                    node.borderAlpha))
                ("borderColors", serializeBorderColors(node))
                ("borderStyles", serializeBorderStyles(node))
                ("outline", node.outlineWidth > 0 || node.hasOutlineColor
                    ? folly::dynamic::object
                        ("width", node.outlineWidth)
                        ("offset", node.outlineOffset)
                        ("style", node.outlineStyle)
                        ("color", serializeColor(
                            node.hasOutlineColor,
                            node.outlineRed,
                            node.outlineGreen,
                            node.outlineBlue,
                            node.outlineAlpha))
                    : folly::dynamic(nullptr))
                ("borderRadius", node.borderRadius)
                ("borderRadii", serializeBorderRadii(node))
                ("backfaceHidden", node.backfaceHidden)
                ("needsOffscreenAlphaCompositing",
                 node.needsOffscreenAlphaCompositing)
                ("nativeRipple", serializeNativeRipple(node))
                ("zIndex", node.zIndex ? folly::dynamic(*node.zIndex)
                                        : folly::dynamic(nullptr))
                ("pointerEvents", node.pointerEvents)
                ("clipsContentToBounds", node.clipsContentToBounds)
                ("hitSlop", folly::dynamic::object
                    ("top", node.hitSlopTop)
                    ("right", node.hitSlopRight)
                    ("bottom", node.hitSlopBottom)
                    ("left", node.hitSlopLeft))
                ("collapsable", node.collapsable)
                ("activityIndicator", node.activityIndicator
                    ? folly::dynamic::object
                        ("animating", node.activityIndicatorAnimating)
                        ("hidesWhenStopped", node.activityIndicatorHidesWhenStopped)
                        ("horizontal", node.activityIndicatorHorizontal)
                        ("progress", node.activityIndicatorProgress)
                        ("color", node.hasActivityIndicatorColor
                            ? folly::dynamic::object
                                ("red", node.activityIndicatorRed)
                                ("green", node.activityIndicatorGreen)
                                ("blue", node.activityIndicatorBlue)
                                ("alpha", node.activityIndicatorAlpha)
                            : folly::dynamic(nullptr))
                    : folly::dynamic(nullptr))
                ("androidSwitch", node.androidSwitch
                    ? folly::dynamic::object
                        ("on", node.androidSwitchOn)
                        ("enabled", node.androidSwitchEnabled)
                        ("thumbColor", node.hasSwitchThumbColor
                            ? folly::dynamic::object
                                ("red", node.switchThumbRed)
                                ("green", node.switchThumbGreen)
                                ("blue", node.switchThumbBlue)
                                ("alpha", node.switchThumbAlpha)
                            : folly::dynamic(nullptr))
                        ("trackColor", node.hasSwitchTrackColor
                            ? folly::dynamic::object
                                ("red", node.switchTrackRed)
                                ("green", node.switchTrackGreen)
                                ("blue", node.switchTrackBlue)
                                ("alpha", node.switchTrackAlpha)
                            : folly::dynamic(nullptr))
                    : folly::dynamic(nullptr))
                ("modal", node.modalHost
                    ? folly::dynamic::object
                        ("transparent", node.modalTransparent)
                    : folly::dynamic(nullptr))
                ("custom", std::move(customProps))
                ("text", node.text)
                ("fontSize", node.fontSize)
                ("fontWeight", node.fontWeight)
                ("fontFamily", node.fontFamily)
                ("preparedText", node.preparedText != nullptr)
#if RNS_ENABLE_SKIA
                ("preparedTextMeasured", node.preparedText != nullptr &&
                    node.preparedText->wasMeasured())
#else
                ("preparedTextMeasured", false)
#endif
                ("lineHeight", node.hasExplicitLineHeight
                    ? folly::dynamic(node.lineHeight)
                    : folly::dynamic(nullptr))
                ("includeFontPadding", node.includeFontPadding)
                ("textAlignVertical", node.textAlignVertical)
                ("subpixelText", node.subpixelText)
                ("textColor", node.hasTextColor
                    ? folly::dynamic::object
                        ("red", node.textRed)
                        ("green", node.textGreen)
                        ("blue", node.textBlue)
                        ("alpha", node.textAlpha)
                    : folly::dynamic(nullptr))
                ("imageUri", node.imageUri)
                ("imagePath", node.imagePath)
                ("imageResizeMode", node.imageResizeMode)
                ("imageTint", node.hasImageTint
                    ? folly::dynamic::object
                        ("red", node.imageTintRed)
                        ("green", node.imageTintGreen)
                        ("blue", node.imageTintBlue)
                        ("alpha", node.imageTintAlpha)
                    : folly::dynamic(nullptr))
                ("textInput", node.textInput
                    ? folly::dynamic::object
                        ("editable", node.editable)
                        ("multiline", node.multiline)
                        ("focused", node.focused)
                        ("placeholder", node.placeholder)
                        ("selectionStart", node.selectionStart)
                        ("selectionEnd", node.selectionEnd)
                    : folly::dynamic(nullptr))
                ("scroll", node.scrollable
                    ? folly::dynamic::object
                        ("offsetX", node.scrollOffsetX)
                        ("offsetY", node.scrollOffsetY)
                        ("contentWidth", node.scrollContentWidth)
                        ("contentHeight", node.scrollContentHeight)
                    : folly::dynamic(nullptr))));
      }
      return nodes;
    };
    folly::dynamic shadowTreeMetadata = folly::dynamic::object
        ("surfaceId", reactFabric.shadowTreeSurfaceId)
        ("revision", reactFabric.shadowTreeRevision)
        ("rootTag", reactFabric.shadowTreeRootTag)
        ("nodeCount", reactFabric.shadowTreeNodes.size())
        ("nodes", serializeViewNodes(reactFabric.shadowTreeNodes));
    folly::dynamic mountingErrors = folly::dynamic::array;
    for (const auto& error : reactFabric.mountingErrors) {
      mountingErrors.push_back(error);
    }
    folly::dynamic mountedViewTreeMetadata = folly::dynamic::object
        ("surfaceId", reactFabric.shadowTreeSurfaceId)
        ("revision", reactFabric.mountingRevision)
        ("rootTag", reactFabric.mountedRootTag)
        ("nodeCount", reactFabric.mountedViewNodes.size())
        ("errors", std::move(mountingErrors))
        ("nodes", serializeViewNodes(reactFabric.mountedViewNodes));
    folly::dynamic componentCapabilities = frameworkComponentCapabilities;
    for (const auto& capability : addonRegistry.componentCapabilities()) {
      componentCapabilities[capability.name] = capability.fidelity;
    }
    folly::dynamic workloadMarkMetadata = folly::dynamic::array;
    for (const auto& mark : workloadMarks) {
      workloadMarkMetadata.push_back(folly::dynamic::object
          ("name", mark.name)
          ("ms", std::chrono::duration<double, std::milli>(
                     mark.timestamp - bundleStart)
                     .count()));
    }
    folly::dynamic jsErrorMetadata = folly::dynamic::array;
    for (const auto& error : jsErrors) {
      folly::dynamic stack = folly::dynamic::array;
      for (const auto& frame : error.stack) {
        stack.push_back(folly::dynamic::object
            ("file", frame.file ? folly::dynamic(*frame.file)
                                : folly::dynamic(nullptr))
            ("method", frame.methodName)
            ("line", frame.lineNumber ? folly::dynamic(*frame.lineNumber)
                                      : folly::dynamic(nullptr))
            ("column", frame.column ? folly::dynamic(*frame.column)
                                    : folly::dynamic(nullptr)));
      }
      jsErrorMetadata.push_back(folly::dynamic::object
          ("message", error.message)
          ("fatal", error.fatal)
          ("ms", std::chrono::duration<double, std::milli>(
                     error.timestamp - bundleStart)
                     .count())
          ("stack", std::move(stack)));
    }
    // Conformance is fail-closed even for embedding callers. The public CLI
    // remains disabled until canonical profile/font/oracle manifests exist,
    // but constructing this mode must never degrade to ordinary workload
    // validation or produce a false-positive pass.
    const bool requireReactFabric =
        conformanceMode || options.requireReactFabric;
    const bool requireNoPendingWork =
        conformanceMode || options.requireNoPendingWork;
    const bool failOnComponentFallback =
        conformanceMode || options.failOnComponentFallback;
    const bool reactFabricRequirementPassed =
        !requireReactFabric || reactFabric.passed;
    const bool pendingWorkRequirementPassed =
        !requireNoPendingWork || !pendingWork;
    const bool componentFallbackRequirementPassed =
        !failOnComponentFallback ||
        reactFabric.fallbackComponentNames.empty();
    const bool requirementsPassed = reactFabricRequirementPassed &&
        pendingWorkRequirementPassed && componentFallbackRequirementPassed;
    const folly::dynamic traceFileMetadata = options.tracePath
        ? folly::dynamic(options.tracePath->string())
        : folly::dynamic(nullptr);
    std::ostringstream metrics;
    metrics << "{\"host\":\"react-native-simulator\","
              << "\"schemaVersion\":2,"
              << "\"engine\":\"Hermes\","
              << "\"reactNativeVersion\":\"" RNS_REACT_NATIVE_VERSION "\","
              << "\"reactVersion\":\"external-bundle\","
              << "\"reactPeerRange\":\"" RNS_REACT_PEER_RANGE "\","
              << "\"hermesVersion\":\"" RNS_HERMES_VERSION "\","
              << "\"workload\":\"" << options.workload << "\","
              << "\"validationMode\":\""
              << (developmentMode
                      ? "development"
                      : (conformanceMode
                             ? "conformance"
                             : (workloadResultPresent ? "workload" : "runtime")))
              << "\","
              << "\"profile\":\"" << options.profile << "\","
              << "\"platformProfile\":\"" << runtimeProfile->platform()
              << "\","
              << "\"bundleTargetReactNativeVersion\":\""
              << runtimeProfile->bundleTargetReactNativeVersion() << "\","
              << "\"compatibilityLevel\":\""
              << runtimeProfile->compatibilityLevel() << "\","
              << "\"viewport\":{\"width\":" << options.viewportWidth
              << ",\"height\":" << options.viewportHeight
              << ",\"pointScaleFactor\":" << options.pointScaleFactor
              << "},"
              << "\"addons\":" << folly::toJson(addonMetadata) << ','
              << "\"rnFrameworkModules\":"
              << folly::toJson(frameworkModuleMetadata) << ','
              << "\"rnFrameworkComponents\":"
              << folly::toJson(frameworkComponentCapabilities) << ','
              << "\"addonModules\":"
              << folly::toJson(addonModuleMetadata) << ','
              << "\"nativeCapabilities\":{\"modules\":"
              << folly::toJson(moduleCapabilities)
              << ",\"text\":"
              << folly::toJson(textCapabilities)
              << ",\"fallbackComponents\":"
              << folly::toJson(fallbackComponents)
              << ",\"components\":"
              << folly::toJson(componentCapabilities) << "},"
              << "\"requirements\":{"
              << "\"requireReactFabric\":"
              << (requireReactFabric ? "true" : "false") << ','
              << "\"requireNoPendingWork\":"
              << (requireNoPendingWork ? "true" : "false") << ','
              << "\"failOnComponentFallback\":"
              << (failOnComponentFallback ? "true" : "false") << ','
              << "\"passed\":"
              << (requirementsPassed ? "true" : "false") << "},"
              << "\"workloadMarks\":"
              << folly::toJson(workloadMarkMetadata) << ','
              << "\"traceFile\":" << folly::toJson(traceFileMetadata) << ','
              << "\"seed\":" << options.seed << ','
              << "\"bundleBytes\":" << bundleBytes << ','
              << "\"bundleHash\":\"" << bundleHash << "\","
              << "\"bundleAPI\":true,"
              << "\"bundlesRequested\":" << requestedBundleCount << ','
              << "\"bundlesLoaded\":" << bundlesLoaded << ','
              << "\"bundleLoadFailed\":"
              << (bundleLoadFailed ? "true" : "false") << ','
              << "\"bundles\":" << folly::toJson(bundleMetadata) << ','
              << "\"rnBridgeless\":true,"
              << "\"runtimeScheduler\":"
              << (schedulerInstalled ? "true" : "false") << ','
              << "\"fabric\":" << (fabric.passed ? "true" : "false")
              << ",\"yoga\":" << (fabric.passed ? "true" : "false")
              << ",\"fabricTransactions\":" << fabric.transactions
              << ",\"fabricCreates\":" << fabric.creates
              << ",\"fabricInserts\":" << fabric.inserts
              << ",\"fabricUpdates\":" << fabric.updates
              << ",\"reactFabric\":"
              << (reactFabric.passed ? "true" : "false")
              << ",\"reactFabricTransactions\":"
              << reactFabric.transactions
              << ",\"reactFabricCreates\":" << reactFabric.creates
              << ",\"reactFabricInserts\":" << reactFabric.inserts
              << ",\"reactFabricUpdates\":" << reactFabric.updates
              << ",\"reactFabricRemoves\":" << reactFabric.removes
              << ",\"reactFabricDeletes\":" << reactFabric.deletes
              << ",\"shadowTree\":" << folly::toJson(shadowTreeMetadata)
              << ",\"mountedViewTree\":"
              << folly::toJson(mountedViewTreeMetadata)
              << ",\"reactYoga\":"
              << (reactFabric.hasExpectedYogaWidths ? "true" : "false")
              << ",\"eventDispatcher\":"
              << (reactFabric.eventDispatcherInstalled ? "true" : "false")
              << ",\"turboModules\":true"
              << ",\"customNativeModule\":"
              << (nativeModulePassed ? "true" : "false")
              << ",\"customNativeComponent\":"
              << (nativeComponentPassed ? "true" : "false")
              << ",\"customNativeCommands\":"
              << reactFabric.customCommands
              << ",\"mockedNativeComponents\":"
              << reactFabric.mockedComponents
              << ",\"yogaWidths\":[" << fabric.firstWidth << ','
              << fabric.firstFlexWidth << ',' << fabric.updatedWidth << ','
              << fabric.updatedFlexWidth << "],"
              << "\"bundleLoaded\":"
              << (bundleLoaded ? "true" : "false") << ','
              << "\"workloadReady\":"
              << (workloadReady ? "true" : "false") << ','
              << "\"workloadComplete\":"
              << (workloadComplete ? "true" : "false") << ','
              << "\"workloadTimedOut\":"
              << (workloadTimedOut ? "true" : "false") << ','
              << "\"pendingWork\":"
              << (pendingWork ? "true" : "false") << ','
              << "\"reactInstance\":true,"
              << "\"devToolsEnabled\":"
              << (devTools ? "true" : "false") << ','
              << "\"devToolsSessionAttached\":"
              << (devTools && devTools->hadSession() ? "true" : "false")
              << ','
              << "\"timerManager\":true,"
              << "\"timersPassed\":"
              << (timersPassed ? "true" : "false") << ','
              << "\"workloadIterations\":" << workload.iterations << ','
              << "\"renderIterations\":" << workload.renderIterations << ','
              << "\"cpuIterations\":" << workload.cpuIterations << ','
              << "\"workloadChecksum\":" << workload.checksum << ','
              << "\"renderMs\":" << workload.renderMs << ','
              << "\"cpuMs\":" << workload.cpuMs << ','
              << "\"commitMs\":" << reactFabric.commitMs << ','
              << "\"layoutMs\":" << reactFabric.layoutMs << ','
              << "\"diffMs\":" << reactFabric.diffMs << ','
              << "\"initializationMs\":"
              << milliseconds(processStart, initializationEnd) << ','
              << "\"bundleAndDrainMs\":"
              << milliseconds(bundleStart, bundleEnd) << ','
              << "\"heapAllocatedBytes\":"
              << runtimeMetrics.heapAllocatedBytes << ','
              << "\"heapSizeBytes\":" << runtimeMetrics.heapSizeBytes << ','
              << "\"heapExternalBytes\":"
              << runtimeMetrics.heapExternalBytes << ','
              << "\"heapTotalAllocatedBytes\":"
              << runtimeMetrics.heapTotalAllocatedBytes << ','
              << "\"gcCollections\":" << runtimeMetrics.gcCollections << ','
              << "\"gcTotalMs\":" << runtimeMetrics.gcTotalMs << ','
              << "\"gcMaxPauseMs\":" << runtimeMetrics.gcMaxPauseMs << ','
              << "\"gcCpuMs\":" << runtimeMetrics.gcCpuMs << ','
              << "\"residentBytes\":" << runtimeMetrics.residentBytes << ','
              << "\"peakResidentBytes\":"
              << runtimeMetrics.peakResidentBytes << ','
              << "\"processUserCpuMs\":"
              << runtimeMetrics.processUserCpuMs << ','
              << "\"processSystemCpuMs\":"
              << runtimeMetrics.processSystemCpuMs << ','
              << "\"workloadHeapAllocatedDeltaBytes\":"
              << workloadRuntimeMetrics.heapAllocatedBytes << ','
              << "\"workloadHeapSizeDeltaBytes\":"
              << workloadRuntimeMetrics.heapSizeBytes << ','
              << "\"workloadHeapExternalDeltaBytes\":"
              << workloadRuntimeMetrics.heapExternalBytes << ','
              << "\"workloadHeapTotalAllocatedBytes\":"
              << workloadRuntimeMetrics.heapTotalAllocatedBytes << ','
              << "\"workloadGcCollections\":"
              << workloadRuntimeMetrics.gcCollections << ','
              << "\"workloadGcTotalMs\":"
              << workloadRuntimeMetrics.gcTotalMs << ','
              << "\"workloadGcCpuMs\":"
              << workloadRuntimeMetrics.gcCpuMs << ','
              << "\"workloadResidentGrowthBytes\":"
              << workloadRuntimeMetrics.residentBytes << ','
              << "\"workloadPeakResidentGrowthBytes\":"
              << workloadRuntimeMetrics.peakResidentBytes << ','
              << "\"workloadUserCpuMs\":"
              << workloadRuntimeMetrics.processUserCpuMs << ','
              << "\"workloadSystemCpuMs\":"
              << workloadRuntimeMetrics.processSystemCpuMs << ','
              << "\"eventLoopTasks\":" << eventLoopTasks << ','
              << "\"jsErrors\":" << jsErrorCount << ','
              << "\"jsErrorDetails\":"
              << folly::toJson(jsErrorMetadata) << "}\n";
    const auto metricsJson = metrics.str();
    auto scene = makeSceneSnapshot(
        options,
        reactFabric,
        static_cast<std::uint64_t>(sessionGeneration));
    if (options.onSceneUpdate &&
        lastPublishedSceneRevision != scene->revision) {
      options.onSceneUpdate(scene);
    }
    const auto lifecyclePassed =
        developmentMode || (workloadReady && workloadComplete);
    const auto exitCode = bundleLoaded && lifecyclePassed &&
            !workloadTimedOut && schedulerInstalled && fabric.passed &&
            requirementsPassed && jsErrorCount == 0
        ? 0
        : 1;
    std::string failureReason;
    if (exitCode != 0) {
      if (!jsErrors.empty()) {
        failureReason = jsErrors.front().message;
      } else if (bundleLoadFailed && !bundleRecords.empty()) {
        failureReason = bundleRecords.back().error;
      } else if (workloadTimedOut) {
        failureReason = "Workload timed out after " +
            std::to_string(options.timeoutMs) + "ms";
      } else if (!fabric.passed) {
        failureReason = fabric.error;
      } else if (!requirementsPassed) {
        failureReason = "Runtime requirements failed";
      } else if (!lifecyclePassed) {
        failureReason = "Workload lifecycle did not complete";
      } else {
        failureReason = "Runtime validation failed";
      }
    }
    if (inspectorTransport) {
      inspectorTransport->sendJson(folly::toJson(folly::dynamic::object
          ("type", "snapshot")
          ("state", "complete")
          ("metrics", folly::parseJson(metricsJson))
          ("scene", makeSceneWireSnapshot(options, reactFabric))));
      inspectorTransport->sendJson(folly::toJson(folly::dynamic::object
          ("type", "exit")
          ("code", exitCode)));
      inspectorTransport->waitForDisconnect();
    }
    if (!fabric.passed) {
      std::cerr << "Fabric/Yoga verification failed: " << fabric.error << '\n';
    }
    if (workloadTimedOut) {
      std::cerr << "Workload timed out after " << options.timeoutMs << "ms"
                << (pendingWork ? " with pending work" : "") << '\n';
    }
    if (!requirementsPassed) {
      std::cerr << "Runtime requirements failed:"
                << (!reactFabricRequirementPassed ? " react-fabric" : "")
                << (!pendingWorkRequirementPassed ? " pending-work" : "")
                << (!componentFallbackRequirementPassed
                        ? " component-fallback"
                        : "")
                << '\n';
    }

    if (!developmentMode) {
      if (devTools && options.devTools.waitForDisconnect &&
          devTools->hasSession()) {
        while (devTools->hasSession()) {
          eventLoop->runFor(std::chrono::milliseconds(10));
        }
      } else if (devTools && options.devTools.keepAliveMs > 0) {
        eventLoop->runFor(
            std::chrono::milliseconds(options.devTools.keepAliveMs));
      }
    }
    hostChrome().onInvalidate = nullptr;
    headlessKeyboard().emit = nullptr;
    headlessBackPress().emit = nullptr;
    headlessBackPress().invokeDefault = nullptr;
    headlessBackPress().runOnJs = nullptr;
    hostEnvironment().reset();
    headlessWebSocketReset();
    headlessBlobReset();
    headlessImageRequestsReset();
    reactFabricHost.reset();
    turboModuleCache.clear();
    // ReactInstance must be the final TimerManager owner so its member
    // destruction releases pending JSI timer callbacks before the runtime.
    timerManager.reset();
    if (devTools) {
      instance->unregisterFromInspector();
    }
    instance.reset();
    devTools.reset();
    eventLoop->quitSynchronous();
    return {
        .exitCode = exitCode,
        .error = std::move(failureReason),
        .metricsJson = metricsJson,
        .scene = std::move(scene),
    };
    }
  } catch (const std::exception& error) {
    if (inspectorTransport) {
      inspectorTransport->sendJson(folly::toJson(folly::dynamic::object
          ("type", "error")
          ("message", error.what())));
    }
    return {.exitCode = 1, .error = error.what()};
  }
}
