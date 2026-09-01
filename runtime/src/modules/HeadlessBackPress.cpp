#include "HeadlessBackPress.h"
#include "HeadlessKeyboard.h"

#include <jsi/jsi.h>

#include <chrono>
#include <vector>

namespace jsi = facebook::jsi;
namespace react = facebook::react;

namespace {
jsi::Value noop(
    jsi::Runtime&,
    react::TurboModule&,
    const jsi::Value*,
    size_t) {
  return jsi::Value::undefined();
}

class DeviceEventManagerModule final : public react::TurboModule {
 public:
  explicit DeviceEventManagerModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("DeviceEventManager", std::move(jsInvoker)) {
    methodMap_["invokeDefaultBackPressHandler"] = {0, &invokeDefault};
    methodMap_["addListener"] = {1, &noop};
    methodMap_["removeListeners"] = {1, &noop};
  }

  void emitHardwareBackPressed() {
    const auto timeStamp = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
    emitDeviceEvent(
        "hardwareBackPress",
        [timeStamp](jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object event(runtime);
          event.setProperty(runtime, "timeStamp", timeStamp);
          args.emplace_back(std::move(event));
        });
  }

 private:
  static jsi::Value invokeDefault(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    if (headlessBackPress().invokeDefault) {
      headlessBackPress().invokeDefault(runtime);
    }
    return jsi::Value::undefined();
  }
};
} // namespace

HeadlessBackPressController& headlessBackPress() {
  static HeadlessBackPressController controller;
  return controller;
}

static void emitHardwareBackPressEvent(jsi::Runtime& runtime) {
  auto emitterValue =
      runtime.global().getProperty(runtime, "__rctDeviceEventEmitter");
  if (!emitterValue.isObject()) {
    return;
  }
  auto emitter = emitterValue.getObject(runtime);
  auto emitValue = emitter.getProperty(runtime, "emit");
  if (!emitValue.isObject() ||
      !emitValue.getObject(runtime).isFunction(runtime)) {
    return;
  }
  auto emit = emitValue.getObject(runtime).asFunction(runtime);
  const auto timeStamp = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
  jsi::Object event(runtime);
  event.setProperty(runtime, "timeStamp", timeStamp);
  emit.callWithThis(
      runtime,
      emitter,
      jsi::String::createFromAscii(runtime, "hardwareBackPress"),
      std::move(event));
}

bool HeadlessBackPressController::press() {
  if (headlessKeyboard().visible) {
    headlessKeyboard().setVisible(false);
    return true;
  }
  if (runOnJs) {
    runOnJs(&emitHardwareBackPressEvent);
    return true;
  }
  if (emit) {
    emit();
    return true;
  }
  return false;
}

std::shared_ptr<react::TurboModule> createHeadlessDeviceEventManager(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  auto module =
      std::make_shared<DeviceEventManagerModule>(std::move(jsInvoker));
  headlessBackPress().emit =
      [weak = std::weak_ptr<DeviceEventManagerModule>(module)]() {
        if (auto locked = weak.lock()) {
          locked->emitHardwareBackPressed();
        }
      };
  return module;
}
