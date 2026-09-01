#include "HeadlessTurboModules.h"
#include "HeadlessAnimatedModule.h"
#include "HeadlessNativeDOM.h"
#include "HeadlessObservers.h"
#include "RuntimeProfile.h"

#include <react-native-simulator/SimulatorAddon.h>
#include "HeadlessRNModules.h"

#include <react/featureflags/ReactNativeFeatureFlags.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jsi = facebook::jsi;
namespace react = facebook::react;

namespace {
class HeadlessFeatureFlagsTurboModule final : public react::TurboModule {
 public:
  explicit HeadlessFeatureFlagsTurboModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule(
            "NativeReactNativeFeatureFlagsCxx",
            std::move(jsInvoker)) {
#include "HeadlessFeatureFlagsMethodMap.inc"
  }
};

class HeadlessMicrotasksTurboModule final : public react::TurboModule {
 public:
  explicit HeadlessMicrotasksTurboModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("NativeMicrotasksCxx", std::move(jsInvoker)) {
    methodMap_["queueMicrotask"] = {
        1,
        [](jsi::Runtime& runtime,
           react::TurboModule&,
           const jsi::Value* args,
           size_t count) -> jsi::Value {
          if (count != 1 || !args[0].isObject() ||
              !args[0].getObject(runtime).isFunction(runtime)) {
            throw jsi::JSError(
                runtime, "NativeMicrotasks.queueMicrotask expects a function");
          }
          runtime.queueMicrotask(
              args[0].getObject(runtime).getFunction(runtime));
          return jsi::Value::undefined();
        }};
  }
};

class HeadlessIdleCallbacksTurboModule final : public react::TurboModule {
 public:
  explicit HeadlessIdleCallbacksTurboModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("NativeIdleCallbacksCxx", std::move(jsInvoker)) {
    methodMap_["requestIdleCallback"] = {2, &requestIdleCallback};
    methodMap_["cancelIdleCallback"] = {1, &cancelIdleCallback};
  }

 private:
  std::unordered_map<int, bool> cancelled_;
  int nextId_{1};

  static HeadlessIdleCallbacksTurboModule& self(react::TurboModule& module) {
    return static_cast<HeadlessIdleCallbacksTurboModule&>(module);
  }

  static jsi::Value requestIdleCallback(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isObject() ||
        !args[0].getObject(runtime).isFunction(runtime)) {
      throw jsi::JSError(
          runtime, "requestIdleCallback expects a function");
    }
    auto* idle = &self(module);
    const int handle = idle->nextId_++;
    idle->cancelled_[handle] = false;
    auto callback = std::make_shared<jsi::Function>(
        args[0].getObject(runtime).getFunction(runtime));
    runtime.queueMicrotask(jsi::Function::createFromHostFunction(
        runtime,
        jsi::PropNameID::forAscii(runtime, "idleCallback"),
        0,
        [idle, handle, callback](
            jsi::Runtime& runtime,
            const jsi::Value&,
            const jsi::Value*,
            size_t) -> jsi::Value {
          const auto found = idle->cancelled_.find(handle);
          if (found != idle->cancelled_.end() && !found->second) {
            jsi::Object deadline(runtime);
            deadline.setProperty(runtime, "didTimeout", false);
            deadline.setProperty(
                runtime,
                "timeRemaining",
                jsi::Function::createFromHostFunction(
                    runtime,
                    jsi::PropNameID::forAscii(runtime, "timeRemaining"),
                    0,
                    [](jsi::Runtime&,
                       const jsi::Value&,
                       const jsi::Value*,
                       size_t) -> jsi::Value { return 16.0; }));
            callback->call(runtime, deadline);
          }
          idle->cancelled_.erase(handle);
          return jsi::Value::undefined();
        }));
    return handle;
  }

  static jsi::Value cancelIdleCallback(
      jsi::Runtime&,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isNumber()) {
      return jsi::Value::undefined();
    }
    self(module).cancelled_[static_cast<int>(args[0].getNumber())] = true;
    return jsi::Value::undefined();
  }
};

