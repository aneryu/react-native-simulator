#include "HeadlessIosModules.h"
#include "HostChrome.h"
#include "HostUi.h"

#include <react/nativemodule/core/ReactCommon/TurboModuleUtils.h>

#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jsi = facebook::jsi;
namespace react = facebook::react;

namespace {
std::string hostUiString(
    jsi::Runtime& runtime,
    const jsi::Object& object,
    const char* name) {
  const auto value = object.getProperty(runtime, name);
  if (!value.isString()) {
    return {};
  }
  return value.getString(runtime).utf8(runtime);
}

int hostUiInt(
    jsi::Runtime& runtime,
    const jsi::Object& object,
    const char* name,
    int fallback) {
  const auto value = object.getProperty(runtime, name);
  if (value.isNumber()) {
    return static_cast<int>(value.getNumber());
  }
  if (value.isString()) {
    const auto text = value.getString(runtime).utf8(runtime);
    if (text.empty()) {
      return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end != text.c_str()) {
      return static_cast<int>(parsed);
    }
  }
  return fallback;
}

std::shared_ptr<jsi::Function> hostUiFunction(
    jsi::Runtime& runtime,
    const jsi::Value* args,
    size_t count,
    size_t index) {
  if (count > index && args[index].isObject() &&
      args[index].getObject(runtime).isFunction(runtime)) {
    return std::make_shared<jsi::Function>(
        args[index].getObject(runtime).getFunction(runtime));
  }
  return nullptr;
}

std::string hostUiButtonText(
    jsi::Runtime& runtime,
    const jsi::Object& button) {
  const auto text = button.getProperty(runtime, "text");
  if (text.isString()) {
    auto label = text.getString(runtime).utf8(runtime);
    return label.empty() ? std::string("OK") : std::move(label);
  }
  auto names = button.getPropertyNames(runtime);
  const auto length = names.size(runtime);
  for (size_t index = 0; index < length; ++index) {
    auto name = names.getValueAtIndex(runtime, index);
    if (!name.isString()) {
      continue;
    }
    auto value = button.getProperty(runtime, name.getString(runtime));
    if (value.isString()) {
      auto label = value.getString(runtime).utf8(runtime);
      return label.empty() ? std::string("OK") : std::move(label);
    }
  }
  return "OK";
}

void collectStringArray(
    jsi::Runtime& runtime,
    const jsi::Object& object,
    const char* name,
    std::vector<std::string>& out) {
  const auto value = object.getProperty(runtime, name);
  if (!value.isObject() || !value.getObject(runtime).isArray(runtime)) {
    return;
  }
  auto array = value.getObject(runtime).getArray(runtime);
  const auto length = array.size(runtime);
  out.reserve(out.size() + length);
  for (size_t index = 0; index < length; ++index) {
    auto item = array.getValueAtIndex(runtime, index);
    if (item.isString()) {
      out.push_back(item.getString(runtime).utf8(runtime));
    } else {
      out.emplace_back();
    }
  }
}

jsi::Object makeNotificationPermissions(jsi::Runtime& runtime) {
  jsi::Object permissions(runtime);
  permissions.setProperty(runtime, "alert", true);
  permissions.setProperty(runtime, "badge", true);
  permissions.setProperty(runtime, "sound", true);
  return permissions;
}

class AlertManagerModule final : public react::TurboModule {
 public:
  explicit AlertManagerModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("AlertManager", std::move(jsInvoker)) {
    methodMap_["alertWithArgs"] = {2, &alertWithArgs};
  }

 private:
  static AlertManagerModule& self(react::TurboModule& module) {
    return static_cast<AlertManagerModule&>(module);
  }

