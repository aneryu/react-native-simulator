#include <react-native-simulator/SimulatorAddon.h>
#include "AddonApiFingerprint.h"

extern "C" RNS_EXPORT const ReactNativeSimulator::SimulatorAddonDescriptor*
react_native_simulator_addon_v4() noexcept {
  using namespace ReactNativeSimulator;
  static const SimulatorAddonDescriptor descriptor{
      sizeof(SimulatorAddonDescriptor),
      kSimulatorAddonAbiVersion,
      kSimulatorAddonApiFingerprint,
      "null-callbacks",
      RNS_REACT_NATIVE_VERSION,
      RNS_HERMES_VERSION,
      nullptr,
      nullptr,
  };
  return &descriptor;
}