class HeadlessPerformanceTurboModule final : public react::TurboModule {
 public:
  explicit HeadlessPerformanceTurboModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("NativePerformanceCxx", std::move(jsInvoker)),
        origin_(std::chrono::steady_clock::now()) {
    methodMap_["now"] = {0, &now};
    methodMap_["timeOrigin"] = {0, &timeOrigin};
    methodMap_["reportMark"] = {3, &reportMark};
    methodMap_["reportMeasure"] = {4, &reportMeasure};
    methodMap_["getMarkTime"] = {1, &getMarkTime};
    methodMap_["clearMarks"] = {1, &clearMarks};
    methodMap_["clearMeasures"] = {1, &clearMeasures};
    methodMap_["getEntries"] = {0, &getEntries};
    methodMap_["getEntriesByName"] = {2, &getEntriesByName};
    methodMap_["getEntriesByType"] = {1, &getEntriesByType};
    methodMap_["getEventCounts"] = {0, &emptyArray};
    methodMap_["getSimpleMemoryInfo"] = {0, &emptyObject};
    methodMap_["getReactNativeStartupTiming"] = {0, &emptyObject};
    methodMap_["createObserver"] = {1, &undefined};
    methodMap_["getDroppedEntriesCount"] = {1, &zero};
    methodMap_["observe"] = {2, &noop};
    methodMap_["disconnect"] = {1, &noop};
    methodMap_["takeRecords"] = {2, &emptyArray};
    methodMap_["getSupportedPerformanceEntryTypes"] = {
        0, &getSupportedPerformanceEntryTypes};
    methodMap_["clearEventCountsForTesting"] = {0, &noop};
  }

 private:
  static constexpr int kMark = 1;
  static constexpr int kMeasure = 2;
  static constexpr size_t kMaxEntries = 1024;

  struct Entry {
    std::string name;
    int entryType{kMark};
    double startTime{0};
    double duration{0};
  };

  std::chrono::steady_clock::time_point origin_;
  std::vector<Entry> entries_;

  static HeadlessPerformanceTurboModule& self(react::TurboModule& module) {
    return static_cast<HeadlessPerformanceTurboModule&>(module);
  }

  void append(Entry entry) {
    if (entries_.size() >= kMaxEntries) {
      entries_.erase(entries_.begin(), entries_.begin() + kMaxEntries / 4);
    }
    entries_.push_back(std::move(entry));
  }

  static jsi::Object toJs(jsi::Runtime& runtime, const Entry& entry) {
    jsi::Object object(runtime);
    object.setProperty(
        runtime, "name", jsi::String::createFromUtf8(runtime, entry.name));
    object.setProperty(runtime, "entryType", entry.entryType);
    object.setProperty(runtime, "startTime", entry.startTime);
    object.setProperty(runtime, "duration", entry.duration);
    return object;
  }

  static jsi::Array toJs(
      jsi::Runtime& runtime,
      const std::vector<Entry>& entries) {
    jsi::Array array(runtime, entries.size());
    for (size_t index = 0; index < entries.size(); ++index) {
      array.setValueAtIndex(runtime, index, toJs(runtime, entries[index]));
    }
    return array;
  }

  static std::string optionalName(
      jsi::Runtime& runtime,
      const jsi::Value* args,
      size_t count,
      size_t index) {
    if (count <= index || args[index].isUndefined() || args[index].isNull()) {
      return {};
    }
    if (args[index].isString()) {
      return args[index].getString(runtime).utf8(runtime);
    }
    return {};
  }

  static jsi::Value now(
      jsi::Runtime&,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    const auto elapsed = std::chrono::steady_clock::now() - self(module).origin_;
    return std::chrono::duration<double, std::milli>(elapsed).count();
  }

  static jsi::Value timeOrigin(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return std::chrono::duration<double, std::milli>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  static jsi::Value reportMark(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isString()) {
      return jsi::Value::undefined();
    }
    Entry entry;
    entry.name = args[0].getString(runtime).utf8(runtime);
    entry.entryType = kMark;
    entry.startTime = count > 1 && args[1].isNumber() ? args[1].getNumber() : 0;
    self(module).append(std::move(entry));
    return jsi::Value::undefined();
  }

