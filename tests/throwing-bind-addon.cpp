#include <react-native-simulator/SimulatorAddon.h>
#include "AddonApiFingerprint.h"

#include <stdexcept>

namespace rns = ReactNativeSimulator;

namespace {
class ThrowingBindAddon final : public rns::SimulatorAddon {
 public:
  rns::AddonManifest manifest() const override {
    rns::AddonManifest manifest;
    manifest.name = "throwing-bind";
    manifest.addonVersion = "0.0.1";
    return manifest;
  }
  void bind(const rns::AddonHost&) override {
    throw std::runtime_error("bind boom");
  }
  void unbind() noexcept override {}
  std::shared_ptr<facebook::react::TurboModule> getTurboModule(
      const rns::AddonGenerationContext&,
      facebook::jsi::Runtime&,
      const std::string&,
      const std::shared_ptr<facebook::react::CallInvoker>&) override {
    return nullptr;
  }
  std::shared_ptr<facebook::react::TurboModule> wrapTurboModule(
      const rns::AddonGenerationContext&,
      facebook::jsi::Runtime&,
      const std::string&,
      std::shared_ptr<facebook::react::TurboModule> framework,
      const std::shared_ptr<facebook::react::CallInvoker>&) override {
    return framework;
  }
  void configureFabric(
      const rns::AddonGenerationContext&,
      rns::AddonFabricRegistrar&) override {}
  void installJSI(
      const rns::AddonGenerationContext&,
      facebook::jsi::Runtime&,
      const std::shared_ptr<facebook::react::CallInvoker>&) override {}
  void hostSnapshotChanged(const rns::AddonHostSnapshot&) override {}
  void quiesceGeneration(std::uint64_t) noexcept override {}
};
} // namespace

extern "C" RNS_EXPORT const ReactNativeSimulator::SimulatorAddonDescriptor*
react_native_simulator_addon_v4() noexcept {
  using namespace ReactNativeSimulator;
  static const SimulatorAddonDescriptor descriptor{
      sizeof(SimulatorAddonDescriptor),
      kSimulatorAddonAbiVersion,
      kSimulatorAddonApiFingerprint,
      "throwing-bind",
      RNS_REACT_NATIVE_VERSION,
      RNS_HERMES_VERSION,
      []() -> SimulatorAddon* { return new ThrowingBindAddon(); },
      [](SimulatorAddon* addon) noexcept { delete addon; },
  };
  return &descriptor;
}
