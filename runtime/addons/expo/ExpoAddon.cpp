#include "ExpoAddon.h"

#include <react/nativemodule/core/ReactCommon/TurboModuleUtils.h>

#include <jsi/jsi.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace jsi = facebook::jsi;
namespace react = facebook::react;
using ReactNativeSimulator::SimulatorAddon;
using ReactNativeSimulator::SimulatorAddonCapability;
using ReactNativeSimulator::SimulatorAddonDescriptor;
using ReactNativeSimulator::kSimulatorAddonAbiVersion;

namespace {
jsi::Value undefinedResult(
    jsi::Runtime&,
    react::TurboModule&,
    const jsi::Value*,
    size_t) {
  return jsi::Value::undefined();
}

jsi::Value resolvedUndefined(
    jsi::Runtime& runtime,
    react::TurboModule&,
    const jsi::Value*,
    size_t) {
  return react::createPromiseAsJSIValue(
      runtime,
      [](jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
        promise->resolve(jsi::Value::undefined());
      });
}

jsi::Value resolvedTrue(
    jsi::Runtime& runtime,
    react::TurboModule&,
    const jsi::Value*,
    size_t) {
  return react::createPromiseAsJSIValue(
      runtime,
      [](jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
        promise->resolve(jsi::Value(true));
      });
}

class ExpoAssetModule final : public react::TurboModule {
 public:
  explicit ExpoAssetModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ExpoAsset", std::move(jsInvoker)) {
    methodMap_["downloadAsync"] = {3, &downloadAsync};
  }

 private:
  static jsi::Value downloadAsync(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    std::string uri = "rnsim://asset";
    if (count > 0 && args[0].isString()) {
      uri = args[0].getString(runtime).utf8(runtime);
    }
    return react::createPromiseAsJSIValue(
        runtime,
        [uri = std::move(uri)](
            jsi::Runtime& runtime, std::shared_ptr<react::Promise> promise) {
          promise->resolve(jsi::String::createFromUtf8(runtime, uri));
        });
  }
};

class ExpoKeepAwakeModule final : public react::TurboModule {
 public:
  explicit ExpoKeepAwakeModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ExpoKeepAwake", std::move(jsInvoker)) {
    methodMap_["activate"] = {1, &undefinedResult};
    methodMap_["deactivate"] = {1, &undefinedResult};
    methodMap_["isAvailableAsync"] = {0, &resolvedTrue};
  }
};

class ExpoSplashScreenModule final : public react::TurboModule {
 public:
  explicit ExpoSplashScreenModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ExpoSplashScreen", std::move(jsInvoker)) {
    methodMap_["preventAutoHideAsync"] = {0, &resolvedTrue};
    methodMap_["hide"] = {0, &undefinedResult};
    methodMap_["hideAsync"] = {0, &resolvedUndefined};
    methodMap_["setOptions"] = {1, &undefinedResult};
  }
};

class ExpoFontLoaderModule final : public react::TurboModule {
 public:
  explicit ExpoFontLoaderModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ExpoFontLoader", std::move(jsInvoker)) {
    methodMap_["getLoadedFonts"] = {0, &getLoadedFonts};
    methodMap_["loadAsync"] = {2, &resolvedUndefined};
  }

 private:
  static jsi::Value getLoadedFonts(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Array(runtime, 0);
  }
};

class ExpoSystemUIModule final : public react::TurboModule {
 public:
  explicit ExpoSystemUIModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ExpoSystemUI", std::move(jsInvoker)) {
    methodMap_["getBackgroundColorAsync"] = {0, &resolvedUndefined};
    methodMap_["setBackgroundColorAsync"] = {1, &resolvedUndefined};
    methodMap_["getUserInterfaceStyle"] = {
        0,
        [](jsi::Runtime& runtime,
           react::TurboModule&,
           const jsi::Value*,
           size_t) -> jsi::Value {
          return jsi::String::createFromAscii(runtime, "unspecified");
        }};
    methodMap_["setUserInterfaceStyle"] = {1, &resolvedUndefined};
  }
};

