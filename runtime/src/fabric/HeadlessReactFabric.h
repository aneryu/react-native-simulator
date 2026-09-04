#pragma once

#include <ReactCommon/RuntimeExecutor.h>
#include <jsi/jsi.h>

#include <cstddef>
#include <chrono>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <react-native-simulator/SimulatorAddon.h>
#include <react-native-simulator/Scene.h>
#include <react-native-simulator/Interaction.h>
#include <react/renderer/componentregistry/ComponentDescriptorProvider.h>

namespace facebook::react {
class ComponentDescriptorProviderRegistry;
class ContextContainer;
class UIManager;
class RuntimeScheduler;
}

struct HeadlessReactFabricResult {
  struct TraceSpan {
    std::string name;
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
    std::size_t transaction{0};
  };
  bool passed{false};
  std::size_t transactions{0};
  std::size_t creates{0};
  std::size_t inserts{0};
  std::size_t updates{0};
  std::size_t removes{0};
  std::size_t deletes{0};
  bool hasExpectedYogaWidths{false};
  bool eventDispatcherInstalled{false};
  double commitMs{0};
  double layoutMs{0};
  double diffMs{0};
  bool customComponentCreated{false};
  int customComponentValue{0};
  std::string customComponentLabel;
  std::size_t customCommands{0};
  std::size_t mockedComponents{0};
  std::vector<std::string> fallbackComponentNames;
  std::vector<std::string> addonMockComponentNames;
  std::vector<TraceSpan> traceSpans;
  int shadowTreeSurfaceId{0};
  std::int64_t shadowTreeRevision{0};
  int shadowTreeRootTag{0};
  std::vector<ReactNativeSimulator::SceneNode> shadowTreeNodes;
  std::int64_t mountingRevision{0};
  int mountedRootTag{0};
  std::vector<ReactNativeSimulator::SceneNode> mountedViewNodes;
  std::vector<std::string> mountingErrors;
  std::string error;
  std::size_t staleCommands{0};
  std::size_t unknownCommands{0};
};

struct AddonFabricHostBindings {
  std::uint64_t generation{0};
  std::unordered_map<std::string, ReactNativeSimulator::AddonMountHandler>
      mountHandlers;
  std::unordered_map<
      std::string,
      std::unordered_map<std::string, ReactNativeSimulator::AddonCommandHandler>>
      commandHandlers;
  std::unordered_map<std::string, std::string> componentOwners;
  std::function<void(std::exception_ptr)> reportFatal;
};

class HeadlessReactFabricHost;
class SimulatorEventLoop;

using HeadlessReactFabricUpdate =
    std::function<void(const HeadlessReactFabricResult&)>;

std::shared_ptr<HeadlessReactFabricHost> installHeadlessReactFabric(
    facebook::jsi::Runtime& runtime,
    facebook::react::RuntimeExecutor runtimeExecutor,
    std::shared_ptr<facebook::react::RuntimeScheduler> runtimeScheduler,
    std::shared_ptr<SimulatorEventLoop> eventLoop,
    float viewportWidth,
    float viewportHeight,
    float pointScaleFactor,
    float insetTop,
    float insetBottom,
    const std::filesystem::path& fontDirectory,
    const std::filesystem::path& assetDirectory,
    const std::string& platform,
    std::vector<ReactNativeSimulator::AddonComponentDeclaration>
        addonComponents = {},
    std::vector<facebook::react::ComponentDescriptorProvider>
        addonProviders = {},
    HeadlessReactFabricUpdate onUpdate = {},
    AddonFabricHostBindings addonBindings = {});

void stopHeadlessReactFabricSurface(HeadlessReactFabricHost& host);
void shutdownHeadlessReactFabric(HeadlessReactFabricHost& host);
void setHeadlessReactFabricCallbacksEnabled(
    HeadlessReactFabricHost& host,
    bool enabled);

std::shared_ptr<facebook::react::UIManager> getHeadlessReactFabricUIManager();

HeadlessReactFabricResult getHeadlessReactFabricResult(
    const HeadlessReactFabricHost& host);

ReactNativeSimulator::InteractionResult dispatchHeadlessReactFabricAction(
    HeadlessReactFabricHost& host,
    const ReactNativeSimulator::InteractionAction& action,
    std::uint64_t sequence);
