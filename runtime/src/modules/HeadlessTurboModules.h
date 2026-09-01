#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <jsi/jsi.h>

#include <functional>
#include <memory>
#include <string>

class SimulatorEventLoop;
class RuntimeProfile;

namespace ReactNativeSimulator {
class SimulatorAddonRegistry;
}

void setDevSettingsReloadHandler(std::function<void()> handler);

std::shared_ptr<facebook::react::TurboModule> getHeadlessTurboModule(
    facebook::jsi::Runtime& runtime,
    const std::string& name,
    const RuntimeProfile& profile,
    ReactNativeSimulator::SimulatorAddonRegistry& addons,
    const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker,
    const std::shared_ptr<SimulatorEventLoop>& eventLoop = {});
