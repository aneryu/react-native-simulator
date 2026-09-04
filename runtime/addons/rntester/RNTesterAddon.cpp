#include <react-native-simulator/SimulatorAddon.h>

#include <react/nativemodule/core/ReactCommon/TurboModuleUtils.h>

#include <memory>

namespace jsi = facebook::jsi;
namespace react = facebook::react;
using ReactNativeSimulator::AddonCommand;
using ReactNativeSimulator::AddonComponentDeclaration;
using ReactNativeSimulator::AddonComponentKind;
using ReactNativeSimulator::AddonGenerationContext;
using ReactNativeSimulator::AddonHost;
using ReactNativeSimulator::AddonHostSnapshot;
using ReactNativeSimulator::AddonManifest;
using ReactNativeSimulator::AddonModuleDeclaration;
using ReactNativeSimulator::AddonNumericConstant;
using ReactNativeSimulator::AddonRole;
using ReactNativeSimulator::AddonViewManagerConfig;
using ReactNativeSimulator::RuntimeCapabilityClass;
using ReactNativeSimulator::SimulatorAddon;

namespace {
class NativeCxxModuleExampleStub final : public react::TurboModule {
 public:
  explicit NativeCxxModuleExampleStub(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("NativeCxxModuleExampleCxx", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["voidFunc"] = {0, &undefinedResult};
    methodMap_["getBool"] = {1, &echoFirst};
    methodMap_["getNumber"] = {1, &echoFirst};
    methodMap_["getString"] = {1, &echoFirst};
    methodMap_["getValueWithPromise"] = {1, &resolvedString};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object constants(runtime);
    constants.setProperty(runtime, "const1", true);
    constants.setProperty(runtime, "const2", 69);
    constants.setProperty(
        runtime, "const3", jsi::String::createFromAscii(runtime, "react-native"));
    return constants;
  }

  static jsi::Value undefinedResult(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Value::undefined();
  }

  static jsi::Value echoFirst(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    return count > 0 ? jsi::Value(runtime, args[0]) : jsi::Value::undefined();
  }

  static jsi::Value resolvedString(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return react::createPromiseAsJSIValue(
        runtime,
        [](jsi::Runtime& runtime, std::shared_ptr<react::Promise> promise) {
          promise->resolve(
              jsi::String::createFromAscii(runtime, "rntester-stub"));
        });
  }
};

class ScreenshotManagerStub final : public react::TurboModule {
 public:
  explicit ScreenshotManagerStub(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ScreenshotManager", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {
        0,
        [](jsi::Runtime& runtime,
           react::TurboModule&,
           const jsi::Value*,
           size_t) -> jsi::Value { return jsi::Object(runtime); }};
    methodMap_["takeScreenshot"] = {
        2,
        [](jsi::Runtime& runtime,
           react::TurboModule&,
           const jsi::Value*,
           size_t) -> jsi::Value {
          return react::createPromiseAsJSIValue(
              runtime,
              [](jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
                promise->reject(
                    "ScreenshotManager.takeScreenshot is a tester stub");
              });
        }};
  }
};

class RNTesterAddon final : public SimulatorAddon {
 public:
  AddonManifest manifest() const override {
    AddonManifest manifest;
    manifest.name = "rntester";
    manifest.addonVersion = "1.0.0";
    manifest.role = AddonRole::Application;
    manifest.modules = {
        {"NativeCxxModuleExampleCxx", RuntimeCapabilityClass::Mocked, "tester-stub"},
        {"ScreenshotManager", RuntimeCapabilityClass::Mocked, "tester-stub"},
    };
    auto mock = [](const char* name) {
      return AddonComponentDeclaration{
          name,
          RuntimeCapabilityClass::Mocked,
          AddonComponentKind::DescriptorOnlyMock,
          {},
          {},
          "descriptor-only-mock"};
    };
    manifest.components = {
        mock("RNTReportFullyDrawnView"),
        mock("RNTMyNativeView"),
        mock("RNTMyLegacyNativeView"),
        mock("AndroidPopupMenu"),
    };
    manifest.viewManagerConfigs = {{
        .name = "RNTMyLegacyNativeView",
        .numericConstants = {AddonNumericConstant{.name = "PI", .value = 3.14}},
        .commands =
            {AddonCommand{.name = "changeBackgroundColor", .id = 1},
             AddonCommand{.name = "addOverlays", .id = 2},
             AddonCommand{.name = "removeOverlays", .id = 3}},
    }};
    return manifest;
  }

  void bind(const AddonHost&) override {}
  void unbind() noexcept override {}

  std::shared_ptr<react::TurboModule> getTurboModule(
      const AddonGenerationContext&,
      jsi::Runtime&,
      const std::string& moduleName,
      const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker) override {
    if (moduleName == "NativeCxxModuleExampleCxx") {
      return std::make_shared<NativeCxxModuleExampleStub>(jsInvoker);
    }
    if (moduleName == "ScreenshotManager") {
      return std::make_shared<ScreenshotManagerStub>(jsInvoker);
    }
    return nullptr;
  }

  std::shared_ptr<react::TurboModule> wrapTurboModule(
      const AddonGenerationContext&,
      jsi::Runtime&,
      const std::string&,
      std::shared_ptr<react::TurboModule> framework,
      const std::shared_ptr<facebook::react::CallInvoker>&) override {
    return framework;
  }

  void configureFabric(
      const AddonGenerationContext&,
      ReactNativeSimulator::AddonFabricRegistrar&) override {}
  void installJSI(
      const AddonGenerationContext&,
      jsi::Runtime&,
      const std::shared_ptr<facebook::react::CallInvoker>&) override {}
  void hostSnapshotChanged(const AddonHostSnapshot&) override {}
  void quiesceGeneration(std::uint64_t) noexcept override {}
};
} // namespace

std::unique_ptr<SimulatorAddon> createRNTesterAddon() {
  return std::make_unique<RNTesterAddon>();
}
