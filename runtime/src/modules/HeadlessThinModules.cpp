#include "HeadlessThinModules.h"

#include <string>

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

class FrameRateLoggerModule final : public react::TurboModule {
 public:
  explicit FrameRateLoggerModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("FrameRateLogger", std::move(jsInvoker)) {
    methodMap_["setGlobalOptions"] = {1, &noop};
    methodMap_["setContext"] = {1, &setContext};
    methodMap_["beginScroll"] = {0, &beginScroll};
    methodMap_["endScroll"] = {0, &endScroll};
  }

  std::string context;
  int beginCount{0};
  int endCount{0};

 private:
  static FrameRateLoggerModule& self(react::TurboModule& module) {
    return static_cast<FrameRateLoggerModule&>(module);
  }

  static jsi::Value setContext(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isString()) {
      self(module).context = args[0].getString(runtime).utf8(runtime);
    }
    return jsi::Value::undefined();
  }

  static jsi::Value beginScroll(
      jsi::Runtime&,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    ++self(module).beginCount;
    return jsi::Value::undefined();
  }

  static jsi::Value endScroll(
      jsi::Runtime&,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    ++self(module).endCount;
    return jsi::Value::undefined();
  }
};

class ModalManagerModule final : public react::TurboModule {
 public:
  explicit ModalManagerModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ModalManager", std::move(jsInvoker)) {
    methodMap_["addListener"] = {1, &noop};
    methodMap_["removeListeners"] = {1, &noop};
  }
};

class DevLoadingViewModule final : public react::TurboModule {
 public:
  explicit DevLoadingViewModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("DevLoadingView", std::move(jsInvoker)) {
    methodMap_["showMessage"] = {4, &noop};
    methodMap_["hide"] = {0, &noop};
  }
};

class RedBoxModule final : public react::TurboModule {
 public:
  explicit RedBoxModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("RedBox", std::move(jsInvoker)) {
    methodMap_["setExtraData"] = {2, &noop};
    methodMap_["dismiss"] = {0, &noop};
  }
};
} // namespace

std::shared_ptr<react::TurboModule> createHeadlessFrameRateLogger(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<FrameRateLoggerModule>(std::move(jsInvoker));
}

std::shared_ptr<react::TurboModule> createHeadlessModalManager(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<ModalManagerModule>(std::move(jsInvoker));
}

std::shared_ptr<react::TurboModule> createHeadlessDevLoadingView(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<DevLoadingViewModule>(std::move(jsInvoker));
}

std::shared_ptr<react::TurboModule> createHeadlessRedBox(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<RedBoxModule>(std::move(jsInvoker));
}
