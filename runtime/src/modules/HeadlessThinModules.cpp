#include "HeadlessThinModules.h"

#include <optional>
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

// In-memory stand-in for Android SharedPreferences. Static so hook settings
// survive TurboModule reloads the same way the RN Android module does.
std::optional<std::string> reactDevToolsHookSettings;

class ReactDevToolsSettingsManagerModule final : public react::TurboModule {
 public:
  explicit ReactDevToolsSettingsManagerModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ReactDevToolsSettingsManager", std::move(jsInvoker)) {
    methodMap_["setGlobalHookSettings"] = {1, &setGlobalHookSettings};
    methodMap_["getGlobalHookSettings"] = {0, &getGlobalHookSettings};
  }

 private:
  static jsi::Value setGlobalHookSettings(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isString()) {
      reactDevToolsHookSettings = args[0].getString(runtime).utf8(runtime);
    }
    return jsi::Value::undefined();
  }

  static jsi::Value getGlobalHookSettings(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    if (!reactDevToolsHookSettings) {
      return jsi::Value::null();
    }
    return jsi::String::createFromUtf8(runtime, *reactDevToolsHookSettings);
  }
};

struct ReactDevToolsReloadAndProfileConfig {
  bool shouldReloadAndProfile{false};
  bool recordChangeDescriptions{false};
};

ReactDevToolsReloadAndProfileConfig reactDevToolsReloadAndProfileConfig;

class ReactDevToolsRuntimeSettingsModule final : public react::TurboModule {
 public:
  explicit ReactDevToolsRuntimeSettingsModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ReactDevToolsRuntimeSettingsModule", std::move(jsInvoker)) {
    methodMap_["setReloadAndProfileConfig"] = {1, &setReloadAndProfileConfig};
    methodMap_["getReloadAndProfileConfig"] = {0, &getReloadAndProfileConfig};
  }

 private:
  static jsi::Value setReloadAndProfileConfig(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count == 0 || !args[0].isObject()) {
      return jsi::Value::undefined();
    }
    auto config = args[0].getObject(runtime);
    if (config.hasProperty(runtime, "shouldReloadAndProfile")) {
      auto value = config.getProperty(runtime, "shouldReloadAndProfile");
      if (value.isBool()) {
        reactDevToolsReloadAndProfileConfig.shouldReloadAndProfile =
            value.getBool();
      }
    }
    if (config.hasProperty(runtime, "recordChangeDescriptions")) {
      auto value = config.getProperty(runtime, "recordChangeDescriptions");
      if (value.isBool()) {
        reactDevToolsReloadAndProfileConfig.recordChangeDescriptions =
            value.getBool();
      }
    }
    return jsi::Value::undefined();
  }

  static jsi::Value getReloadAndProfileConfig(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object config(runtime);
    config.setProperty(
        runtime,
        "shouldReloadAndProfile",
        reactDevToolsReloadAndProfileConfig.shouldReloadAndProfile);
    config.setProperty(
        runtime,
        "recordChangeDescriptions",
        reactDevToolsReloadAndProfileConfig.recordChangeDescriptions);
    return config;
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

std::shared_ptr<react::TurboModule> createHeadlessReactDevToolsSettingsManager(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<ReactDevToolsSettingsManagerModule>(
      std::move(jsInvoker));
}

std::shared_ptr<react::TurboModule>
createHeadlessReactDevToolsRuntimeSettingsModule(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<ReactDevToolsRuntimeSettingsModule>(
      std::move(jsInvoker));
}