class ExpoModulesCoreModule final : public react::TurboModule {
 public:
  explicit ExpoModulesCoreModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ExpoModulesCore", std::move(jsInvoker)) {
    methodMap_["installModules"] = {0, &undefinedResult};
  }
};

class ExpoFetchModule final : public react::TurboModule {
 public:
  explicit ExpoFetchModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ExpoFetchModule", std::move(jsInvoker)) {}
};

class ExpoLinkingModule final : public react::TurboModule {
 public:
  explicit ExpoLinkingModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ExpoLinking", std::move(jsInvoker)) {
    methodMap_["getLinkingURL"] = {0, &getLinkingURL};
    methodMap_["clearInitialURL"] = {0, &undefinedResult};
    methodMap_["addListener"] = {2, &addListener};
    methodMap_["removeListeners"] = {1, &undefinedResult};
  }

 private:
  static jsi::Value getLinkingURL(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Value::null();
  }

  static jsi::Value addListener(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object subscription(runtime);
    subscription.setProperty(
        runtime,
        "remove",
        jsi::Function::createFromHostFunction(
            runtime,
            jsi::PropNameID::forAscii(runtime, "remove"),
            0,
            [](jsi::Runtime&, const jsi::Value&, const jsi::Value*, size_t)
                -> jsi::Value { return jsi::Value::undefined(); }));
    return subscription;
  }
};

class ExponentConstantsModule final : public react::TurboModule {
 public:
  explicit ExponentConstantsModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ExponentConstants", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["getWebViewUserAgentAsync"] = {0, &getWebViewUserAgentAsync};
  }

  std::vector<jsi::PropNameID> getPropertyNames(jsi::Runtime& runtime) override {
    auto names = TurboModule::getPropertyNames(runtime);
    for (const char* key : kConstantKeys) {
      names.push_back(jsi::PropNameID::forAscii(runtime, key));
    }
    return names;
  }

 protected:
  jsi::Value create(jsi::Runtime& runtime, const jsi::PropNameID& name) override {
    if (auto value = constantValue(runtime, name.utf8(runtime))) {
      return std::move(*value);
    }
    return TurboModule::create(runtime, name);
  }

 private:
  static constexpr const char* kConstantKeys[] = {
      "name",
      "appOwnership",
      "debugMode",
      "deviceName",
      "deviceYearClass",
      "executionEnvironment",
      "experienceUrl",
      "expoRuntimeVersion",
      "expoVersion",
      "isHeadless",
      "linkingUri",
      "manifest",
      "sessionId",
      "statusBarHeight",
      "systemFonts",
  };

  static std::optional<jsi::Value> constantValue(
      jsi::Runtime& runtime,
      const std::string& key) {
    if (key == "name") {
      return jsi::String::createFromAscii(runtime, "ExponentConstants");
    }
    if (key == "appOwnership" || key == "deviceYearClass" ||
        key == "expoRuntimeVersion" || key == "expoVersion") {
      return jsi::Value::null();
    }
    if (key == "manifest") {
      jsi::Object manifest(runtime);
      manifest.setProperty(
          runtime, "name", jsi::String::createFromAscii(runtime, "rnsim"));
      manifest.setProperty(
          runtime, "slug", jsi::String::createFromAscii(runtime, "rnsim"));
      manifest.setProperty(
          runtime, "scheme", jsi::String::createFromAscii(runtime, "rnsim"));
      return manifest;
    }
    if (key == "debugMode") {
      return jsi::Value(true);
    }
    if (key == "isHeadless") {
      return jsi::Value(false);
    }
    if (key == "deviceName") {
      return jsi::String::createFromAscii(runtime, "rnsim");
    }
    if (key == "executionEnvironment") {
      return jsi::String::createFromAscii(runtime, "bare");
    }
    if (key == "experienceUrl" || key == "linkingUri") {
      return jsi::String::createFromAscii(runtime, "rnsim://");
    }
    if (key == "sessionId") {
      return jsi::String::createFromAscii(runtime, "rnsim-session");
    }
    if (key == "statusBarHeight") {
      return jsi::Value(24);
    }
    if (key == "systemFonts") {
      return jsi::Array(runtime, 0);
    }
    return std::nullopt;
  }

  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object constants(runtime);
    for (const char* key : kConstantKeys) {
      if (auto value = constantValue(runtime, key)) {
        constants.setProperty(runtime, key, std::move(*value));
      }
    }
    return constants;
  }

  static jsi::Value getWebViewUserAgentAsync(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return react::createPromiseAsJSIValue(
        runtime,
        [](jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          promise->resolve(jsi::Value::null());
        });
  }
};

