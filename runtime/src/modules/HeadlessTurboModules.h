#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <jsi/jsi.h>

#include <functional>
#include <memory>
#include <string>

class SimulatorEventLoop;

void setDevSettingsReloadHandler(std::function<void()> handler);

std::shared_ptr<facebook::react::TurboModule> getHeadlessHostTurboModule(
    facebook::jsi::Runtime& runtime,
    const std::string& name,
    const std::shared_ptr<facebook::react::CallInvoker>& jsInvoker,
    const std::shared_ptr<SimulatorEventLoop>& eventLoop = {});
