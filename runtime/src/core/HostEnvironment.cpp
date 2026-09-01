#include "HostEnvironment.h"

#include <algorithm>
#include <utility>
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

void callBoolCallback(
    jsi::Runtime& runtime,
    const jsi::Value* args,
    size_t count,
    bool value) {
  if (count > 0 && args[0].isObject() &&
      args[0].getObject(runtime).isFunction(runtime)) {
    args[0].getObject(runtime).getFunction(runtime).call(runtime, value);
  }
}

template <bool HostEnvironmentState::*Field>
jsi::Value boolCallback(
    jsi::Runtime& runtime,
    react::TurboModule&,
    const jsi::Value* args,
    size_t count) {
  callBoolCallback(
      runtime, args, count, hostEnvironment().state.*Field);
  return jsi::Value::undefined();
}

jsi::Value boolFalseCallback(
    jsi::Runtime& runtime,
    react::TurboModule&,
    const jsi::Value* args,
    size_t count) {
  callBoolCallback(runtime, args, count, false);
  return jsi::Value::undefined();
}

jsi::Object makeDisplayMetrics(
    jsi::Runtime& runtime,
    float width,
    float height,
    float scale) {
  jsi::Object metrics(runtime);
  metrics.setProperty(runtime, "width", width);
  metrics.setProperty(runtime, "height", height);
  metrics.setProperty(runtime, "scale", scale);
  metrics.setProperty(runtime, "fontScale", 1);
  return metrics;
}

jsi::Object makePhysicalDisplayMetrics(
    jsi::Runtime& runtime,
    float width,
    float height,
    float scale) {
  jsi::Object metrics(runtime);
  metrics.setProperty(runtime, "width", width * scale);
  metrics.setProperty(runtime, "height", height * scale);
  metrics.setProperty(runtime, "scale", scale);
  metrics.setProperty(runtime, "fontScale", 1);
  metrics.setProperty(runtime, "densityDpi", 160.0f * scale);
  return metrics;
}

const char* colorSchemeName() {
  return hostEnvironment().state.colorScheme == "dark" ? "dark" : "light";
}

class A11yEventModule : public react::TurboModule {
 public:
  using TurboModule::TurboModule;

  void emitA11yEvents() {
    const auto& state = hostEnvironment().state;
    emitBool("touchExplorationDidChange", state.screenReader);
    emitBool("reduceMotionDidChange", state.reduceMotion);
    emitBool("highTextContrastDidChange", state.highTextContrast);
    emitBool("accessibilityServiceDidChange", state.accessibilityService);
    emitBool("invertColorDidChange", state.invertColors);
    emitBool("grayscaleModeDidChange", state.grayscale);
    emitBool("boldTextChanged", state.boldText);
    emitBool("screenReaderChanged", state.screenReader);
    emitBool("grayscaleChanged", state.grayscale);
    emitBool("invertColorsChanged", state.invertColors);
    emitBool("reduceMotionChanged", state.reduceMotion);
    emitBool("reduceTransparencyChanged", state.reduceTransparency);
    emitBool("darkerSystemColorsChanged", state.darkerSystemColors);
  }

 private:
  void emitBool(const char* eventName, bool value) {
    emitDeviceEvent(
        eventName,
        [value](jsi::Runtime&, std::vector<jsi::Value>& args) {
          args.emplace_back(value);
        });
  }
};

class AppStateModule final : public react::TurboModule {
 public:
  explicit AppStateModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("AppState", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["getCurrentAppState"] = {2, &getCurrentAppState};
    methodMap_["addListener"] = {1, &noop};
    methodMap_["removeListeners"] = {1, &noop};
  }

  void emitAppStateEvents() {
    const auto appState = hostEnvironment().state.appState;
    const bool focused = appState == "active";
    emitDeviceEvent(
        "appStateDidChange",
        [appState](jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object object(runtime);
          object.setProperty(
              runtime,
              "app_state",
              jsi::String::createFromUtf8(runtime, appState));
          args.emplace_back(std::move(object));
        });
    emitDeviceEvent(
        "appStateFocusChange",
        [focused](jsi::Runtime&, std::vector<jsi::Value>& args) {
          args.emplace_back(focused);
        });
  }