class ExpoAddon final : public SimulatorAddon {
 public:
  std::string name() const override {
    return "expo";
  }

  std::vector<std::string> moduleNames() const override {
    return {
        "ExpoModulesCore",
        "ExpoAsset",
        "ExpoKeepAwake",
        "ExpoSplashScreen",
        "ExpoFontLoader",
        "ExpoSystemUI",
        "ExponentConstants",
        "ExpoFetchModule",
        "ExpoLinking",
    };
  }

  std::vector<SimulatorAddonCapability> moduleCapabilities() const override {
    return {
        {"ExpoModulesCore", "host-adapted"},
        {"ExpoAsset", "host-adapted"},
        {"ExpoKeepAwake", "host-adapted"},
        {"ExpoSplashScreen", "host-adapted"},
        {"ExpoFontLoader", "host-adapted"},
        {"ExpoSystemUI", "host-adapted"},
        {"ExponentConstants", "host-adapted"},
        {"ExpoFetchModule", "host-adapted"},
        {"ExpoLinking", "host-adapted"},
    };
  }

  std::shared_ptr<react::TurboModule> getTurboModule(
      jsi::Runtime&,
      const std::string& moduleName,
      const std::shared_ptr<react::CallInvoker>& jsInvoker) override {
    if (moduleName == "ExpoModulesCore") {
      return std::make_shared<ExpoModulesCoreModule>(jsInvoker);
    }
    if (moduleName == "ExpoAsset") {
      return std::make_shared<ExpoAssetModule>(jsInvoker);
    }
    if (moduleName == "ExpoKeepAwake") {
      return std::make_shared<ExpoKeepAwakeModule>(jsInvoker);
    }
    if (moduleName == "ExpoSplashScreen") {
      return std::make_shared<ExpoSplashScreenModule>(jsInvoker);
    }
    if (moduleName == "ExpoFontLoader") {
      return std::make_shared<ExpoFontLoaderModule>(jsInvoker);
    }
    if (moduleName == "ExpoSystemUI") {
      return std::make_shared<ExpoSystemUIModule>(jsInvoker);
    }
    if (moduleName == "ExponentConstants") {
      return std::make_shared<ExponentConstantsModule>(jsInvoker);
    }
    if (moduleName == "ExpoFetchModule") {
      return std::make_shared<ExpoFetchModule>(jsInvoker);
    }
    if (moduleName == "ExpoLinking") {
      return std::make_shared<ExpoLinkingModule>(jsInvoker);
    }
    return nullptr;
  }

