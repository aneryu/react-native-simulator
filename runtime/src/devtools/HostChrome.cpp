#include "HostChrome.h"

#include <algorithm>

bool HostChromeController::toastVisible() const {
  return !toastMessage.empty() &&
      std::chrono::steady_clock::now() < toastUntil;
}

void HostChromeController::setStatusBarHidden(bool hidden) {
  if (statusBarHidden == hidden) {
    return;
  }
  statusBarHidden = hidden;
  invalidate();
}

void HostChromeController::setStatusBarStyle(const std::string& style) {
  const bool light = style != "dark-content";
  if (statusBarLightContent == light) {
    return;
  }
  statusBarLightContent = light;
  invalidate();
}

void HostChromeController::showToast(
    std::string message,
    int durationMs,
    int gravity,
    float xOffset,
    float yOffset) {
  toastMessage = std::move(message);
  toastGravity = gravity;
  toastOffsetX = xOffset;
  toastOffsetY = yOffset;
  toastUntil = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(std::max(durationMs, 1));
  invalidate();
}

void HostChromeController::invalidate() {
  if (onInvalidate) {
    onInvalidate();
  }
}

HostChromeController& hostChrome() {
  static HostChromeController controller;
  return controller;
}