  void emitOrientationEvent() {
    const auto& state = hostEnvironment().state;
    const auto name = state.orientation;
    const auto rotationDegrees = state.rotationDegrees;
    const auto isLandscape = state.isLandscape;
    emitDeviceEvent(
        "namedOrientationDidChange",
        [name, rotationDegrees, isLandscape](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object object(runtime);
          object.setProperty(
              runtime,
              "name",
              jsi::String::createFromUtf8(runtime, name));
          object.setProperty(runtime, "rotationDegrees", rotationDegrees);
          object.setProperty(runtime, "isLandscape", isLandscape);
          args.emplace_back(std::move(object));
        });
  }

  void emitDimensionsEvent() {
    const auto& state = hostEnvironment().state;
    const auto width = state.viewportWidth;
    const auto height = state.viewportHeight;
    const auto scale = state.pointScaleFactor;
    const auto screenHeight =
        height + std::max(0.0f, state.insetTop) +
        std::max(0.0f, state.insetBottom);
    emitDeviceEvent(
        "didUpdateDimensions",
        [width, height, screenHeight, scale](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object payload(runtime);
          payload.setProperty(
              runtime,
              "window",
              makeDisplayMetrics(runtime, width, height, scale));
          payload.setProperty(
              runtime,
              "screen",
              makeDisplayMetrics(runtime, width, screenHeight, scale));
          payload.setProperty(
              runtime,
              "windowPhysicalPixels",
              makePhysicalDisplayMetrics(runtime, width, height, scale));
          payload.setProperty(
              runtime,
              "screenPhysicalPixels",
              makePhysicalDisplayMetrics(runtime, width, screenHeight, scale));
          args.emplace_back(std::move(payload));
        });
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object constants(runtime);
    constants.setProperty(
        runtime,
        "initialAppState",
        jsi::String::createFromUtf8(
            runtime, hostEnvironment().state.appState));
    return constants;
  }

  static jsi::Value getCurrentAppState(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isObject() &&
        args[0].getObject(runtime).isFunction(runtime)) {
      jsi::Object state(runtime);
      state.setProperty(
          runtime,
          "app_state",
          jsi::String::createFromUtf8(
              runtime, hostEnvironment().state.appState));
      args[0].getObject(runtime).getFunction(runtime).call(
          runtime, std::move(state));
    }
    return jsi::Value::undefined();
  }
};

class AppearanceModule final : public react::TurboModule {
 public:
  explicit AppearanceModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("Appearance", std::move(jsInvoker)) {
    methodMap_["getColorScheme"] = {0, &getColorScheme};
    methodMap_["setColorScheme"] = {1, &setColorScheme};
    methodMap_["addListener"] = {1, &noop};
    methodMap_["removeListeners"] = {1, &noop};
  }

  void emitAppearanceEvent() {
    const auto* scheme = colorSchemeName();
    emitDeviceEvent(
        "appearanceChanged",
        [scheme](jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object object(runtime);
          object.setProperty(
              runtime,
              "colorScheme",
              jsi::String::createFromAscii(runtime, scheme));
          args.emplace_back(std::move(object));
        });
  }

 private:
  static jsi::Value getColorScheme(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::String::createFromAscii(runtime, colorSchemeName());
  }

  static jsi::Value setColorScheme(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isString()) {
      const auto scheme = args[0].getString(runtime).utf8(runtime);
      if (scheme == "dark" || scheme == "light") {
        hostEnvironment().setColorScheme(scheme);
      }
    }
    return jsi::Value::undefined();
  }
};

class AccessibilityInfoModule final : public A11yEventModule {
 public:
  explicit AccessibilityInfoModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : A11yEventModule("AccessibilityInfo", std::move(jsInvoker)) {
    methodMap_["isReduceMotionEnabled"] = {
        1, &boolCallback<&HostEnvironmentState::reduceMotion>};
    methodMap_["isInvertColorsEnabled"] = {
        1, &boolCallback<&HostEnvironmentState::invertColors>};
    methodMap_["isHighTextContrastEnabled"] = {
        1, &boolCallback<&HostEnvironmentState::highTextContrast>};
    methodMap_["isTouchExplorationEnabled"] = {
        1, &boolCallback<&HostEnvironmentState::screenReader>};
    methodMap_["isAccessibilityServiceEnabled"] = {
        1, &boolCallback<&HostEnvironmentState::accessibilityService>};
    methodMap_["isGrayscaleEnabled"] = {
        1, &boolCallback<&HostEnvironmentState::grayscale>};
    methodMap_["setAccessibilityFocus"] = {1, &noop};
    methodMap_["announceForAccessibility"] = {1, &noop};
    methodMap_["getRecommendedTimeoutMillis"] = {
        2, &getRecommendedTimeoutMillis};
    methodMap_["addListener"] = {1, &noop};
    methodMap_["removeListeners"] = {1, &noop};
  }

