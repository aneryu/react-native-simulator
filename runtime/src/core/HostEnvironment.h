#pragma once
#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <functional>
#include <memory>
#include <string>

struct HostEnvironmentState {
  std::string colorScheme{"light"}; // "light" | "dark"
  std::string appState{"active"};   // "active" | "background" | "inactive"
  bool reduceMotion{false};
  bool invertColors{false};
  bool highTextContrast{false};
  bool screenReader{false};
  bool accessibilityService{false};
  bool grayscale{false};
  bool boldText{false};
  bool reduceTransparency{false};
  bool darkerSystemColors{false};
  std::string orientation{"portrait-primary"};
  double rotationDegrees{0};
  bool isLandscape{false};
  float viewportWidth{390};
  float viewportHeight{844};
  float pointScaleFactor{3};
  float insetTop{24};
  float insetBottom{0};
};

struct HostEnvironmentController {
  HostEnvironmentState state;

  // Modules assign these when constructed (like HeadlessKeyboardController::emit).
  std::function<void()> emitAppearance;
  std::function<void()> emitAppState;
  std::function<void()> emitA11y;
  std::function<void()> emitOrientation;
  std::function<void()> emitDimensions;

  void setColorScheme(std::string scheme); // emit appearanceChanged if changed
  void setAppState(std::string appState);  // emit appStateDidChange if changed; also appStateFocusChange bool
  void setReduceMotion(bool);
  void setInvertColors(bool);
  void setHighTextContrast(bool);
  void setScreenReader(bool);
  void setAccessibilityService(bool);
  void setGrayscale(bool);
  void setBoldText(bool);
  void setReduceTransparency(bool);
  void setDarkerSystemColors(bool);
  // portrait-primary / portrait-secondary / landscape-primary / landscape-secondary
  void setOrientation(std::string name);
  void setViewport(
      float width,
      float height,
      float scale,
      float insetTop = 24,
      float insetBottom = 0);
  void reset(); // defaults + clear emit fns
};

HostEnvironmentController& hostEnvironment();

std::shared_ptr<facebook::react::TurboModule> createHeadlessAppStateModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
std::shared_ptr<facebook::react::TurboModule> createHeadlessAppearanceModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
std::shared_ptr<facebook::react::TurboModule> createHeadlessAccessibilityInfoModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
std::shared_ptr<facebook::react::TurboModule> createHeadlessAccessibilityManagerModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
