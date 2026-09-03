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
        inner_(std::move(inner)) {}

  jsi::Value get(jsi::Runtime& runtime, const jsi::PropNameID& name) override {
    const auto utf8 = name.utf8(runtime);
    if (utf8 == "getConstants") {
      return jsi::Function::createFromHostFunction(
          runtime,
          name,
          0,
          [inner = inner_](
              jsi::Runtime& runtime,
              const jsi::Value&,
              const jsi::Value*,
              size_t) -> jsi::Value {
            auto function = inner->get(
                runtime, jsi::PropNameID::forAscii(runtime, "getConstants"));
            if (!function.isObject() ||
                !function.getObject(runtime).isFunction(runtime)) {
              return jsi::Value::undefined();
            }
            auto constantsValue =
                function.getObject(runtime).getFunction(runtime).call(runtime);
            if (!constantsValue.isObject()) {
              return constantsValue;
            }
            auto constants = constantsValue.getObject(runtime);
            jsi::Object version(runtime);
            version.setProperty(runtime, "major", 0);
            version.setProperty(runtime, "minor", 73);
            version.setProperty(runtime, "patch", 10);
            version.setProperty(runtime, "prerelease", jsi::Value::null());
            constants.setProperty(
                runtime, "reactNativeVersion", std::move(version));
            return constants;
          });
    }
    return inner_->get(runtime, name);
  }

 private:
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
