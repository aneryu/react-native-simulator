#include <react-native-simulator/SimulatorAddon.h>

#include <dlfcn.h>
#include <iterator>
#include <stdexcept>

namespace ReactNativeSimulator {

struct SimulatorAddonRegistry::Entry {
  std::unique_ptr<SimulatorAddon> owned;
  SimulatorAddon* addon{nullptr};
  void (*destroy)(SimulatorAddon*){nullptr};
  void* library{nullptr};

  ~Entry() {
    if (destroy != nullptr && addon != nullptr) {
      destroy(addon);
    }
    owned.reset();
    if (library != nullptr) {
      dlclose(library);
    }
  }
};

SimulatorAddonRegistry::SimulatorAddonRegistry() = default;
SimulatorAddonRegistry::~SimulatorAddonRegistry() = default;
SimulatorAddonRegistry::SimulatorAddonRegistry(SimulatorAddonRegistry&&) noexcept =
    default;
SimulatorAddonRegistry& SimulatorAddonRegistry::operator=(
    SimulatorAddonRegistry&&) noexcept = default;

void SimulatorAddonRegistry::add(std::unique_ptr<SimulatorAddon> addon) {
  auto entry = std::make_unique<Entry>();
  entry->addon = addon.get();
  entry->owned = std::move(addon);
  addons_.push_back(std::move(entry));
}

void SimulatorAddonRegistry::load(
    const std::filesystem::path& libraryPath,
    std::string_view reactNativeVersion,
    std::string_view hermesVersion) {
  void* library = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (library == nullptr) {
    throw std::runtime_error(
        "Cannot load addon " + libraryPath.string() + ": " + dlerror());
  }
  auto closeOnFailure = [&] { dlclose(library); };
  dlerror();
  auto getDescriptor = reinterpret_cast<GetSimulatorAddonDescriptor>(
      dlsym(library, kSimulatorAddonEntryPoint));
  if (const char* error = dlerror(); error != nullptr) {
    const std::string message = error;
    closeOnFailure();
    throw std::runtime_error(
        "Addon entry point is missing in " + libraryPath.string() +
        ": " + message);
  }
  const auto* descriptor = getDescriptor();
  if (descriptor == nullptr || descriptor->abiVersion != kSimulatorAddonAbiVersion ||
      descriptor->name == nullptr || descriptor->reactNativeVersion == nullptr ||
      descriptor->hermesVersion == nullptr || descriptor->create == nullptr ||
      descriptor->destroy == nullptr) {
    closeOnFailure();
    throw std::runtime_error("Invalid addon descriptor: " + libraryPath.string());
  }
  if (reactNativeVersion != descriptor->reactNativeVersion ||
      hermesVersion != descriptor->hermesVersion) {
    const auto addonVersions = std::string(descriptor->reactNativeVersion) + "/" +
        descriptor->hermesVersion;
    closeOnFailure();
    throw std::runtime_error(
        "Addon runtime version mismatch: " + addonVersions);
  }
  auto entry = std::make_unique<Entry>();
  entry->library = library;
  entry->destroy = descriptor->destroy;
  entry->addon = descriptor->create();
  if (entry->addon == nullptr || entry->addon->name() != descriptor->name) {
    throw std::runtime_error("Addon factory returned an invalid instance");
  }
  addons_.push_back(std::move(entry));
}

std::shared_ptr<facebook::react::TurboModule>
SimulatorAddonRegistry::getTurboModule(
    facebook::jsi::Runtime& runtime,
    const std::string& moduleName,
    const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker) {
  for (const auto& addon : addons_) {
    if (auto module = addon->addon->getTurboModule(
            runtime, moduleName, jsInvoker)) {
      return module;
    }
  }
  return nullptr;
}

std::vector<std::string> SimulatorAddonRegistry::names() const {
  std::vector<std::string> result;
  result.reserve(addons_.size());
  for (const auto& addon : addons_) {
    result.push_back(addon->addon->name());
  }
  return result;
}

std::vector<std::string> SimulatorAddonRegistry::moduleNames() const {
  std::vector<std::string> result;
  for (const auto& addon : addons_) {
    const auto names = addon->addon->moduleNames();
    result.insert(result.end(), names.begin(), names.end());
  }
  return result;
}

std::vector<SimulatorAddonCapability>
SimulatorAddonRegistry::moduleCapabilities() const {
  std::vector<SimulatorAddonCapability> result;
  for (const auto& addon : addons_) {
    const auto capabilities = addon->addon->moduleCapabilities();
    result.insert(result.end(), capabilities.begin(), capabilities.end());
  }
  return result;
}

std::vector<SimulatorAddonCapability>
SimulatorAddonRegistry::componentCapabilities() const {
  std::vector<SimulatorAddonCapability> result;
  for (const auto& entry : addons_) {
    auto capabilities = entry->addon->componentCapabilities();
    result.insert(
        result.end(),
        std::make_move_iterator(capabilities.begin()),
        std::make_move_iterator(capabilities.end()));
  }
  return result;
}

std::vector<SimulatorAddonViewManagerConfig>
SimulatorAddonRegistry::viewManagerConfigs() const {
  std::vector<SimulatorAddonViewManagerConfig> result;
  for (const auto& entry : addons_) {
    auto configs = entry->addon->viewManagerConfigs();
    result.insert(
        result.end(),
        std::make_move_iterator(configs.begin()),
        std::make_move_iterator(configs.end()));
  }
  return result;
}

} // namespace ReactNativeSimulator
