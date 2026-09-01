#pragma once

#include <functional>

struct HeadlessKeyboardController {
  std::function<void(bool show)> emit;
  float viewportWidth{390};
  float viewportHeight{844};
  float keyboardHeight{280};
  bool visible{false};

  void setVisible(bool show);
};

HeadlessKeyboardController& headlessKeyboard();