  static jsi::Value reportMeasure(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isString()) {
      return jsi::Value::undefined();
    }
    Entry entry;
    entry.name = args[0].getString(runtime).utf8(runtime);
    entry.entryType = kMeasure;
    entry.startTime = count > 1 && args[1].isNumber() ? args[1].getNumber() : 0;
    entry.duration = count > 2 && args[2].isNumber() ? args[2].getNumber() : 0;
    self(module).append(std::move(entry));
    return jsi::Value::undefined();
  }

  static jsi::Value getMarkTime(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isString()) {
      return jsi::Value::undefined();
    }
    const auto name = args[0].getString(runtime).utf8(runtime);
    const auto& entries = self(module).entries_;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
      if (it->entryType == kMark && it->name == name) {
        return it->startTime;
      }
    }
    return jsi::Value::undefined();
  }

  static jsi::Value clearNamed(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count,
      int type) {
    auto& entries = self(module).entries_;
    const auto name = optionalName(runtime, args, count, 0);
    entries.erase(
        std::remove_if(
            entries.begin(),
            entries.end(),
            [&](const Entry& entry) {
              return entry.entryType == type &&
                  (name.empty() || entry.name == name);
            }),
        entries.end());
    return jsi::Value::undefined();
  }

  static jsi::Value clearMarks(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    return clearNamed(runtime, module, args, count, kMark);
  }

  static jsi::Value clearMeasures(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    return clearNamed(runtime, module, args, count, kMeasure);
  }

  static jsi::Value getEntries(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    return toJs(runtime, self(module).entries_);
  }

  static jsi::Value getEntriesByName(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isString()) {
      return jsi::Array(runtime, 0);
    }
    const auto name = args[0].getString(runtime).utf8(runtime);
    const bool typed = count > 1 && args[1].isNumber();
    const int type = typed ? static_cast<int>(args[1].getNumber()) : 0;
    std::vector<Entry> matched;
    for (const auto& entry : self(module).entries_) {
      if (entry.name == name && (!typed || entry.entryType == type)) {
        matched.push_back(entry);
      }
    }
    return toJs(runtime, matched);
  }

  static jsi::Value getEntriesByType(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 1 || !args[0].isNumber()) {
      return jsi::Array(runtime, 0);
    }
    const int type = static_cast<int>(args[0].getNumber());
    std::vector<Entry> matched;
    for (const auto& entry : self(module).entries_) {
      if (entry.entryType == type) {
        matched.push_back(entry);
      }
    }
    return toJs(runtime, matched);
  }

  static jsi::Value getSupportedPerformanceEntryTypes(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Array types(runtime, 2);
    types.setValueAtIndex(runtime, 0, kMark);
    types.setValueAtIndex(runtime, 1, kMeasure);
    return types;
  }

  static jsi::Value noop(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Value::undefined();
  }

  static jsi::Value undefined(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Value::undefined();
  }

  static jsi::Value zero(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return 0;
  }

  static jsi::Value emptyArray(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Array(runtime, 0);
  }

  static jsi::Value emptyObject(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Object(runtime);
  }
};

class HeadlessSampleTurboModule final : public react::TurboModule {
 public:
  explicit HeadlessSampleTurboModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("HeadlessSampleModule", std::move(jsInvoker)) {
    methodMap_["add"] = {
        2,
        [](jsi::Runtime& runtime,
           react::TurboModule&,
           const jsi::Value* args,
           size_t count) -> jsi::Value {
          if (count != 2 || !args[0].isNumber() || !args[1].isNumber()) {
            throw jsi::JSError(runtime, "HeadlessSampleModule.add expects two numbers");
          }
          return args[0].getNumber() + args[1].getNumber();
        }};
    methodMap_["echo"] = {
        1,
        [](jsi::Runtime& runtime,
           react::TurboModule&,
           const jsi::Value* args,
           size_t count) -> jsi::Value {
          if (count != 1 || !args[0].isString()) {
            throw jsi::JSError(runtime, "HeadlessSampleModule.echo expects a string");
          }
          return jsi::String::createFromUtf8(
              runtime, args[0].getString(runtime).utf8(runtime));
        }};
  }
};

class EmptyTurboModule final : public react::TurboModule {
 public:
  EmptyTurboModule(
      std::string name,
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule(std::move(name), std::move(jsInvoker)) {}
};

class ExceptionsTurboModule final : public react::TurboModule {
 public:
  explicit ExceptionsTurboModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ExceptionsManager", std::move(jsInvoker)) {
    for (const auto* method : {
             "reportFatalException",
             "reportSoftException",
             "reportException",
             "updateExceptionMessage",
             "dismissRedbox"}) {
      methodMap_[method] = {
          1,
          [](jsi::Runtime& runtime,
             react::TurboModule&,
             const jsi::Value* args,
             size_t count) -> jsi::Value {
            if (count > 0 && args[0].isString()) {
              std::cerr << "RN exception: "
                        << args[0].getString(runtime).utf8(runtime) << '\n';
            }
            return jsi::Value::undefined();
          }};
    }
  }
};

class LogBoxTurboModule final : public react::TurboModule {
 public:
  explicit LogBoxTurboModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("LogBox", std::move(jsInvoker)) {
    addNoop("show", 0);
    addNoop("hide", 0);
  }

