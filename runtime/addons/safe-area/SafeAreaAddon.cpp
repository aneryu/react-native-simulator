#include <react-native-simulator/SimulatorAddon.h>

#include <react/nativemodule/core/ReactCommon/TurboModule.h>
#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>

#include <folly/dynamic.h>

#include <memory>
#include <unordered_map>
#include <utility>

namespace jsi = facebook::jsi;
namespace react = facebook::react;
using ReactNativeSimulator::AddonComponentKind;
using ReactNativeSimulator::AddonFabricRegistrar;
using ReactNativeSimulator::AddonGenerationContext;
using ReactNativeSimulator::AddonHost;
using ReactNativeSimulator::AddonHostSnapshot;
using ReactNativeSimulator::AddonManifest;
using ReactNativeSimulator::AddonMountKind;
using ReactNativeSimulator::AddonRole;
using ReactNativeSimulator::RuntimeCapabilityClass;
using ReactNativeSimulator::SimulatorAddon;

namespace {
const char kProviderName[] = "RNCSafeAreaProvider";
const char kViewName[] = "RNCSafeAreaView";

using ProviderShadowNode =
    react::ConcreteViewShadowNode<kProviderName, react::ViewProps, react::ViewEventEmitter>;
using ProviderDescriptor = react::ConcreteComponentDescriptor<ProviderShadowNode>;
using ViewShadowNode =
    react::ConcreteViewShadowNode<kViewName, react::ViewProps, react::ViewEventEmitter>;
using ViewDescriptor = react::ConcreteComponentDescriptor<ViewShadowNode>;

class RNCSafeAreaContextModule final : public react::TurboModule {
 public:
  RNCSafeAreaContextModule(
      std::shared_ptr<react::CallInvoker> jsInvoker,
      AddonHostSnapshot snapshot)
      : TurboModule("RNCSafeAreaContext", std::move(jsInvoker)),
        snapshot_(std::move(snapshot)) {
    methodMap_["getConstants"] = {0, &getConstants};
  }

 private:
  AddonHostSnapshot snapshot_;

  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    const auto& self = static_cast<RNCSafeAreaContextModule&>(module);
    jsi::Object insets(runtime);
    insets.setProperty(runtime, "top", self.snapshot_.viewport.insetTop);
    insets.setProperty(runtime, "right", self.snapshot_.viewport.insetRight);
    insets.setProperty(runtime, "bottom", self.snapshot_.viewport.insetBottom);
    insets.setProperty(runtime, "left", self.snapshot_.viewport.insetLeft);
    jsi::Object frame(runtime);
    frame.setProperty(runtime, "x", 0);
    frame.setProperty(runtime, "y", 0);
    frame.setProperty(runtime, "width", self.snapshot_.viewport.width);
    frame.setProperty(runtime, "height", self.snapshot_.viewport.height);
    jsi::Object metrics(runtime);
    metrics.setProperty(runtime, "insets", std::move(insets));
    metrics.setProperty(runtime, "frame", std::move(frame));
    jsi::Object constants(runtime);
    constants.setProperty(runtime, "initialWindowMetrics", std::move(metrics));
    return constants;
  }
};

class SafeAreaAddon final : public SimulatorAddon {
 public:
  AddonManifest manifest() const override {
    AddonManifest manifest;
    manifest.name = "safe-area";
    manifest.addonVersion = "1.0.0";
    manifest.role = AddonRole::Community;
    manifest.modules = {{
        "RNCSafeAreaContext",
        RuntimeCapabilityClass::HostAdapted,
        "window-relative-insets",
    }};
    manifest.components = {
        {
            "RNCSafeAreaProvider",
            RuntimeCapabilityClass::HostAdapted,
            AddonComponentKind::FabricDescriptor,
            {"topInsetsChange"},
            {},
            "window-relative-insets",
        },
        {
            "RNCSafeAreaView",
            RuntimeCapabilityClass::LayoutOnly,
            AddonComponentKind::FabricDescriptor,
            {},
            {},
            "layout-only-until-edges",
        },
    };
    return manifest;
  }

  void bind(const AddonHost& host) override {
    snapshot_ = host.snapshot();
  }
  void unbind() noexcept override {}

  std::shared_ptr<react::TurboModule> getTurboModule(
      const AddonGenerationContext&,
      jsi::Runtime&,
      const std::string& moduleName,
      const std::shared_ptr<react::CallInvoker>& jsInvoker) override {
    if (moduleName == "RNCSafeAreaContext") {
      return std::make_shared<RNCSafeAreaContextModule>(jsInvoker, snapshot_);
    }
    return nullptr;
  }

  std::shared_ptr<react::TurboModule> wrapTurboModule(
      const AddonGenerationContext&,
      jsi::Runtime&,
      const std::string&,
      std::shared_ptr<react::TurboModule> framework,
      const std::shared_ptr<react::CallInvoker>&) override {
    return framework;
  }

  void configureFabric(
      const AddonGenerationContext&,
      AddonFabricRegistrar& registrar) override {
    registrar.registerDescriptor(
        react::concreteComponentDescriptorProvider<ProviderDescriptor>());
    registrar.registerDescriptor(
        react::concreteComponentDescriptorProvider<ViewDescriptor>());
    registrar.onMount(
        "RNCSafeAreaProvider",
        [this](AddonMountKind kind, const ReactNativeSimulator::AddonMountedNode& node) {
          if (kind == AddonMountKind::Unmounted) {
            lastFrames_.erase(node.tag);
            return;
          }
          const auto& frame = node.layoutMetrics.frame;
          auto found = lastFrames_.find(node.tag);
          const bool changed = kind == AddonMountKind::Mounted ||
              found == lastFrames_.end() ||
              found->second.origin.x != frame.origin.x ||
              found->second.origin.y != frame.origin.y ||
              found->second.size.width != frame.size.width ||
              found->second.size.height != frame.size.height;
          lastFrames_[node.tag] = frame;
          if (!changed || node.shadowNode == nullptr ||
              node.shadowNode->getEventEmitter() == nullptr) {
            return;
          }
          node.shadowNode->getEventEmitter()->dispatchEvent(
              "topInsetsChange",
              folly::dynamic::object
                  ("insets",
                   folly::dynamic::object
                       ("top", 0)("right", 0)("bottom", 0)("left", 0))
                  ("frame",
                   folly::dynamic::object
                       ("x", frame.origin.x)("y", frame.origin.y)
                       ("width", frame.size.width)
                       ("height", frame.size.height)));
        });
  }

  void installJSI(
      const AddonGenerationContext&,
      jsi::Runtime&,
      const std::shared_ptr<react::CallInvoker>&) override {}
  void hostSnapshotChanged(const AddonHostSnapshot&) override {}
  void quiesceGeneration(std::uint64_t) noexcept override {
    lastFrames_.clear();
  }

 private:
  AddonHostSnapshot snapshot_;
  std::unordered_map<react::Tag, react::Rect> lastFrames_;
};
} // namespace

std::unique_ptr<SimulatorAddon> createSafeAreaAddon() {
  return std::make_unique<SafeAreaAddon>();
}
