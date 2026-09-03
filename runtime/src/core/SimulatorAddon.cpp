#include "AddonHostSupport.h"

#include <react-native-simulator/Engine.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ReactNativeSimulator {

AddonFabricRegistrar::AddonFabricRegistrar(
    HostSession& session,
    std::string addonName,
    const AddonManifest& manifest)
    : session_(&session),
      addonName_(std::move(addonName)),
      manifest_(&manifest) {
  session_->addonName = addonName_;
  session_->manifest = manifest_;
}

void AddonFabricRegistrar::registerDescriptor(
    facebook::react::ComponentDescriptorProvider provider) {
  if (session_ == nullptr || manifest_ == nullptr) {
    throw std::logic_error("AddonFabricRegistrar is not bound to a session");
  }
  if (provider.name == nullptr || provider.name[0] == '\0' ||
      provider.constructor == nullptr || provider.handle == 0) {
    throw AddonContractViolation(
        addonName_,
        "configureFabric",
        provider.name != nullptr ? provider.name : "",
        0,
        "registerDescriptor requires a non-empty name, constructor, and handle");
  }
  const auto component = std::find_if(
      manifest_->components.begin(),
      manifest_->components.end(),
      [&](const auto& candidate) { return candidate.name == provider.name; });
  if (component == manifest_->components.end()) {
    throw AddonContractViolation(
        addonName_,
        "configureFabric",
        provider.name,
        0,
        "registerDescriptor for a component not in this addon's manifest");
  }
  if (component->kind != AddonComponentKind::FabricDescriptor) {
    throw AddonContractViolation(
        addonName_,
        "configureFabric",
        provider.name,
        0,
        "DescriptorOnlyMock components must not register a provider");
  }
  for (const auto& staged : session_->providers) {
    if (staged.provider.name != nullptr &&
        std::string(staged.provider.name) == provider.name) {
      throw AddonContractViolation(
          addonName_,
          "configureFabric",
          provider.name,
          0,
          "duplicate descriptor registration");
    }
    if (staged.provider.handle == provider.handle) {
      throw AddonContractViolation(
          addonName_,
          "configureFabric",
          provider.name,
          0,
          "duplicate component handle");
    }
  }
  session_->providers.push_back({addonName_, provider});
}

void AddonFabricRegistrar::onMount(
    std::string_view ownedComponent,
    AddonMountHandler handler) {
  if (session_ == nullptr || manifest_ == nullptr) {
    throw std::logic_error("AddonFabricRegistrar is not bound to a session");
  }
  const std::string name(ownedComponent);
  const auto component = std::find_if(
      manifest_->components.begin(),
      manifest_->components.end(),
      [&](const auto& candidate) { return candidate.name == name; });
  if (component == manifest_->components.end()) {
    throw AddonContractViolation(
        addonName_, "configureFabric", name, 0, "onMount for an unowned component");
  }
  if (session_->mountHandlers.contains(name)) {
    throw AddonContractViolation(
        addonName_,
        "configureFabric",
        name,
        0,
        "duplicate mount handler");
  }
  session_->mountHandlers[name] = std::move(handler);
}

void AddonFabricRegistrar::onCommand(
    std::string_view ownedComponent,
    std::string_view declaredCommand,
    AddonCommandHandler handler) {
  if (session_ == nullptr || manifest_ == nullptr) {
    throw std::logic_error("AddonFabricRegistrar is not bound to a session");
  }
  const std::string name(ownedComponent);
  const std::string command(declaredCommand);
  const auto component = std::find_if(
      manifest_->components.begin(),
      manifest_->components.end(),
      [&](const auto& candidate) { return candidate.name == name; });
  if (component == manifest_->components.end()) {
    throw AddonContractViolation(
        addonName_,
        "configureFabric",
        name,
        0,
        "onCommand for an unowned component");
  }
  if (std::find(component->commands.begin(), component->commands.end(), command) ==
      component->commands.end()) {
    throw AddonContractViolation(
        addonName_,
        "configureFabric",
        name,
        0,
        "onCommand for undeclared command " + command);
  }
  auto& commands = session_->commandHandlers[name];
  if (commands.contains(command)) {
    throw AddonContractViolation(
        addonName_,
        "configureFabric",
        name,
        0,
        "duplicate command handler " + command);
  }
  commands[command] = std::move(handler);
}

bool AddonRuntimeExecutor::post(
    std::function<void(facebook::jsi::Runtime&)> fn) const noexcept {
  if (!state_ || !state_->open.load()) {
    if (state_) {
      state_->droppedPosts.fetch_add(1);
    }
    return false;
  }
  try {
    if (!state_->enqueue) {
      state_->droppedPosts.fetch_add(1);
      return false;
    }
    return state_->enqueue(std::move(fn));
  } catch (...) {
    return false;
  }
}

AddonRuntimeExecutor makeAddonRuntimeExecutor(
    std::shared_ptr<AddonRuntimeExecutor::State> state) {
  AddonRuntimeExecutor executor;
  executor.state_ = std::move(state);
  return executor;
}

} // namespace ReactNativeSimulator