 private:
  void addNoop(const char* name, size_t argumentCount) {
    methodMap_[name] = {
        argumentCount,
        [](jsi::Runtime&,
           react::TurboModule&,
           const jsi::Value*,
           size_t) -> jsi::Value { return jsi::Value::undefined(); }};
  }
};

std::mutex devSettingsReloadMutex;
std::function<void()> devSettingsReloadHandler;

void requestDevSettingsReload() {
  std::function<void()> handler;
  {
    std::lock_guard lock(devSettingsReloadMutex);
    handler = devSettingsReloadHandler;
  }
  if (handler) {
    handler();
  }
}

class DevSettingsTurboModule final : public react::TurboModule {
 public:
  explicit DevSettingsTurboModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("DevSettings", std::move(jsInvoker)) {
    methodMap_["reload"] = {0, &invokeReload};
    methodMap_["reloadWithReason"] = {1, &invokeReload};
    addNoop("onFastRefresh", 0);
    addNoop("setHotLoadingEnabled", 1);
    addNoop("setProfilingEnabled", 1);
    addNoop("toggleElementInspector", 0);
    addNoop("addMenuItem", 1);
    addNoop("openDebugger", 0);
    addNoop("addListener", 1);
    addNoop("removeListeners", 1);
    addNoop("setIsShakeToShowDevMenuEnabled", 1);
  }

 private:
  static jsi::Value invokeReload(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    requestDevSettingsReload();
    return jsi::Value::undefined();
  }

  void addNoop(const char* name, size_t argumentCount) {
    methodMap_[name] = {
        argumentCount,
        [](jsi::Runtime&,
           react::TurboModule&,
           const jsi::Value*,
           size_t) -> jsi::Value { return jsi::Value::undefined(); }};
  }
};
} // namespace

void setDevSettingsReloadHandler(std::function<void()> handler) {
  std::lock_guard lock(devSettingsReloadMutex);
  devSettingsReloadHandler = std::move(handler);
}

std::shared_ptr<react::TurboModule> getHeadlessTurboModule(
    jsi::Runtime& runtime,
    const std::string& name,
    const RuntimeProfile& profile,
    ReactNativeSimulator::SimulatorAddonRegistry& addons,
    const std::shared_ptr<react::CallInvoker>& jsInvoker,
    const std::shared_ptr<SimulatorEventLoop>& eventLoop) {
  if (name == "NativeAnimatedModule") {
    return createHeadlessAnimatedModule(jsInvoker, eventLoop);
  }
  if (name == "NativeReactNativeFeatureFlagsCxx") {
    return std::make_shared<HeadlessFeatureFlagsTurboModule>(jsInvoker);
  }
  if (name == "NativeMicrotasksCxx") {
    return std::make_shared<HeadlessMicrotasksTurboModule>(jsInvoker);
  }
  if (name == "NativeDOMCxx") {
    return createHeadlessNativeDOM(jsInvoker);
  }
  if (name == "NativeIdleCallbacksCxx") {
    return std::make_shared<HeadlessIdleCallbacksTurboModule>(jsInvoker);
  }
  if (name == "NativePerformanceCxx") {
    return std::make_shared<HeadlessPerformanceTurboModule>(jsInvoker);
  }
  if (name == "NativeIntersectionObserverCxx") {
    return createHeadlessIntersectionObserver(jsInvoker);
  }
  if (name == "NativeMutationObserverCxx") {
    return createHeadlessMutationObserver(jsInvoker);
  }
  if (name == "ExceptionsManager") {
    return std::make_shared<ExceptionsTurboModule>(jsInvoker);
  }
  if (name == "LogBox") {
    return std::make_shared<LogBoxTurboModule>(jsInvoker);
  }
  if (name == "DevSettings") {
    return std::make_shared<DevSettingsTurboModule>(jsInvoker);
  }
  if (name == "HeadlessSampleModule") {
    return std::make_shared<HeadlessSampleTurboModule>(jsInvoker);
  }
  if (auto module = profile.getTurboModule(runtime, name, jsInvoker)) {
    return module;
  }
  if (name == "UIManager") {
    // Fabric owns layout and mounting. This compatibility module only lets
    // RN's static view-config path probe for legacy UIManager safely.
    return std::make_shared<EmptyTurboModule>(name, jsInvoker);
  }
  return addons.getTurboModule(runtime, name, jsInvoker);
}
