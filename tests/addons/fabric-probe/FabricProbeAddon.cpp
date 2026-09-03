#include <react-native-simulator/SimulatorAddon.h>

#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/core/ConcreteState.h>

#include <folly/dynamic.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace react = facebook::react;
namespace jsi = facebook::jsi;
using ReactNativeSimulator::AddonComponentKind;
using ReactNativeSimulator::AddonFabricRegistrar;
using ReactNativeSimulator::AddonGenerationContext;
using ReactNativeSimulator::AddonHost;
using ReactNativeSimulator::AddonHostSnapshot;
using ReactNativeSimulator::AddonManifest;
using ReactNativeSimulator::AddonMountKind;
using ReactNativeSimulator::AddonRole;
using ReactNativeSimulator::AddonRuntimeExecutor;
using ReactNativeSimulator::RuntimeCapabilityClass;
using ReactNativeSimulator::SimulatorAddon;

namespace {
const char kProbeName[] = "RNSFabricProbeView";

struct ProbeState {
  int value{0};
#ifdef RN_SERIALIZABLE_STATE
  ProbeState() = default;
  ProbeState(const ProbeState& previous, folly::dynamic data) : value(previous.value) {
    if (data.isObject() && data.count("value") && data["value"].isNumber()) {
      value = static_cast<int>(data["value"].asDouble());
    }
  }
  folly::dynamic getDynamic() const {
    return folly::dynamic::object("value", value);
  }
#endif
};

using ProbeShadowNode = react::ConcreteViewShadowNode<
    kProbeName,
    react::ViewProps,
    react::ViewEventEmitter,
    ProbeState>;
using ProbeDescriptor = react::ConcreteComponentDescriptor<ProbeShadowNode>;

class FabricProbeAddon final : public SimulatorAddon {
 public:
  AddonManifest manifest() const override {
    AddonManifest manifest;
    manifest.name = "fabric-probe";
    manifest.addonVersion = "1.0.0";
    manifest.role = AddonRole::Application;
    manifest.components = {{
        "RNSFabricProbeView",
        RuntimeCapabilityClass::HostAdapted,
        AddonComponentKind::FabricDescriptor,
        {"topProbeEvent"},
        {"setNativeValue"},
        "abi4-fabric-module-probe",
    }};
    return manifest;
  }

  void bind(const AddonHost&) override {}
  void unbind() noexcept override {}
  std::shared_ptr<react::TurboModule> getTurboModule(
      const AddonGenerationContext&,
      facebook::jsi::Runtime&,
      const std::string&,
      const std::shared_ptr<react::CallInvoker>&) override {
    return nullptr;
  }
  std::shared_ptr<react::TurboModule> wrapTurboModule(
      const AddonGenerationContext&,
      facebook::jsi::Runtime&,
      const std::string&,
      std::shared_ptr<react::TurboModule> framework,
      const std::shared_ptr<react::CallInvoker>&) override {
    return framework;
  }
  void configureFabric(
      const AddonGenerationContext&,
      AddonFabricRegistrar& registrar) override {
    registrar.registerDescriptor(
        react::concreteComponentDescriptorProvider<ProbeDescriptor>());
    registrar.onMount(
        "RNSFabricProbeView",
        [mounted = mounted_, unmounted = unmounted_, updated = updated_](
            AddonMountKind kind, const auto&) {
          if (kind == AddonMountKind::Mounted) {
            mounted->fetch_add(1);
          } else if (kind == AddonMountKind::Unmounted) {
            unmounted->fetch_add(1);
          } else {
            updated->fetch_add(1);
          }
        });
    registrar.onCommand(
        "RNSFabricProbeView",
        "setNativeValue",
        [](const auto& node, std::string_view, const folly::dynamic& args) {
          int value = 0;
          if (args.isArray() && !args.empty() && args[0].isNumber()) {
            value = static_cast<int>(args[0].asDouble());
          } else if (args.isNumber()) {
            value = static_cast<int>(args.asDouble());
          }
          auto probe =
              std::dynamic_pointer_cast<const ProbeShadowNode>(node.shadowNode);
          if (!probe) {
            return;
          }
          auto state = std::static_pointer_cast<const react::ConcreteState<ProbeState>>(
              probe->getState());
          ProbeState next = probe->getStateData();
          next.value = value;
          state->updateState(std::move(next));
          probe->getConcreteEventEmitter().dispatchEvent(
              "topProbeEvent", folly::dynamic::object("value", value));
        });
  }
  void installJSI(
      const AddonGenerationContext& context,
      facebook::jsi::Runtime& runtime,
      const std::shared_ptr<react::CallInvoker>&) override {
    executor_ = context.executor;
    runtime.global().setProperty(
        runtime,
        "__rnsProbeMounts",
        jsi::Function::createFromHostFunction(
            runtime,
            jsi::PropNameID::forAscii(runtime, "__rnsProbeMounts"),
            0,
            [mounted = mounted_, unmounted = unmounted_, updated = updated_](
                jsi::Runtime& runtime,
                const jsi::Value&,
                const jsi::Value*,
                size_t) {
              auto object = jsi::Object(runtime);
              object.setProperty(
                  runtime, "mounted", static_cast<int>(mounted->load()));
              object.setProperty(
                  runtime, "unmounted", static_cast<int>(unmounted->load()));
              object.setProperty(
                  runtime, "updated", static_cast<int>(updated->load()));
              return object;
            }));
    std::thread([executor = context.executor]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      executor.post([](jsi::Runtime& runtime) {
        runtime.global().setProperty(
            runtime, "__rnsProbeExecutorPosted", true);
      });
    }).detach();
  }
  void hostSnapshotChanged(const AddonHostSnapshot&) override {}
  void quiesceGeneration(std::uint64_t) noexcept override {
    executor_ = {};
  }

 private:
  AddonRuntimeExecutor executor_{};
  std::shared_ptr<std::atomic<int>> mounted_{std::make_shared<std::atomic<int>>(0)};
  std::shared_ptr<std::atomic<int>> unmounted_{std::make_shared<std::atomic<int>>(0)};
  std::shared_ptr<std::atomic<int>> updated_{std::make_shared<std::atomic<int>>(0)};
};
} // namespace

std::unique_ptr<SimulatorAddon> createFabricProbeAddon() {
  return std::make_unique<FabricProbeAddon>();
}