  static jsi::Value alertWithArgs(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    HostUiAlert spec;
    spec.buttonPositive.clear();
    int cancelIndex = 0;
    bool usedItems = false;
    if (count > 0 && args[0].isObject()) {
      auto config = args[0].getObject(runtime);
      spec.title = hostUiString(runtime, config, "title");
      spec.message = hostUiString(runtime, config, "message");
      cancelIndex = hostUiInt(runtime, config, "cancelButtonKey", 0);
      const auto buttons = config.getProperty(runtime, "buttons");
      if (buttons.isObject() && buttons.getObject(runtime).isArray(runtime)) {
        auto array = buttons.getObject(runtime).getArray(runtime);
        const auto length = array.size(runtime);
        spec.items.reserve(length);
        for (size_t index = 0; index < length; ++index) {
          auto item = array.getValueAtIndex(runtime, index);
          if (item.isObject()) {
            spec.items.push_back(
                hostUiButtonText(runtime, item.getObject(runtime)));
          } else {
            spec.items.emplace_back("OK");
          }
        }
      }
    }
    if (spec.items.empty()) {
      spec.buttonPositive = "OK";
    } else {
      usedItems = true;
    }
    auto onAction = hostUiFunction(runtime, args, count, 1);
    auto jsInvoker = self(module).jsInvoker_;
    hostUi().presentAlert(
        std::move(spec),
        [jsInvoker, onAction, usedItems, cancelIndex](
            HostUiAlertResult result) {
          if (!onAction) {
            return;
          }
          int id = 0;
          if (result.dismissed) {
            id = cancelIndex;
          } else if (usedItems) {
            id = result.buttonKey;
          } else if (result.buttonKey == -2) {
            id = 1;
          } else if (result.buttonKey == -3) {
            id = 2;
          }
          jsInvoker->invokeAsync(
              [onAction, id](jsi::Runtime& runtime) {
                onAction->call(
                    runtime,
                    id,
                    jsi::String::createFromAscii(runtime, ""));
              });
        });
    return jsi::Value::undefined();
  }
};

class ActionSheetManagerModule final : public react::TurboModule {
 public:
  explicit ActionSheetManagerModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ActionSheetManager", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["showActionSheetWithOptions"] = {2, &showActionSheetWithOptions};
    methodMap_["showShareActionSheetWithOptions"] = {
        3, &showShareActionSheetWithOptions};
    methodMap_["dismissActionSheet"] = {0, &dismissActionSheet};
  }

 private:
  static ActionSheetManagerModule& self(react::TurboModule& module) {
    return static_cast<ActionSheetManagerModule&>(module);
  }

  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Object(runtime);
  }

  static jsi::Value showActionSheetWithOptions(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    HostUiAlert spec;
    spec.buttonPositive.clear();
    int cancelButtonIndex = 0;
    if (count > 0 && args[0].isObject()) {
      auto options = args[0].getObject(runtime);
      spec.title = hostUiString(runtime, options, "title");
      spec.message = hostUiString(runtime, options, "message");
      collectStringArray(runtime, options, "options", spec.items);
      cancelButtonIndex = hostUiInt(runtime, options, "cancelButtonIndex", 0);
    }
    auto onAction = hostUiFunction(runtime, args, count, 1);
    auto jsInvoker = self(module).jsInvoker_;
    hostUi().presentAlert(
        std::move(spec),
        [jsInvoker, onAction, cancelButtonIndex](HostUiAlertResult result) {
          if (!onAction) {
            return;
          }
          const int index =
              result.dismissed ? cancelButtonIndex : result.buttonKey;
          jsInvoker->invokeAsync(
              [onAction, index](jsi::Runtime& runtime) {
                onAction->call(runtime, index);
              });
        });
    return jsi::Value::undefined();
  }

  static jsi::Value showShareActionSheetWithOptions(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    HostUiShare spec;
    if (count > 0 && args[0].isObject()) {
      auto options = args[0].getObject(runtime);
      spec.title = hostUiString(runtime, options, "subject");
      spec.message = hostUiString(runtime, options, "message");
      if (spec.message.empty()) {
        spec.message = hostUiString(runtime, options, "url");
      }
    }
    auto onSuccess = hostUiFunction(runtime, args, count, 2);
    auto jsInvoker = self(module).jsInvoker_;
    hostUi().presentShare(
        spec,
        [jsInvoker, onSuccess](HostUiShareResult result) {
          if (!onSuccess) {
            return;
          }
          jsInvoker->invokeAsync(
              [onSuccess, shared = result.shared](jsi::Runtime& runtime) {
                onSuccess->call(runtime, shared, jsi::Value::null());
              });
        });
    return jsi::Value::undefined();
  }

  static jsi::Value dismissActionSheet(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    const auto pending = hostUi().peek();
    if (!pending) {
      return jsi::Value::undefined();
    }
    if (pending->kind == HostUiKind::Alert) {
      hostUi().completeAlert({.dismissed = true, .buttonKey = -1});
    } else if (pending->kind == HostUiKind::Share) {
      hostUi().completeShare({.shared = false});
    }
    return jsi::Value::undefined();
  }
};

