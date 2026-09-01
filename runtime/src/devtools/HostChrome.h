#pragma once

#include <chrono>
#include <functional>
#include <string>

struct HostChromeController {
  bool statusBarHidden{false};
  bool statusBarLightContent{true};
  float statusBarHeight{24};
  float statusBarRed{0};
  float statusBarGreen{0};
  float statusBarBlue{0};
  float statusBarAlpha{0};

  std::string toastMessage;
  int toastGravity{81};
  float toastOffsetX{0};
  float toastOffsetY{0};
  std::chrono::steady_clock::time_point toastUntil{};

  std::function<void()> onInvalidate;

  bool toastVisible() const;
  void setStatusBarHidden(bool hidden);
  void setStatusBarStyle(const std::string& style);
  void showToast(
      std::string message,
      int durationMs,
      int gravity,
      float xOffset,
      float yOffset);
  void invalidate();
};

HostChromeController& hostChrome();
