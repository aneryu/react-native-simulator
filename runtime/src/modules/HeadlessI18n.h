#pragma once

struct HeadlessI18nController {
  bool allowRTL{true};
  bool forceRTL{false};
  bool doLeftAndRightSwapInRTL{true};

  // Android I18nUtil: isRTL is true when forceRTL is set (allowRTL gates whether
  // the system RTL locale would apply; this host has no system RTL locale).
  bool isRTL() const { return forceRTL && allowRTL; }
};

inline HeadlessI18nController& headlessI18n() {
  static HeadlessI18nController controller;
  return controller;
}