struct SettingsValue {
  enum class Type { Null, Boolean, Number, String };
  Type type{Type::Null};
  bool boolean{false};
  double number{0};
  std::string string;
};

struct SettingsState {
  std::mutex mutex;
  std::unordered_map<std::string, SettingsValue> values;
};

SettingsState& settingsState() {
  static SettingsState state;
  return state;
}

jsi::Value settingsValueToJs(
    jsi::Runtime& runtime,
    const SettingsValue& value) {
  switch (value.type) {
    case SettingsValue::Type::Boolean:
      return jsi::Value(value.boolean);
    case SettingsValue::Type::Number:
      return jsi::Value(value.number);
    case SettingsValue::Type::String:
      return jsi::String::createFromUtf8(runtime, value.string);
    case SettingsValue::Type::Null:
      break;
  }
  return jsi::Value::null();
}

class SettingsManagerModule final : public react::TurboModule {
 public:
  explicit SettingsManagerModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("SettingsManager", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["setValues"] = {1, &setValues};
    methodMap_["deleteValues"] = {1, &deleteValues};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    std::unordered_map<std::string, SettingsValue> snapshot;
    {
      auto& state = settingsState();
      std::lock_guard lock(state.mutex);
      snapshot = state.values;
    }
    jsi::Object settings(runtime);
    for (const auto& [key, value] : snapshot) {
      settings.setProperty(
          runtime, key.c_str(), settingsValueToJs(runtime, value));
    }
    jsi::Object constants(runtime);
    constants.setProperty(runtime, "settings", std::move(settings));
    return constants;
  }

  static jsi::Value setValues(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count == 0 || !args[0].isObject()) {
      return jsi::Value::undefined();
    }
    auto values = args[0].getObject(runtime);
    auto names = values.getPropertyNames(runtime);
    const auto length = names.size(runtime);
    auto& state = settingsState();
    std::lock_guard lock(state.mutex);
    for (size_t index = 0; index < length; ++index) {
      auto name = names.getValueAtIndex(runtime, index);
      if (!name.isString()) {
        continue;
      }
      auto key = name.getString(runtime).utf8(runtime);
      auto value = values.getProperty(runtime, name.getString(runtime));
      SettingsValue stored;
      if (value.isNull()) {
        stored.type = SettingsValue::Type::Null;
      } else if (value.isBool()) {
        stored.type = SettingsValue::Type::Boolean;
        stored.boolean = value.getBool();
      } else if (value.isNumber()) {
        stored.type = SettingsValue::Type::Number;
        stored.number = value.getNumber();
      } else if (value.isString()) {
        stored.type = SettingsValue::Type::String;
        stored.string = value.getString(runtime).utf8(runtime);
      } else {
        continue;
      }
      state.values[std::move(key)] = std::move(stored);
    }
    return jsi::Value::undefined();
  }

  static jsi::Value deleteValues(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count == 0 || !args[0].isObject() ||
        !args[0].getObject(runtime).isArray(runtime)) {
      return jsi::Value::undefined();
    }
    auto keys = args[0].getObject(runtime).getArray(runtime);
    const auto length = keys.size(runtime);
    auto& state = settingsState();
    std::lock_guard lock(state.mutex);
    for (size_t index = 0; index < length; ++index) {
      auto item = keys.getValueAtIndex(runtime, index);
      if (item.isString()) {
        state.values.erase(item.getString(runtime).utf8(runtime));
      }
    }
    return jsi::Value::undefined();
  }
};