 private:
  static jsi::Value getRecommendedTimeoutMillis(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    const auto millis = count > 0 && args[0].isNumber() ? args[0].getNumber()
                                                        : 0.0;
    if (count > 1 && args[1].isObject() &&
        args[1].getObject(runtime).isFunction(runtime)) {
      args[1].getObject(runtime).getFunction(runtime).call(runtime, millis);
    }
    return jsi::Value::undefined();
  }
};

class AccessibilityManagerModule final : public A11yEventModule {
 public:
  explicit AccessibilityManagerModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : A11yEventModule("AccessibilityManager", std::move(jsInvoker)) {
    methodMap_["getCurrentBoldTextState"] = {
        2, &boolCallback<&HostEnvironmentState::boldText>};
    methodMap_["getCurrentGrayscaleState"] = {
        2, &boolCallback<&HostEnvironmentState::grayscale>};
    methodMap_["getCurrentInvertColorsState"] = {
        2, &boolCallback<&HostEnvironmentState::invertColors>};
    methodMap_["getCurrentReduceMotionState"] = {
        2, &boolCallback<&HostEnvironmentState::reduceMotion>};
    methodMap_["getCurrentDarkerSystemColorsState"] = {
        2, &boolCallback<&HostEnvironmentState::darkerSystemColors>};
    methodMap_["getCurrentPrefersCrossFadeTransitionsState"] = {
        2, &boolFalseCallback};
    methodMap_["getCurrentReduceTransparencyState"] = {
        2, &boolCallback<&HostEnvironmentState::reduceTransparency>};
    methodMap_["getCurrentVoiceOverState"] = {
        2, &boolCallback<&HostEnvironmentState::screenReader>};
    methodMap_["setAccessibilityContentSizeMultipliers"] = {1, &noop};
    methodMap_["setAccessibilityFocus"] = {1, &noop};
    methodMap_["announceForAccessibility"] = {1, &noop};
    methodMap_["announceForAccessibilityWithOptions"] = {2, &noop};
    methodMap_["addListener"] = {1, &noop};
    methodMap_["removeListeners"] = {1, &noop};
  }
};

void setA11yFlag(bool HostEnvironmentState::*field, bool value) {
  auto& env = hostEnvironment();
  if (env.state.*field == value) {
    return;
  }
  env.state.*field = value;
  if (env.emitA11y) {
    env.emitA11y();
  }
}
} // namespace

HostEnvironmentController& hostEnvironment() {
  static HostEnvironmentController controller;
  return controller;
}

void HostEnvironmentController::setColorScheme(std::string scheme) {
  if (state.colorScheme == scheme) {
    return;
  }
  state.colorScheme = std::move(scheme);
  if (emitAppearance) {
    emitAppearance();
  }
}

void HostEnvironmentController::setAppState(std::string appState) {
  if (state.appState == appState) {
    return;
  }
  state.appState = std::move(appState);
  if (emitAppState) {
    emitAppState();
  }
}

void HostEnvironmentController::setReduceMotion(bool value) {
  setA11yFlag(&HostEnvironmentState::reduceMotion, value);
}

void HostEnvironmentController::setInvertColors(bool value) {
  setA11yFlag(&HostEnvironmentState::invertColors, value);
}

void HostEnvironmentController::setHighTextContrast(bool value) {
  setA11yFlag(&HostEnvironmentState::highTextContrast, value);
}

void HostEnvironmentController::setScreenReader(bool value) {
  setA11yFlag(&HostEnvironmentState::screenReader, value);
}

void HostEnvironmentController::setAccessibilityService(bool value) {
  setA11yFlag(&HostEnvironmentState::accessibilityService, value);
}

void HostEnvironmentController::setGrayscale(bool value) {
  setA11yFlag(&HostEnvironmentState::grayscale, value);
}

