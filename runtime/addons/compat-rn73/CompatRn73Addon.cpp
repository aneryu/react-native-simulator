#include <react-native-simulator/SimulatorAddon.h>

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

  std::vector<jsi::PropNameID> getPropertyNames(jsi::Runtime& runtime) override {
    auto names = inner_->getPropertyNames(runtime);
    auto has = [&](const char* needle) {
      for (const auto& name : names) {
        if (name.utf8(runtime) == needle) {
          return true;
        }
      }
      return false;
    };
    if (!has("getConstants")) {
      names.push_back(jsi::PropNameID::forAscii(runtime, "getConstants"));
    }
    if (!has("getAndroidID")) {
      names.push_back(jsi::PropNameID::forAscii(runtime, "getAndroidID"));
    }
    return names;
  }

 private:
  static jsi::Value callInner(
      jsi::Runtime& runtime,
      react::TurboModule& inner,
      const char* name,
      const jsi::Value* args,
      size_t count) {
    auto function = inner.get(
        runtime, jsi::PropNameID::forAscii(runtime, name));
    if (!function.isObject() || !function.getObject(runtime).isFunction(runtime)) {
      return jsi::Value::undefined();
    }
    return function.getObject(runtime).getFunction(runtime).call(
        runtime, args, count);
  }

  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule& turboModule,
      const jsi::Value* args,
      size_t count) {
    auto& overlay = static_cast<PlatformConstantsRn73Overlay&>(turboModule);
    auto constantsValue =
        callInner(runtime, *overlay.inner_, "getConstants", args, count);
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
      react::TurboModule& turboModule,
      const jsi::Value* args,
      size_t count) {
    auto& overlay = static_cast<PlatformConstantsRn73Overlay&>(turboModule);
    return callInner(runtime, *overlay.inner_, "getAndroidID", args, count);
  }

  std::shared_ptr<react::TurboModule> inner_;
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

  void bind(const AddonHost&) override {}
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
