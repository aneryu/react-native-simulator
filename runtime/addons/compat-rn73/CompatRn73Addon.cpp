#include <react-native-simulator/SimulatorAddon.h>

#include <iostream>
#include <memory>
#include <utility>

namespace jsi = facebook::jsi;
namespace react = facebook::react;
using ReactNativeSimulator::AddonFabricRegistrar;
using ReactNativeSimulator::AddonGenerationContext;
using ReactNativeSimulator::AddonHost;
using ReactNativeSimulator::AddonHostSnapshot;
using ReactNativeSimulator::AddonManifest;
using ReactNativeSimulator::AddonRole;
using ReactNativeSimulator::SimulatorAddon;

namespace {
class PlatformConstantsRn73Overlay final : public react::TurboModule {
 public:
  PlatformConstantsRn73Overlay(
      std::shared_ptr<react::TurboModule> inner,
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("PlatformConstants", std::move(jsInvoker)),
        inner_(std::move(inner)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["getAndroidID"] = {0, &getAndroidID};
  }

 private:
  std::shared_ptr<react::TurboModule> inner_;

  static jsi::Value callInner(
      jsi::Runtime& runtime,
      const std::shared_ptr<react::TurboModule>& inner,
      const char* name) {
    auto function = inner->get(runtime, jsi::PropNameID::forAscii(runtime, name));
    if (!function.isObject() || !function.getObject(runtime).isFunction(runtime)) {
      return jsi::Value::undefined();
    }
    auto host = jsi::Object::createFromHostObject(runtime, inner);
    return function.getObject(runtime).getFunction(runtime).callWithThis(
        runtime, host);
  }

  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    auto& self = static_cast<PlatformConstantsRn73Overlay&>(module);
    auto constantsValue = callInner(runtime, self.inner_, "getConstants");
    if (!constantsValue.isObject()) {
      return constantsValue;
    }
    auto constants = constantsValue.getObject(runtime);
    jsi::Object version(runtime);
    version.setProperty(runtime, "major", 0);
    version.setProperty(runtime, "minor", 73);
    version.setProperty(runtime, "patch", 10);
    version.setProperty(runtime, "prerelease", jsi::Value::null());
    constants.setProperty(runtime, "reactNativeVersion", std::move(version));
    return constants;
  }

  static jsi::Value getAndroidID(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    auto& self = static_cast<PlatformConstantsRn73Overlay&>(module);
    return callInner(runtime, self.inner_, "getAndroidID");
  }
};

class CompatRn73Addon final : public SimulatorAddon {
 public:
  AddonManifest manifest() const override {
    AddonManifest manifest;
    manifest.name = "compat-rn73";
    manifest.addonVersion = "1.0.0";
    manifest.role = AddonRole::VersionCompat;
    manifest.allowedProfiles = {"android-rn87"};
    manifest.moduleOverlays = {{
        "PlatformConstants",
        "js-visible-react-native-0.73.10",
    }};
    manifest.bundleCompatibility = {{
        .targetFamily = "0.73.x",
        .jsVisibleReactNativeVersion = "0.73.10",
        .level = "best-effort-source-js",
    }};
    return manifest;
  }

  void bind(const AddonHost&) override {
    std::cerr
        << "rnsim: native RN 0.87.0 + Hermes 260318099.0.1 is running JavaScript targeting\n"
        << "the RN 0.73.x family via compat-rn73. This is a best-effort source-JS adapter,\n"
        << "not an RN 0.73 native engine. Hermes bytecode is not translated.\n";
  }
  void unbind() noexcept override {}

  std::shared_ptr<react::TurboModule> getTurboModule(
      const AddonGenerationContext&,
      jsi::Runtime&,
      const std::string&,
      const std::shared_ptr<react::CallInvoker>&) override {
    return nullptr;
  }

  std::shared_ptr<react::TurboModule> wrapTurboModule(
      const AddonGenerationContext&,
      jsi::Runtime&,
      const std::string& moduleName,
      std::shared_ptr<react::TurboModule> framework,
      const std::shared_ptr<react::CallInvoker>& jsInvoker) override {
    if (moduleName == "PlatformConstants" && framework) {
      return std::make_shared<PlatformConstantsRn73Overlay>(
          std::move(framework), jsInvoker);
    }
    return framework;
  }

  void configureFabric(const AddonGenerationContext&, AddonFabricRegistrar&) override {}
  void installJSI(
      const AddonGenerationContext&,
      jsi::Runtime&,
      const std::shared_ptr<react::CallInvoker>&) override {}
  void hostSnapshotChanged(const AddonHostSnapshot&) override {}
  void quiesceGeneration(std::uint64_t) noexcept override {}
};
} // namespace

std::unique_ptr<SimulatorAddon> createCompatRn73Addon() {
  return std::make_unique<CompatRn73Addon>();
}
