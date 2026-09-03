#include "LaunchPlan.h"

#include "AddonApiFingerprint.h"
#include "AddonJson.h"
#include "BuiltinAddonCatalog.h"

#include <folly/json.h>
#include <react/renderer/componentregistry/componentNameByReactViewName.h>

#include <algorithm>
#include <sstream>
#include <sys/stat.h>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace rns = ReactNativeSimulator;

namespace {

std::string joinOrigins(const std::vector<rns::AddonRequestOrigin>& origins) {
  std::string result;
  for (size_t i = 0; i < origins.size(); ++i) {
    if (i != 0) {
      result += ",";
    }
    result += rns::jsonAddonOrigin(origins[i]);
  }
  return result;
}

rns::FileIdentity identifyFile(const std::filesystem::path& path) {
  std::error_code error;
  auto canonical = std::filesystem::weakly_canonical(path, error);
  if (error) {
    canonical = std::filesystem::absolute(path);
  }
  rns::FileIdentity identity{.canonical = canonical};
  struct stat info {};
  if (::stat(canonical.c_str(), &info) == 0) {
    identity.device = static_cast<std::uint64_t>(info.st_dev);
    identity.inode = static_cast<std::uint64_t>(info.st_ino);
  }
  return identity;
}

std::string explicitFingerprint(const rns::LaunchDraft::Impl& draft) {
  std::ostringstream out;
  for (const auto& request : draft.requests) {
    std::visit(
        [&](const auto& spec) {
          using T = std::decay_t<decltype(spec)>;
          if constexpr (std::is_same_v<T, rns::BuiltInAddonSpec>) {
            out << "builtin:" << spec.catalogKey << ';';
          } else if constexpr (std::is_same_v<T, rns::ModuleAddonSpec>) {
            out << "module:" << identifyFile(spec.path).canonical.string()
                << ';';
          } else {
            out << "inprocess:" << spec.diagnosticLabel << ';';
          }
        },
        request.spec);
  }
  return out.str();
}

void validateManifest(const rns::AddonManifest& manifest, std::string_view where) {
  if (!rns::validAddonName(manifest.name)) {
    throw rns::TerminalLaunchPlanError(
        "Invalid addon name '" + manifest.name + "' for " + std::string(where));
  }
  std::unordered_set<std::string> modules;
  std::unordered_set<std::string> overlays;
  std::unordered_set<std::string> components;
  for (const auto& module : manifest.modules) {
    if (module.name.empty() || !modules.insert(module.name).second) {
      throw rns::TerminalLaunchPlanError(
          "Addon " + manifest.name + " has a duplicate or empty module name");
    }
    if (module.classification == rns::RuntimeCapabilityClass::Unavailable) {
      throw rns::TerminalLaunchPlanError(
          "Addon " + manifest.name + " cannot serve unavailable module " +
          module.name);
    }
  }
  for (const auto& overlay : manifest.moduleOverlays) {
    if (overlay.moduleName.empty() ||
        !overlays.insert(overlay.moduleName).second) {
      throw rns::TerminalLaunchPlanError(
          "Addon " + manifest.name + " has a duplicate or empty overlay target");
    }
    if (modules.contains(overlay.moduleName)) {
      throw rns::TerminalLaunchPlanError(
          "Addon " + manifest.name +
          " cannot serve and overlay the same module " + overlay.moduleName);
    }
  }
  for (const auto& component : manifest.components) {
    if (component.name.empty() || !components.insert(component.name).second) {
      throw rns::TerminalLaunchPlanError(
          "Addon " + manifest.name + " has a duplicate or empty component name");
    }
    if (component.classification == rns::RuntimeCapabilityClass::Unavailable) {
      throw rns::TerminalLaunchPlanError(
          "Addon " + manifest.name + " cannot serve unavailable component " +
          component.name);
    }
    if (component.kind == rns::AddonComponentKind::DescriptorOnlyMock &&
        component.classification != rns::RuntimeCapabilityClass::LayoutOnly &&
        component.classification != rns::RuntimeCapabilityClass::Mocked) {
      throw rns::TerminalLaunchPlanError(
          "Addon " + manifest.name + " DescriptorOnlyMock component " +
          component.name + " must be layout-only or mocked");
    }
    std::unordered_set<std::string> events;
    std::unordered_set<std::string> commands;
    for (const auto& event : component.events) {
      if (event.empty() || !events.insert(event).second) {
        throw rns::TerminalLaunchPlanError(
            "Addon " + manifest.name + " component " + component.name +
            " has a duplicate or empty event");
      }
    }
    for (const auto& command : component.commands) {
      if (command.empty() || !commands.insert(command).second) {
        throw rns::TerminalLaunchPlanError(
            "Addon " + manifest.name + " component " + component.name +
            " has a duplicate or empty command");
      }
    }
  }
  std::unordered_set<std::string> viewManagers;
  for (const auto& config : manifest.viewManagerConfigs) {
    if (config.name.empty() || !viewManagers.insert(config.name).second) {
      throw rns::TerminalLaunchPlanError(
          "Addon " + manifest.name +
          " has a duplicate or empty view-manager config");
    }
    if (!components.contains(config.name)) {
      throw rns::TerminalLaunchPlanError(
          "Addon " + manifest.name + " view-manager config " + config.name +
          " does not name a component of this addon");
    }
    std::unordered_set<std::string> constants;
    std::unordered_set<std::string> commands;
    std::unordered_set<std::int32_t> ids;
    for (const auto& constant : config.numericConstants) {
      if (constant.name.empty() || !constants.insert(constant.name).second) {
        throw rns::TerminalLaunchPlanError(
            "Addon " + manifest.name + " view manager " + config.name +
            " has a duplicate or empty numeric constant");
      }
    }
    for (const auto& command : config.commands) {
      if (command.name.empty() || !commands.insert(command.name).second ||
          !ids.insert(command.id).second) {
        throw rns::TerminalLaunchPlanError(
            "Addon " + manifest.name + " view manager " + config.name +
            " has a duplicate command name or id");
      }
    }
  }
}

rns::CommittedAddon loadBuiltIn(const rns::BuiltInAddonSpec& spec,
                                std::vector<rns::AddonRequestOrigin> origins) {
  const auto* entry = rns::findBuiltinAddon(spec.catalogKey);
  if (entry == nullptr) {
    throw rns::TerminalLaunchPlanError(
        "unknown addon name '" + spec.catalogKey + "'");
  }
  rns::CommittedAddon loaded;
  loaded.origin = {
      .source = rns::AddonSource::BuiltIn,
      .locator = spec.catalogKey,
      .requestedBy = std::move(origins),
  };
  loaded.addon = rns::AddonPtr(entry->create().release(), rns::AddonDeleter{});
  if (!loaded.addon) {
    throw rns::TerminalLaunchPlanError(
        "Built-in addon factory returned null: " + spec.catalogKey);
  }
  loaded.manifest = loaded.addon->manifest();
  if (loaded.manifest.name != spec.catalogKey) {
    throw rns::TerminalLaunchPlanError(
        "Built-in addon name '" + loaded.manifest.name +
        "' does not equal catalog key '" + spec.catalogKey + "'");
  }
  validateManifest(loaded.manifest, spec.catalogKey);
  return loaded;
}

rns::CommittedAddon loadModule(const rns::ModuleAddonSpec& spec,
                               std::vector<rns::AddonRequestOrigin> origins) {
  auto library = std::make_shared<rns::DlLibrary>();
  library->path = spec.path;
  library->handle = dlopen(spec.path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (library->handle == nullptr) {
    const char* error = dlerror();
    throw rns::TerminalLaunchPlanError(
        "Cannot load addon " + spec.path.string() + ": " +
        (error != nullptr ? error : "unknown dlopen error"));
  }
  dlerror();
  auto getDescriptor = reinterpret_cast<rns::GetSimulatorAddonDescriptorV4>(
      dlsym(library->handle, rns::kSimulatorAddonEntryPoint));
  if (const char* error = dlerror(); error != nullptr || getDescriptor == nullptr) {
    throw rns::TerminalLaunchPlanError(
        spec.path.string() + " is not an ABI 4 addon");
  }
  const auto* descriptor = getDescriptor();
  if (descriptor == nullptr) {
    throw rns::TerminalLaunchPlanError(
        "Invalid addon descriptor: " + spec.path.string());
  }
  if (descriptor->descriptorSize < sizeof(rns::SimulatorAddonDescriptor) ||
      descriptor->abiVersion != rns::kSimulatorAddonAbiVersion ||
      descriptor->addonApiFingerprint == nullptr ||
      descriptor->name == nullptr || descriptor->name[0] == '\0' ||
      descriptor->reactNativeVersion == nullptr ||
      descriptor->hermesVersion == nullptr || descriptor->create == nullptr ||
      descriptor->destroy == nullptr) {
    throw rns::TerminalLaunchPlanError(
        "Invalid addon descriptor: " + spec.path.string());
  }
  if (std::string_view(descriptor->addonApiFingerprint) !=
      kSimulatorAddonApiFingerprint) {
    throw rns::TerminalLaunchPlanError(
        "Addon API fingerprint mismatch for " + spec.path.string() +
        "; rebuild the MODULE against this engine");
  }
  if (std::string_view(descriptor->reactNativeVersion) !=
          RNS_REACT_NATIVE_VERSION ||
      std::string_view(descriptor->hermesVersion) != RNS_HERMES_VERSION) {
    throw rns::TerminalLaunchPlanError(
        std::string("Addon runtime version mismatch: ") +
        descriptor->reactNativeVersion + "/" + descriptor->hermesVersion);
  }
  rns::SimulatorAddon* raw = nullptr;
  try {
    raw = descriptor->create();
  } catch (const std::exception& error) {
    const std::string name =
        descriptor->name != nullptr ? descriptor->name : "";
    throw rns::TerminalLaunchPlanError(
        "Addon create() failed for " + spec.path.string() +
        (name.empty() ? "" : " (" + name + ")") + ": " + error.what());
  } catch (...) {
    throw rns::TerminalLaunchPlanError(
        "Addon create() failed for " + spec.path.string());
  }
  if (raw == nullptr) {
    throw rns::TerminalLaunchPlanError(
        "Addon factory returned null: " + spec.path.string());
  }
  rns::CommittedAddon loaded;
  loaded.origin = {
      .source = rns::AddonSource::Module,
      .locator = spec.path.string(),
      .requestedBy = std::move(origins),
  };
  loaded.library = std::move(library);
  loaded.addon = rns::AddonPtr(raw, rns::AddonDeleter{descriptor->destroy});
  loaded.manifest = loaded.addon->manifest();
  if (loaded.manifest.name != descriptor->name) {
    throw rns::TerminalLaunchPlanError(
        "Addon manifest name '" + loaded.manifest.name +
        "' does not equal descriptor name '" + descriptor->name + "'");
  }
  validateManifest(loaded.manifest, loaded.origin.locator);
  return loaded;
}

rns::CommittedAddon loadInProcess(rns::InProcessAddonSpec spec,
                                  std::vector<rns::AddonRequestOrigin> origins) {
  if (!spec.addon) {
    throw rns::TerminalLaunchPlanError("InProcess addon must not be null");
  }
  rns::CommittedAddon loaded;
  loaded.origin = {
      .source = rns::AddonSource::InProcess,
      .locator = spec.diagnosticLabel,
      .requestedBy = std::move(origins),
  };
  loaded.addon = rns::AddonPtr(spec.addon.release(), rns::AddonDeleter{});
  loaded.manifest = loaded.addon->manifest();
  if (!rns::validAddonName(loaded.manifest.name)) {
    throw rns::TerminalLaunchPlanError(
        "Invalid InProcess addon name '" + loaded.manifest.name + "'");
  }
  validateManifest(loaded.manifest, loaded.origin.locator);
  return loaded;
}

rns::CommittedAddon loadRequest(rns::AddonRequest& request) {
  return std::visit(
      [&](auto& spec) {
        using T = std::decay_t<decltype(spec)>;
        if constexpr (std::is_same_v<T, rns::BuiltInAddonSpec>) {
          return loadBuiltIn(spec, request.requestedBy);
        } else if constexpr (std::is_same_v<T, rns::ModuleAddonSpec>) {
          return loadModule(spec, request.requestedBy);
        } else {
          return loadInProcess(std::move(spec), request.requestedBy);
        }
      },
      request.spec);
}

bool autoSelected(rns::AddonAutoPolicy policy, rns::ProjectKind kind) {
  switch (policy) {
    case rns::AddonAutoPolicy::Always:
      return true;
    case rns::AddonAutoPolicy::Expo:
      return kind == rns::ProjectKind::Expo;
    case rns::AddonAutoPolicy::Never:
      return false;
  }
  return false;
}

std::string normalizedComponentName(std::string_view name) {
  if (name.size() < 3) {
    return std::string(name);
  }
  return facebook::react::componentNameByReactViewName(std::string(name));
}

bool isFixedPointName(std::string_view name) {
  return normalizedComponentName(name) == name;
}

} // namespace

namespace ReactNativeSimulator {

TerminalLaunchPlanError::TerminalLaunchPlanError(std::string message)
    : std::runtime_error(std::move(message)) {}

RetryableNetworkError::RetryableNetworkError(std::string message)
    : std::runtime_error(std::move(message)) {}

AddonContractViolation::AddonContractViolation(
    std::string addon,
    std::string operation,
    std::string surface,
    std::uint64_t generation,
    std::string message)
    : std::runtime_error(std::move(message)),
      addon_(std::move(addon)),
      operation_(std::move(operation)),
      surface_(std::move(surface)),
      generation_(generation) {}

const std::string& AddonContractViolation::addon() const noexcept {
  return addon_;
}
const std::string& AddonContractViolation::operation() const noexcept {
  return operation_;
}
const std::string& AddonContractViolation::surface() const noexcept {
  return surface_;
}
std::uint64_t AddonContractViolation::generation() const noexcept {
  return generation_;
}

LaunchDraft::LaunchDraft(EngineConfig config)
    : impl_(std::make_unique<Impl>()) {
  impl_->config = std::move(config);
}

LaunchDraft::LaunchDraft(LaunchDraft&&) noexcept = default;
LaunchDraft& LaunchDraft::operator=(LaunchDraft&&) noexcept = default;
LaunchDraft::~LaunchDraft() = default;

const EngineConfig& LaunchDraft::config() const noexcept {
  return impl_->config;
}
EngineConfig& LaunchDraft::config() noexcept {
  return impl_->config;
}

void LaunchDraft::setProjectKind(ProjectKind kind) {
  impl_->projectKind = kind;
}

void LaunchDraft::addBuiltInAddon(
    std::string_view catalogKey,
    AddonRequestOrigin origin) {
  if (impl_->explicitPrepared) {
    impl_->explicitMutatedAfterPrepare = true;
  }
  AddonRequest request;
  request.spec = BuiltInAddonSpec{std::string(catalogKey)};
  request.requestedBy.push_back(origin);
  impl_->requests.push_back(std::move(request));
}

void LaunchDraft::addAddonPath(
    const std::filesystem::path& path,
    AddonRequestOrigin origin) {
  if (impl_->explicitPrepared) {
    impl_->explicitMutatedAfterPrepare = true;
  }
  AddonRequest request;
  request.spec = ModuleAddonSpec{path};
  request.requestedBy.push_back(origin);
  impl_->requests.push_back(std::move(request));
}

void LaunchDraft::addAddon(
    std::unique_ptr<SimulatorAddon> addon,
    std::string diagnosticLabel,
    AddonRequestOrigin origin) {
  if (impl_->explicitPrepared) {
    impl_->explicitMutatedAfterPrepare = true;
  }
  AddonRequest request;
  request.spec = InProcessAddonSpec{std::move(addon), std::move(diagnosticLabel)};
  request.requestedBy.push_back(origin);
  impl_->requests.push_back(std::move(request));
}

void LaunchDraft::disableAddon(std::string_view catalogKey) {
  if (std::find(impl_->disabled.begin(), impl_->disabled.end(), catalogKey) ==
      impl_->disabled.end()) {
    impl_->disabled.emplace_back(catalogKey);
  }
}

void LaunchDraft::setAutoAddons(bool enabled) {
  impl_->autoAddons = enabled;
}

void LaunchDraft::addBundle(InitialBundleSpec bundle) {
  if (bundle.sourceUrl.empty()) {
    throw std::invalid_argument("bundle sourceUrl must not be empty");
  }
  const bool hasPath = bundle.path.has_value();
  const bool hasBody = bundle.body.has_value();
  if (hasPath == hasBody) {
    throw std::invalid_argument("bundle requires exactly one of path or body");
  }
  impl_->bundles.push_back(std::move(bundle));
}

void LaunchDraft::setInitialUrl(std::optional<std::string> url) {
  impl_->initialUrl = std::move(url);
}

PreparedAddonCandidates::PreparedAddonCandidates()
    : impl_(std::make_unique<Impl>()) {}
PreparedAddonCandidates::PreparedAddonCandidates(
    PreparedAddonCandidates&&) noexcept = default;
PreparedAddonCandidates& PreparedAddonCandidates::operator=(
    PreparedAddonCandidates&&) noexcept = default;
PreparedAddonCandidates::~PreparedAddonCandidates() = default;
PreparedAddonCandidates::operator bool() const noexcept {
  return impl_ && impl_->valid;
}

PreparedLaunchPlan::PreparedLaunchPlan()
    : impl_(std::make_unique<Impl>()) {}
PreparedLaunchPlan::PreparedLaunchPlan(PreparedLaunchPlan&&) noexcept = default;
PreparedLaunchPlan& PreparedLaunchPlan::operator=(PreparedLaunchPlan&&) noexcept =
    default;
PreparedLaunchPlan::~PreparedLaunchPlan() = default;
PreparedLaunchPlan::operator bool() const noexcept {
  return impl_ && impl_->valid;
}

void destroyCommittedAddons(std::vector<CommittedAddon>& addons) noexcept {
  for (auto it = addons.rbegin(); it != addons.rend(); ++it) {
    it->addon.reset();
    it->library.reset();
  }
  addons.clear();
}

std::string describeAddonOrigin(const AddonOrigin& origin) {
  return std::string(jsonAddonSource(origin.source)) + " " + origin.locator +
      " (" + joinOrigins(origin.requestedBy) + ")";
}

std::string componentNameByReactViewNameGuarded(std::string_view name) {
  return normalizedComponentName(name);
}

folly::dynamic builtinAddonCatalogJson() {
  folly::dynamic addons = folly::dynamic::array;
  for (const auto& entry : builtinAddonCatalog()) {
    addons.push_back(folly::dynamic::object
        ("name", entry.key)
        ("auto", jsonAddonAutoPolicy(entry.autoPolicy))
        ("builtin", true));
  }
  return folly::dynamic::object
      ("addonAbi", static_cast<int>(kSimulatorAddonAbiVersion))
      ("reactNative", RNS_REACT_NATIVE_VERSION)
      ("hermes", RNS_HERMES_VERSION)
      ("addons", std::move(addons));
}

PreparedAddonCandidates prepareExplicitAddons(LaunchDraft& draft) {
  if (!draft.impl_) {
    throw std::logic_error("LaunchDraft is empty");
  }
  PreparedAddonCandidates candidates;
  std::unordered_map<std::string, rns::FileIdentity> seenFiles;
  std::unordered_set<std::string> seenNames;
  for (auto& request : draft.impl_->requests) {
    if (auto* module = std::get_if<ModuleAddonSpec>(&request.spec)) {
      auto identity = identifyFile(module->path);
      for (const auto& [path, existing] : seenFiles) {
        (void)path;
        if (existing.canonical == identity.canonical ||
            (identity.inode != 0 && existing.device == identity.device &&
             existing.inode == identity.inode)) {
          throw TerminalLaunchPlanError(
              "Duplicate addon MODULE path " + identity.canonical.string());
        }
      }
      seenFiles.emplace(identity.canonical.string(), identity);
    }
    auto loaded = loadRequest(request);
    if (!seenNames.insert(loaded.manifest.name).second) {
      throw TerminalLaunchPlanError(
          "Addon collision: duplicate addon name \"" + loaded.manifest.name +
          "\"");
    }
    candidates.impl_->addons.push_back(std::move(loaded));
  }
  candidates.impl_->fingerprint = explicitFingerprint(*draft.impl_);
  candidates.impl_->valid = true;
  draft.impl_->explicitPrepared = true;
  draft.impl_->explicitFingerprint = candidates.impl_->fingerprint;
  draft.impl_->explicitMutatedAfterPrepare = false;
  return candidates;
}

PreparedLaunchPlan finalizeLaunchPlan(
    LaunchDraft&& draft,
    PreparedAddonCandidates&& candidates) {
  if (!draft.impl_) {
    throw std::logic_error("LaunchDraft is empty");
  }
  if (!candidates) {
    throw TerminalLaunchPlanError(
        "finalizeLaunchPlan requires prepared addon candidates");
  }
  if (draft.impl_->explicitMutatedAfterPrepare ||
      explicitFingerprint(*draft.impl_) != candidates.impl_->fingerprint) {
    throw TerminalLaunchPlanError(
        "explicit addon requests changed after prepareExplicitAddons");
  }
  if (draft.impl_->config.colorScheme &&
      *draft.impl_->config.colorScheme != "light" &&
      *draft.impl_->config.colorScheme != "dark") {
    throw TerminalLaunchPlanError(
        "colorScheme must be light or dark");
  }
  if (draft.impl_->config.appState &&
      *draft.impl_->config.appState != "active" &&
      *draft.impl_->config.appState != "background" &&
      *draft.impl_->config.appState != "inactive") {
    throw TerminalLaunchPlanError(
        "appState must be active, background, or inactive");
  }
  if (!isKnownProfileName(draft.impl_->config.profile)) {
    throw TerminalLaunchPlanError(
        "Unknown profile: " + draft.impl_->config.profile);
  }

  std::unordered_set<std::string> disabled(
      draft.impl_->disabled.begin(), draft.impl_->disabled.end());
  for (const auto& name : disabled) {
    if (findBuiltinAddon(name) == nullptr) {
      throw TerminalLaunchPlanError("unknown addon name '" + name + "'");
    }
  }

  std::unordered_map<std::string, size_t> explicitByName;
  for (size_t i = 0; i < candidates.impl_->addons.size(); ++i) {
    explicitByName[candidates.impl_->addons[i].manifest.name] = i;
  }
  for (const auto& [name, index] : explicitByName) {
    (void)index;
    if (disabled.contains(name)) {
      throw TerminalLaunchPlanError(
          "explicit addon '" + name + "' is also disabled");
    }
  }

  std::vector<CommittedAddon> merged;
  std::unordered_set<std::string> placed;
  if (draft.impl_->autoAddons) {
    for (const auto& entry : builtinAddonCatalog()) {
      if (!autoSelected(entry.autoPolicy, draft.impl_->projectKind)) {
        continue;
      }
      if (disabled.contains(entry.key)) {
        continue;
      }
      if (auto found = explicitByName.find(entry.key);
          found != explicitByName.end()) {
        auto& addon = candidates.impl_->addons[found->second];
        addon.origin.requestedBy.insert(
            addon.origin.requestedBy.begin(), AddonRequestOrigin::Auto);
        merged.push_back(std::move(addon));
        placed.insert(entry.key);
        continue;
      }
      BuiltInAddonSpec spec{entry.key};
      merged.push_back(loadBuiltIn(spec, {AddonRequestOrigin::Auto}));
      placed.insert(entry.key);
    }
  }
  for (auto& addon : candidates.impl_->addons) {
    if (!addon.addon) {
      continue;
    }
    if (placed.contains(addon.manifest.name)) {
      continue;
    }
    merged.push_back(std::move(addon));
  }
  candidates.impl_->valid = false;
  candidates.impl_->addons.clear();

  auto bindings = std::make_shared<InventoryRuntimeBindings>();
  auto inventory =
      buildFrameworkSurfaceInventory(draft.impl_->config.profile, bindings);

  std::unordered_map<std::string, ModuleOwnerRow> moduleOwners;
  std::unordered_set<std::string> frameworkModules;
  std::unordered_set<std::string> frameworkComponents;
  std::unordered_map<std::string, std::string> overlays;
  std::vector<ComponentLedgerRow> components;

  auto addFrameworkModule = [&](const FrameworkModuleEntry& entry) {
    frameworkModules.insert(entry.contract.name);
    moduleOwners[entry.contract.name] = {
        .name = entry.contract.name,
        .owner = entry.contract.owner,
    };
  };
  for (const auto& entry : inventory.hostModules) {
    addFrameworkModule(entry);
  }
  for (const auto& entry : inventory.profileModules) {
    addFrameworkModule(entry);
  }
  auto addFrameworkComponent = [&](const FrameworkComponentEntry& entry) {
    frameworkComponents.insert(entry.contract.name);
    components.push_back({
        .requestedName = entry.contract.name,
        .normalizedName = entry.contract.name,
        .canonicalName = entry.contract.name,
        .handle = entry.provider.handle,
        .owner = entry.contract.owner,
        .kind = entry.contract.kind,
        .contract = entry.contract,
    });
  };
  for (const auto& entry : allInventoryComponents(inventory)) {
    addFrameworkComponent(entry);
  }

  std::unordered_set<std::string> addonNames;
  std::optional<std::string> compatAddon;
  ResolvedBundleCompatibility compatibility{
      .nativeReactNativeVersion = RNS_REACT_NATIVE_VERSION,
      .targetFamily = "0.87.x",
      .jsVisibleReactNativeVersion = RNS_REACT_NATIVE_VERSION,
      .level = inventory.profile.compatibilityLevel,
  };

  for (const auto& addon : merged) {
    if (!addonNames.insert(addon.manifest.name).second) {
      throw TerminalLaunchPlanError(
          "Addon collision: duplicate addon name \"" + addon.manifest.name +
          "\"");
    }
    if (!addon.manifest.allowedProfiles.empty()) {
      const auto allowed = std::find(
          addon.manifest.allowedProfiles.begin(),
          addon.manifest.allowedProfiles.end(),
          inventory.profile.name);
      if (allowed == addon.manifest.allowedProfiles.end()) {
        std::string listed;
        for (size_t i = 0; i < addon.manifest.allowedProfiles.size(); ++i) {
          if (i != 0) {
            listed += ", ";
          }
          listed += addon.manifest.allowedProfiles[i];
        }
        throw TerminalLaunchPlanError(
            "Addon policy: " + addon.manifest.name + " allows profiles [" +
            listed + "]; selected " + inventory.profile.name);
      }
    }
    const auto owner = "addon:" + addon.manifest.name;
    for (const auto& module : addon.manifest.modules) {
      if (frameworkModules.contains(module.name)) {
        throw TerminalLaunchPlanError(
            "Addon collision: module \"" + module.name +
            "\" declared by addon \"" + addon.manifest.name +
            "\" is owned by " + moduleOwners[module.name].owner);
      }
      if (moduleOwners.contains(module.name)) {
        throw TerminalLaunchPlanError(
            "Addon collision: module \"" + module.name +
            "\" declared by addon \"" + addon.manifest.name +
            "\" is owned by " + moduleOwners[module.name].owner);
      }
      moduleOwners[module.name] = {
          .name = module.name,
          .owner = owner,
      };
    }
    for (const auto& overlay : addon.manifest.moduleOverlays) {
      if (!frameworkModules.contains(overlay.moduleName)) {
        throw TerminalLaunchPlanError(
            "Addon policy: overlay target \"" + overlay.moduleName +
            "\" is not a framework module on " + inventory.profile.name);
      }
      if (auto found = overlays.find(overlay.moduleName);
          found != overlays.end()) {
        throw TerminalLaunchPlanError(
            "Addon policy: overlay target \"" + overlay.moduleName +
            "\" is already wrapped by addon \"" + found->second + "\"");
      }
      overlays[overlay.moduleName] = addon.manifest.name;
      moduleOwners[overlay.moduleName].overlayOwner = owner;
    }
    for (const auto& component : addon.manifest.components) {
      if (!isFixedPointName(component.name)) {
        throw TerminalLaunchPlanError(
            "Addon " + addon.manifest.name + " component \"" + component.name +
            "\" is not a fixed point of componentNameByReactViewName");
      }
      if (frameworkComponents.contains(component.name)) {
        throw TerminalLaunchPlanError(
            "Addon collision: component \"" + component.name +
            "\" declared by addon \"" + addon.manifest.name +
            "\" is owned by profile " + inventory.profile.name);
      }
      for (const auto& row : components) {
        if (row.canonicalName == component.name) {
          throw TerminalLaunchPlanError(
              "Addon collision: component \"" + component.name +
              "\" declared by addon \"" + addon.manifest.name +
              "\" is owned by " + row.owner);
        }
      }
      ComponentContract contract{
          .name = component.name,
          .classification = component.classification,
          .owner = owner,
          .kind = component.kind,
          .note = component.note,
      };
      components.push_back({
          .requestedName = component.name,
          .normalizedName = component.name,
          .canonicalName = component.name,
          .handle = 0,
          .owner = owner,
          .kind = component.kind,
          .contract = std::move(contract),
      });
    }
    if (addon.manifest.bundleCompatibility) {
      if (compatAddon) {
        throw TerminalLaunchPlanError(
            "Addon policy: bundleCompatibility is already claimed by " +
            *compatAddon);
      }
      compatAddon = addon.manifest.name;
      compatibility.targetFamily =
          addon.manifest.bundleCompatibility->targetFamily;
      compatibility.jsVisibleReactNativeVersion =
          addon.manifest.bundleCompatibility->jsVisibleReactNativeVersion;
      compatibility.level = addon.manifest.bundleCompatibility->level;
      compatibility.compatAddon = addon.manifest.name;
      compatibility.hbcTranslation = false;
    }
  }

  if (draft.impl_->bundles.empty()) {
    throw TerminalLaunchPlanError("at least one bundle is required");
  }

  PreparedLaunchPlan plan;
  plan.impl_->valid = true;
  plan.impl_->config = std::move(draft.impl_->config);
  plan.impl_->projectKind = draft.impl_->projectKind;
  plan.impl_->bindings = std::move(bindings);
  plan.impl_->inventory = std::move(inventory);
  plan.impl_->addons = std::move(merged);
  plan.impl_->bundles = std::move(draft.impl_->bundles);
  plan.impl_->initialUrl = std::move(draft.impl_->initialUrl);
  plan.impl_->compatibility = std::move(compatibility);
  plan.impl_->moduleOwners = std::move(moduleOwners);
  plan.impl_->overlays = std::move(overlays);
  plan.impl_->expectedComponents = std::move(components);
  plan.impl_->frameworkModuleNames = std::move(frameworkModules);
  plan.impl_->frameworkComponentNames = std::move(frameworkComponents);
  draft.impl_.reset();
  return plan;
}

} // namespace ReactNativeSimulator
