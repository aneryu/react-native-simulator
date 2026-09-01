#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>

#include <memory>

std::shared_ptr<facebook::react::TurboModule> createHeadlessFrameRateLogger(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
std::shared_ptr<facebook::react::TurboModule> createHeadlessModalManager(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
std::shared_ptr<facebook::react::TurboModule> createHeadlessDevLoadingView(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
std::shared_ptr<facebook::react::TurboModule> createHeadlessRedBox(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