class StatusBarManagerIOS final : public react::TurboModule {
 public:
  explicit StatusBarManagerIOS(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("StatusBarManager", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["getHeight"] = {1, &getHeight};
    methodMap_["setStyle"] = {2, &setStyle};
    methodMap_["setHidden"] = {2, &setHidden};
    addNoop("addListener", 1);
    addNoop("removeListeners", 1);
    addNoop("setNetworkActivityIndicatorVisible", 1);
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

  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object constants(runtime);
    constants.setProperty(runtime, "HEIGHT", hostChrome().statusBarHeight);
    constants.setProperty(runtime, "DEFAULT_BACKGROUND_COLOR", 0);
    return constants;
  }

  static jsi::Value getHeight(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isObject() &&
        args[0].getObject(runtime).isFunction(runtime)) {
      jsi::Object result(runtime);
      result.setProperty(
          runtime, "height", hostChrome().statusBarHeight);
      args[0].getObject(runtime).getFunction(runtime).call(
          runtime, std::move(result));
    }
    return jsi::Value::undefined();
  }

  static jsi::Value setStyle(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    std::string style = "default";
    if (count > 0 && args[0].isString()) {
      style = args[0].getString(runtime).utf8(runtime);
    }
    hostChrome().setStatusBarStyle(style);
    return jsi::Value::undefined();
  }

  static jsi::Value setHidden(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isBool()) {
      hostChrome().setStatusBarHidden(args[0].getBool());
    }
    return jsi::Value::undefined();
  }
};

struct PushNotificationState {
  std::mutex mutex;
  int badge{0};
  std::vector<std::string> scheduled;
};

PushNotificationState& pushNotificationState() {
  static PushNotificationState state;
  return state;
}

void storeLocalNotification(std::string alertBody) {
  auto& state = pushNotificationState();
  std::lock_guard lock(state.mutex);
  if (state.scheduled.size() >= 32) {
    state.scheduled.erase(state.scheduled.begin());
  }
  state.scheduled.push_back(std::move(alertBody));
}

void clearLocalNotifications() {
  auto& state = pushNotificationState();
  std::lock_guard lock(state.mutex);
  state.scheduled.clear();
}

class PushNotificationManagerModule final : public react::TurboModule {
 public:
  explicit PushNotificationManagerModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("PushNotificationManager", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["requestPermissions"] = {1, &requestPermissions};
    methodMap_["checkPermissions"] = {1, &checkPermissions};
    methodMap_["setApplicationIconBadgeNumber"] = {
        1, &setApplicationIconBadgeNumber};
    methodMap_["getApplicationIconBadgeNumber"] = {
        1, &getApplicationIconBadgeNumber};
    methodMap_["presentLocalNotification"] = {1, &storeNotification};
    methodMap_["scheduleLocalNotification"] = {1, &storeNotification};
    methodMap_["cancelAllLocalNotifications"] = {0, &clearNotifications};
    methodMap_["cancelLocalNotifications"] = {1, &clearNotifications};
    methodMap_["getInitialNotification"] = {0, &getInitialNotification};
    methodMap_["getScheduledLocalNotifications"] = {
        1, &getScheduledLocalNotifications};
    methodMap_["removeAllDeliveredNotifications"] = {0, &noop};
    methodMap_["removeDeliveredNotifications"] = {1, &noop};
    methodMap_["getDeliveredNotifications"] = {1, &getDeliveredNotifications};
    methodMap_["getAuthorizationStatus"] = {1, &getAuthorizationStatus};
    methodMap_["onFinishRemoteNotification"] = {2, &noop};
    methodMap_["abandonPermissions"] = {0, &noop};
    addNoop("addListener", 1);
    addNoop("removeListeners", 1);
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

  static jsi::Value noop(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Value::undefined();
  }

  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Object(runtime);
  }

