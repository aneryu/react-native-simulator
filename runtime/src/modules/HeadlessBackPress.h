#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>

#include <functional>
#include <memory>

namespace facebook::jsi {
class Runtime;
}

// Android hardware back. Interactive chrome and dispatchActions share this
// path: hide IME first, then emit RCTDeviceEventEmitter hardwareBackPress.
struct HeadlessBackPressController {
  std::function<void()> emit;
  std::function<void(facebook::jsi::Runtime&)> invokeDefault;
  std::function<void(std::function<void(facebook::jsi::Runtime&)>)> runOnJs;

  bool press();
};

HeadlessBackPressController& headlessBackPress();

std::shared_ptr<facebook::react::TurboModule> createHeadlessDeviceEventManager(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
