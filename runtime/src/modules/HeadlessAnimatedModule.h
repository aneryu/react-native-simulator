#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>

#include <memory>

class SimulatorEventLoop;

std::shared_ptr<facebook::react::TurboModule> createHeadlessAnimatedModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
    std::shared_ptr<SimulatorEventLoop> eventLoop);