  static jsi::Value requestPermissions(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return react::createPromiseAsJSIValue(
        runtime,
        [](jsi::Runtime& runtime, std::shared_ptr<react::Promise> promise) {
          promise->resolve(makeNotificationPermissions(runtime));
        });
  }

  static jsi::Value checkPermissions(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isObject() &&
        args[0].getObject(runtime).isFunction(runtime)) {
      args[0].getObject(runtime).getFunction(runtime).call(
          runtime, makeNotificationPermissions(runtime));
    }
    return jsi::Value::undefined();
  }

  static jsi::Value setApplicationIconBadgeNumber(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isNumber()) {
      auto& state = pushNotificationState();
      std::lock_guard lock(state.mutex);
      state.badge = static_cast<int>(args[0].getNumber());
    }
    return jsi::Value::undefined();
  }

  static jsi::Value getApplicationIconBadgeNumber(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    int badge = 0;
    {
      auto& state = pushNotificationState();
      std::lock_guard lock(state.mutex);
      badge = state.badge;
    }
    if (count > 0 && args[0].isObject() &&
        args[0].getObject(runtime).isFunction(runtime)) {
      args[0].getObject(runtime).getFunction(runtime).call(runtime, badge);
    }
    return jsi::Value::undefined();
  }

  static jsi::Value storeNotification(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    std::string alertBody;
    if (count > 0 && args[0].isObject()) {
      alertBody = hostUiString(runtime, args[0].getObject(runtime), "alertBody");
    }
    storeLocalNotification(std::move(alertBody));
    return jsi::Value::undefined();
  }

  static jsi::Value clearNotifications(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    clearLocalNotifications();
    return jsi::Value::undefined();
  }

  static jsi::Value getInitialNotification(
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

  static jsi::Value getScheduledLocalNotifications(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    std::vector<std::string> snapshot;
    {
      auto& state = pushNotificationState();
      std::lock_guard lock(state.mutex);
      snapshot = state.scheduled;
    }
    if (count > 0 && args[0].isObject() &&
        args[0].getObject(runtime).isFunction(runtime)) {
      jsi::Array notifications(runtime, snapshot.size());
      for (size_t index = 0; index < snapshot.size(); ++index) {
        jsi::Object notification(runtime);
        notification.setProperty(
            runtime,
            "alertBody",
            jsi::String::createFromUtf8(runtime, snapshot[index]));
        notifications.setValueAtIndex(
            runtime, index, std::move(notification));
      }
      args[0].getObject(runtime).getFunction(runtime).call(
          runtime, std::move(notifications));
    }
    return jsi::Value::undefined();
  }

  static jsi::Value getDeliveredNotifications(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isObject() &&
        args[0].getObject(runtime).isFunction(runtime)) {
      args[0].getObject(runtime).getFunction(runtime).call(
          runtime, jsi::Array(runtime, 0));
    }
    return jsi::Value::undefined();
  }

  static jsi::Value getAuthorizationStatus(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isObject() &&
        args[0].getObject(runtime).isFunction(runtime)) {
      args[0].getObject(runtime).getFunction(runtime).call(runtime, 2);
    }
    return jsi::Value::undefined();
  }
};
} // namespace

std::shared_ptr<react::TurboModule> createHeadlessAlertManagerModule(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<AlertManagerModule>(std::move(jsInvoker));
}

std::shared_ptr<react::TurboModule> createHeadlessActionSheetManagerModule(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<ActionSheetManagerModule>(std::move(jsInvoker));
}

std::shared_ptr<react::TurboModule> createHeadlessSettingsManagerModule(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<SettingsManagerModule>(std::move(jsInvoker));
}

std::shared_ptr<react::TurboModule> createHeadlessStatusBarManagerIOS(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<StatusBarManagerIOS>(std::move(jsInvoker));
}

std::shared_ptr<react::TurboModule> createHeadlessPushNotificationManagerModule(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<PushNotificationManagerModule>(std::move(jsInvoker));
}
