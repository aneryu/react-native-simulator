#include "HeadlessKeyboard.h"

HeadlessKeyboardController& headlessKeyboard() {
  static HeadlessKeyboardController controller;
  return controller;
}

void HeadlessKeyboardController::setVisible(bool show) {
  if (visible == show) {
    return;
  }
  visible = show;
  if (emit) {
    emit(show);
  }
}
