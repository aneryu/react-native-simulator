/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <optional>
#include <string>

#include <react/renderer/components/text/BaseParagraphProps.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/renderer/core/RawValue.h>
#include <react/renderer/core/propsConversions.h>

namespace facebook::react {

enum class DataDetectorType {
  All,
  Email,
  Link,
  None,
  PhoneNumber,
};

inline void fromRawValue(
    const PropsParserContext& /*context*/,
    const RawValue& value,
    DataDetectorType& result) {
  auto string = static_cast<std::string>(value);
  if (string == "all") {
    result = DataDetectorType::All;
  } else if (string == "email") {
    result = DataDetectorType::Email;
  } else if (string == "link") {
    result = DataDetectorType::Link;
  } else if (string == "none") {
    result = DataDetectorType::None;
  } else if (string == "phoneNumber") {
    result = DataDetectorType::PhoneNumber;
  } else {
    result = DataDetectorType::None;
  }
}

class HostPlatformParagraphProps : public BaseParagraphProps {
 public:
  HostPlatformParagraphProps() = default;
  HostPlatformParagraphProps(
      const PropsParserContext& context,
      const HostPlatformParagraphProps& sourceProps,
      const RawProps& rawProps)
      : BaseParagraphProps(context, sourceProps, rawProps),
        disabled(convertRawProp(
            context,
            rawProps,
            "disabled",
            sourceProps.disabled,
            {})),
        dataDetectorType(convertRawProp(
            context,
            rawProps,
            "dataDetectorType",
            sourceProps.dataDetectorType,
            {})) {}

  void setProp(
      const PropsParserContext& context,
      RawPropsPropNameHash hash,
      const char* propName,
      const RawValue& value) {
    BaseParagraphProps::setProp(context, hash, propName, value);
    static auto defaults = HostPlatformParagraphProps{};
    switch (hash) {
      RAW_SET_PROP_SWITCH_CASE_BASIC(disabled);
      RAW_SET_PROP_SWITCH_CASE_BASIC(dataDetectorType);
    }
  }

  bool disabled{false};
  std::optional<DataDetectorType> dataDetectorType{};
};

} // namespace facebook::react
