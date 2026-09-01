/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <react/debug/react_native_expect.h>
#include <react/renderer/core/RawValue.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/fromRawValueShared.h>
#include <react/utils/ContextContainer.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace facebook::react {
namespace {

// Pixel 4a / AppCompat DayNight (light) samples for android-rn87 RN Tester.
// Glance-level approximations only — not a Resources lookup.
struct AndroidThemeSwatch {
  const char* needle;
  float red;
  float green;
  float blue;
  float alpha;
};

// Longer / more specific needles first so substring matching does not collapse
// colorPrimaryDark into colorPrimary, textColorPrimary into colorPrimary, or
// colorControlHighlight into a generic control token.
constexpr AndroidThemeSwatch kAndroidThemeColors[] = {
    {"colorPrimaryDark", 0.165f, 0.38f, 0.345f, 1.0f},
    {"textColorPrimary", 0.0f, 0.0f, 0.0f, 0.87f},
    {"colorControlHighlight", 0.0f, 0.0f, 0.0f, 0.12f},
    {"colorControlNormal", 0.0f, 0.0f, 0.0f, 0.54f},
    {"colorControlActivated", 0.22745098f, 0.5137255f, 0.46666667f, 1.0f},
    {"colorBackgroundFloating", 0.98f, 0.98f, 0.98f, 1.0f},
    {"colorButtonNormal", 0.98f, 0.98f, 0.98f, 1.0f},
    {"colorError", 0.702f, 0.149f, 0.118f, 1.0f},
    {"color_error", 0.702f, 0.149f, 0.118f, 1.0f},
    {"colorAccent", 0.22745098f, 0.5137255f, 0.46666667f, 1.0f},
    {"colorPrimary", 0.22745098f, 0.5137255f, 0.46666667f, 1.0f},
    {"colorOnSurface", 0.0f, 0.0f, 0.0f, 0.87f},
    {"holo_green_light", 0.6f, 0.8f, 0.0f, 1.0f},
    {"holo_purple", 0.667f, 0.4f, 0.8f, 1.0f},
    {"catalyst_redbox", 0.8f, 0.1f, 0.1f, 1.0f},
    {"catalyst_logbox", 0.98f, 0.75f, 0.14f, 1.0f},
};

std::optional<SharedColor> androidThemeColor(const std::string& token) {
  for (const auto& entry : kAndroidThemeColors) {
    if (token.find(entry.needle) != std::string::npos) {
      return colorFromComponents(
          {entry.red, entry.green, entry.blue, entry.alpha});
    }
  }
  return std::nullopt;
}

SharedColor androidThemeColorOrBlack(const std::string& token) {
  if (auto color = androidThemeColor(token)) {
    return *color;
  }
  return {colorFromComponents({0.0f, 0.0f, 0.0f, 1.0f})};
}

} // namespace

inline SharedColor parsePlatformColor(
    const ContextContainer& /*contextContainer*/,
    int32_t /*surfaceId*/,
    const RawValue& value) {
  if (value.hasType<std::unordered_map<std::string, RawValue>>()) {
    const auto& items =
        (std::unordered_map<std::string, RawValue>)value;
    const auto paths = items.find("resource_paths");
    if (paths != items.end() &&
        paths->second.hasType<std::vector<std::string>>()) {
      const auto tokens = (std::vector<std::string>)paths->second;
      for (const auto& token : tokens) {
        if (auto color = androidThemeColor(token)) {
          return *color;
        }
      }
      if (!tokens.empty()) {
        return androidThemeColorOrBlack(tokens.front());
      }
    }
  }
  return {colorFromComponents({0, 0, 0, 0})};
}

inline void fromRawValue(
    const ContextContainer& contextContainer,
    int32_t surfaceId,
    const RawValue& value,
    SharedColor& result) {
  fromRawValueShared(
      contextContainer, surfaceId, value, result, parsePlatformColor);
}

} // namespace facebook::react
