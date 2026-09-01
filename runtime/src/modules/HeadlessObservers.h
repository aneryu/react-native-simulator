#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>

#include <memory>

std::shared_ptr<facebook::react::TurboModule>
createHeadlessIntersectionObserver(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

std::shared_ptr<facebook::react::TurboModule> createHeadlessMutationObserver(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
