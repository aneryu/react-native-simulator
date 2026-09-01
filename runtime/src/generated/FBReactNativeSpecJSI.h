/**
 * Narrow codegen stand-in for the GitHub source checkout, which does not ship
 * FBReactNativeSpec. Only NativeAnimated types needed by RN 0.87 C++ Animated.
 */
#pragma once

#include <ReactCommon/TurboModule.h>
#include <react/bridging/Bridging.h>

#include <optional>

namespace facebook::react {

#pragma mark - NativeAnimatedTurboModuleEndResult

template <typename P0, typename P1, typename P2>
struct NativeAnimatedTurboModuleEndResult {
  P0 finished{};
  P1 value{};
  P2 offset;
  bool operator==(const NativeAnimatedTurboModuleEndResult& other) const {
    return finished == other.finished && value == other.value &&
        offset == other.offset;
  }
};

template <typename T>
struct NativeAnimatedTurboModuleEndResultBridging {
  static T types;

  static T fromJs(
      jsi::Runtime& rt,
      const jsi::Object& value,
      const std::shared_ptr<CallInvoker>& jsInvoker) {
    T result{
        bridging::fromJs<decltype(types.finished)>(
            rt, value.getProperty(rt, "finished"), jsInvoker),
        bridging::fromJs<decltype(types.value)>(
            rt, value.getProperty(rt, "value"), jsInvoker),
        bridging::fromJs<decltype(types.offset)>(
            rt, value.getProperty(rt, "offset"), jsInvoker)};
    return result;
  }

  static jsi::Object toJs(
      jsi::Runtime& rt,
      const T& value,
      const std::shared_ptr<CallInvoker>& jsInvoker) {
    auto result = facebook::jsi::Object(rt);
    result.setProperty(
        rt, "finished", bridging::toJs(rt, value.finished, jsInvoker));
    if (value.value) {
      result.setProperty(
          rt, "value", bridging::toJs(rt, value.value.value(), jsInvoker));
    }
    if (value.offset) {
      result.setProperty(
          rt, "offset", bridging::toJs(rt, value.offset.value(), jsInvoker));
    }
    return result;
  }
};

} // namespace facebook::react
