#include <react-native-simulator/SimulatorAddon.h>
#include "AddonApiFingerprint.h"

#include <stdexcept>

extern "C" RNS_EXPORT const ReactNativeSimulator::SimulatorAddonDescriptor*
react_native_simulator_addon_v4() noexcept {
  using namespace ReactNativeSimulator;
  static const SimulatorAddonDescriptor descriptor{
      sizeof(SimulatorAddonDescriptor),
      kSimulatorAddonAbiVersion,
      kSimulatorAddonApiFingerprint,
      "throwing-create",
      RNS_REACT_NATIVE_VERSION,
      RNS_HERMES_VERSION,
      []() -> SimulatorAddon* { throw std::runtime_error("create boom"); },
      [](SimulatorAddon*) noexcept {},
  };
  return &descriptor;
}