  void installJSI(
      jsi::Runtime& runtime,
      const std::shared_ptr<react::CallInvoker>& jsInvoker) override {
    constexpr const char* kBootstrap = R"JS(
(function () {
  class EventEmitter {
    addListener() { return { remove() {} }; }
    removeListener() {}
    removeAllListeners() {}
    emit() {}
    listenerCount() { return 0; }
  }
  class SharedObject extends EventEmitter {}
  class SharedRef extends SharedObject {}
  class NativeModule extends EventEmitter {}
  const expo = globalThis.expo && typeof globalThis.expo === 'object'
      ? globalThis.expo
      : {};
  expo.EventEmitter = EventEmitter;
  expo.SharedObject = SharedObject;
  expo.SharedRef = SharedRef;
  expo.NativeModule = NativeModule;
  expo.uuidv4 = function () {
    return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function (c) {
      const r = Math.random() * 16 | 0;
      const v = c === 'x' ? r : (r & 0x3 | 0x8);
      return v.toString(16);
    });
  };
  expo.uuidv5 = function () { return expo.uuidv4(); };
  expo.getViewConfig = function () { return null; };
  expo.reloadAppAsync = function () { return Promise.resolve(); };
  if (!expo.modules || typeof expo.modules !== 'object') {
    expo.modules = {};
  }
  class NativeResponse extends SharedObject {
    constructor() {
      super();
      this.bodyUsed = false;
      this._rawHeaders = [];
      this.status = 0;
      this.statusText = '';
      this.url = '';
      this.redirected = false;
      this._text = '';
    }
    _bytes() {
      const text = this._text || '';
      const view = new Uint8Array(text.length);
      for (let i = 0; i < text.length; i++) {
        view[i] = text.charCodeAt(i) & 0xff;
      }
      return view;
    }
    startStreaming() { return Promise.resolve(this._bytes()); }
    cancelStreaming() {}
    arrayBuffer() { return Promise.resolve(this._bytes().buffer); }
    text() { return Promise.resolve(this._text || ''); }
  }
  class NativeRequest extends SharedObject {
    constructor(response) {
      super();
      this._response = response;
    }
    start(url, init, body) {
      const response = this._response;
      return new Promise((resolve, reject) => {
        if (typeof XMLHttpRequest !== 'function') {
          reject(new Error('ExpoFetchModule: XMLHttpRequest is unavailable'));
          return;
        }
        const xhr = new XMLHttpRequest();
        xhr.onload = function () {
          response.status = xhr.status;
          response.statusText = xhr.statusText || '';
          response.url = String(url);
          response.redirected = false;
          response._rawHeaders = [];
          response._text = xhr.responseText || '';
          resolve(response);
        };
        xhr.onerror = function () {
          reject(new Error('ExpoFetchModule request failed: ' + url));
        };
        xhr.open((init && init.method) || 'GET', String(url));
        const headers = init && init.headers;
        if (Array.isArray(headers)) {
          for (let i = 0; i < headers.length; i++) {
            const pair = headers[i];
            if (pair && pair.length >= 2) {
              xhr.setRequestHeader(String(pair[0]), String(pair[1]));
            }
          }
        }
        xhr.send(body || null);
      });
    }
    cancel() {}
  }
  expo.modules.ExpoFetchModule = {
    NativeRequest: NativeRequest,
    NativeResponse: NativeResponse,
  };
  globalThis.expo = expo;
})();
)JS";
    runtime.evaluateJavaScript(
        std::make_shared<jsi::StringBuffer>(kBootstrap),
        "rnsim-expo-runtime");
    auto expoValue = runtime.global().getProperty(runtime, "expo");
    if (!expoValue.isObject()) {
      return;
    }
    auto expo = expoValue.asObject(runtime);
    auto modulesValue = expo.getProperty(runtime, "modules");
    if (!modulesValue.isObject()) {
      return;
    }
    auto modules = modulesValue.asObject(runtime);
    for (const auto& moduleName : moduleNames()) {
      if (moduleName == "ExpoModulesCore" ||
          moduleName == "ExpoFetchModule") {
        continue;
      }
      auto module = getTurboModule(runtime, moduleName, jsInvoker);
      if (module == nullptr) {
        continue;
      }
      modules.setProperty(
          runtime,
          moduleName.c_str(),
          jsi::Object::createFromHostObject(runtime, std::move(module)));
    }
  }
};
} // namespace

std::unique_ptr<SimulatorAddon> createExpoAddon() {
  return std::make_unique<ExpoAddon>();
}

#if defined(RNS_EXPO_ADDON_DYLIB)
extern "C" const SimulatorAddonDescriptor*
react_native_simulator_addon_v2() {
  static const SimulatorAddonDescriptor descriptor{
      .abiVersion = kSimulatorAddonAbiVersion,
      .name = "expo",
      .reactNativeVersion = RNS_REACT_NATIVE_VERSION,
      .hermesVersion = RNS_HERMES_VERSION,
      .create = []() -> SimulatorAddon* { return createExpoAddon().release(); },
      .destroy = [](SimulatorAddon* addon) { delete addon; },
  };
  return &descriptor;
}
#endif