void HostEnvironmentController::setBoldText(bool value) {
  setA11yFlag(&HostEnvironmentState::boldText, value);
}

void HostEnvironmentController::setReduceTransparency(bool value) {
  setA11yFlag(&HostEnvironmentState::reduceTransparency, value);
}

void HostEnvironmentController::setDarkerSystemColors(bool value) {
  setA11yFlag(&HostEnvironmentState::darkerSystemColors, value);
}

void HostEnvironmentController::setOrientation(std::string name) {
  if (state.orientation == name) {
    return;
  }
  bool landscape = false;
  double degrees = 0;
  if (name == "portrait-primary") {
    degrees = 0;
    landscape = false;
  } else if (name == "landscape-primary") {
    degrees = -90;
    landscape = true;
  } else if (name == "portrait-secondary") {
    degrees = 180;
    landscape = false;
  } else if (name == "landscape-secondary") {
    degrees = 90;
    landscape = true;
  } else {
    return;
  }
  state.orientation = std::move(name);
  state.rotationDegrees = degrees;
  state.isLandscape = landscape;
  if (landscape && state.viewportWidth < state.viewportHeight) {
    std::swap(state.viewportWidth, state.viewportHeight);
  } else if (!landscape && state.viewportWidth > state.viewportHeight) {
    std::swap(state.viewportWidth, state.viewportHeight);
  }
  if (emitOrientation) {
    emitOrientation();
  }
  if (emitDimensions) {
    emitDimensions();
  }
}

void HostEnvironmentController::setViewport(
    float width,
    float height,
    float scale,
    float insetTop,
    float insetBottom) {
  if (state.viewportWidth == width && state.viewportHeight == height &&
      state.pointScaleFactor == scale && state.insetTop == insetTop &&
      state.insetBottom == insetBottom) {
    return;
  }
  state.viewportWidth = width;
  state.viewportHeight = height;
  state.pointScaleFactor = scale;
  state.insetTop = insetTop;
  state.insetBottom = insetBottom;
  if (emitDimensions) {
    emitDimensions();
  }
}

void HostEnvironmentController::reset() {
  state = HostEnvironmentState{};
  emitAppearance = nullptr;
  emitAppState = nullptr;
  emitA11y = nullptr;
  emitOrientation = nullptr;
  emitDimensions = nullptr;
}

std::shared_ptr<facebook::react::TurboModule> createHeadlessAppStateModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
  auto module = std::make_shared<AppStateModule>(std::move(jsInvoker));
  auto& env = hostEnvironment();
  env.emitAppState = [weak = std::weak_ptr<AppStateModule>(module)]() {
    if (auto locked = weak.lock()) {
      locked->emitAppStateEvents();
    }
  };
  env.emitOrientation = [weak = std::weak_ptr<AppStateModule>(module)]() {
    if (auto locked = weak.lock()) {
      locked->emitOrientationEvent();
    }
  };
  env.emitDimensions = [weak = std::weak_ptr<AppStateModule>(module)]() {
    if (auto locked = weak.lock()) {
      locked->emitDimensionsEvent();
    }
  };
  return module;
}

std::shared_ptr<facebook::react::TurboModule> createHeadlessAppearanceModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
  auto module = std::make_shared<AppearanceModule>(std::move(jsInvoker));
  hostEnvironment().emitAppearance =
      [weak = std::weak_ptr<AppearanceModule>(module)]() {
        if (auto locked = weak.lock()) {
          locked->emitAppearanceEvent();
        }
      };
  return module;
}

std::shared_ptr<facebook::react::TurboModule>
createHeadlessAccessibilityInfoModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
  auto module =
      std::make_shared<AccessibilityInfoModule>(std::move(jsInvoker));
  hostEnvironment().emitA11y =
      [weak = std::weak_ptr<AccessibilityInfoModule>(module)]() {
        if (auto locked = weak.lock()) {
          locked->emitA11yEvents();
        }
      };
  return module;
}

std::shared_ptr<facebook::react::TurboModule>
createHeadlessAccessibilityManagerModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
  auto module =
      std::make_shared<AccessibilityManagerModule>(std::move(jsInvoker));
  hostEnvironment().emitA11y =
      [weak = std::weak_ptr<AccessibilityManagerModule>(module)]() {
        if (auto locked = weak.lock()) {
          locked->emitA11yEvents();
        }
      };
  return module;
}
