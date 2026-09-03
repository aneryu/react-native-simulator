#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <jsi/jsi.h>

#include <memory>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ReactNativeSimulator {

struct SimulatorAddonCapability {
  std::string name;
  std::string fidelity;
};

struct SimulatorAddonNumericConstant {
  std::string name;
  double value{0};
};

struct SimulatorAddonCommand {
  std::string name;
  std::int32_t id{0};
};

// Application-owned legacy interop data stays in the addon that declares the
// component. The engine only translates this plain data into UIManager's JSI
// shape; it does not know application component names.
struct SimulatorAddonViewManagerConfig {
  std::string name;
  std::vector<SimulatorAddonNumericConstant> numericConstants;
  std::vector<SimulatorAddonCommand> commands;
};

class SimulatorAddon {
 public:
  virtual ~SimulatorAddon() = default;
  virtual std::string name() const = 0;
  virtual std::vector<std::string> moduleNames() const = 0;
  virtual std::vector<SimulatorAddonCapability> moduleCapabilities() const = 0;
  // Descriptor-only component mocks preserve Fabric tree, Yoga, diff and
  // mutation cost without pretending to provide platform pixels or I/O.
  virtual std::vector<SimulatorAddonCapability> componentCapabilities() const {
    return {};
  }
  virtual std::vector<SimulatorAddonViewManagerConfig>
  viewManagerConfigs() const {
    return {};
  }
  virtual std::shared_ptr<facebook::react::TurboModule> getTurboModule(
      facebook::jsi::Runtime& runtime,
      const std::string& moduleName,
      const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker) = 0;
  // Optional JSI install after TurboModuleBinding is in place.
  virtual void installJSI(
      facebook::jsi::Runtime& runtime,
      const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker) {
    (void)runtime;
    (void)jsInvoker;
  }
};

inline constexpr std::uint32_t kSimulatorAddonAbiVersion = 3;

struct SimulatorAddonDescriptor {
  std::uint32_t abiVersion;
  const char* name;
  const char* reactNativeVersion;
  const char* hermesVersion;
  SimulatorAddon* (*create)();
  void (*destroy)(SimulatorAddon*);
};

using GetSimulatorAddonDescriptor = const SimulatorAddonDescriptor* (*)();
inline constexpr const char* kSimulatorAddonEntryPoint =
    "react_native_simulator_addon_v2";

class SimulatorAddonRegistry {
 public:
  SimulatorAddonRegistry();
  ~SimulatorAddonRegistry();
  SimulatorAddonRegistry(SimulatorAddonRegistry&&) noexcept;
  SimulatorAddonRegistry& operator=(SimulatorAddonRegistry&&) noexcept;
  SimulatorAddonRegistry(const SimulatorAddonRegistry&) = delete;
  SimulatorAddonRegistry& operator=(const SimulatorAddonRegistry&) = delete;

  void add(std::unique_ptr<SimulatorAddon> addon);
  void load(
      const std::filesystem::path& libraryPath,
      std::string_view reactNativeVersion,
      std::string_view hermesVersion);
  std::shared_ptr<facebook::react::TurboModule> getTurboModule(
      facebook::jsi::Runtime& runtime,
      const std::string& moduleName,
      const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker);
  std::vector<std::string> names() const;
  std::vector<std::string> moduleNames() const;
  std::vector<SimulatorAddonCapability> moduleCapabilities() const;
  std::vector<SimulatorAddonCapability> componentCapabilities() const;
  std::vector<SimulatorAddonViewManagerConfig> viewManagerConfigs() const;
  void installJSI(
      facebook::jsi::Runtime& runtime,
      const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker);

 private:
  struct Entry;
  std::vector<std::unique_ptr<Entry>> addons_;
};

} // namespace ReactNativeSimulator
