#include <react-native-simulator/SimulatorAddon.h>

#include <react/nativemodule/core/ReactCommon/TurboModuleUtils.h>

namespace jsi = facebook::jsi;
namespace react = facebook::react;
using ReactNativeSimulator::SimulatorAddon;
using ReactNativeSimulator::SimulatorAddonCapability;
using ReactNativeSimulator::SimulatorAddonCommand;
using ReactNativeSimulator::SimulatorAddonDescriptor;
using ReactNativeSimulator::SimulatorAddonNumericConstant;
using ReactNativeSimulator::SimulatorAddonViewManagerConfig;
using ReactNativeSimulator::kSimulatorAddonAbiVersion;

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
  std::string name() const override {
    return "rntester";
  }

  std::vector<std::string> moduleNames() const override {
    return {"NativeCxxModuleExampleCxx", "ScreenshotManager"};
  }

  std::vector<SimulatorAddonCapability> moduleCapabilities() const override {
    return {
        {"NativeCxxModuleExampleCxx", "tester-stub"},
        {"ScreenshotManager", "tester-stub"},
    };
  }

  std::vector<SimulatorAddonCapability> componentCapabilities() const override {
    return {
        {"RNTReportFullyDrawnView", "descriptor-only-mock"},
        {"RNTMyNativeView", "descriptor-only-mock"},
        {"RNTMyLegacyNativeView", "descriptor-only-mock"},
        {"AndroidPopupMenu", "descriptor-only-mock"},
    };
  }

  std::vector<SimulatorAddonViewManagerConfig>
  viewManagerConfigs() const override {
    return {{
        .name = "RNTMyLegacyNativeView",
        .numericConstants = {
            SimulatorAddonNumericConstant{.name = "PI", .value = 3.14},
        },
        .commands = {
            SimulatorAddonCommand{
                .name = "changeBackgroundColor", .id = 1},
            SimulatorAddonCommand{.name = "addOverlays", .id = 2},
            SimulatorAddonCommand{.name = "removeOverlays", .id = 3},
        },
    }};
  }

  std::shared_ptr<react::TurboModule> getTurboModule(
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
};
} // namespace

extern "C" const SimulatorAddonDescriptor*
react_native_simulator_addon_v2() {
  static const SimulatorAddonDescriptor descriptor{
      .abiVersion = kSimulatorAddonAbiVersion,
      .name = "rntester",
      .reactNativeVersion = RNS_REACT_NATIVE_VERSION,
      .hermesVersion = RNS_HERMES_VERSION,
      .create = []() -> SimulatorAddon* { return new RNTesterAddon(); },
      .destroy = [](SimulatorAddon* addon) { delete addon; },
  };
  return &descriptor;
}
