#include "HeadlessSampleComponent.h"

#include <react/renderer/core/propsConversions.h>

namespace facebook::react {

const char HeadlessSampleViewComponentName[] = "HeadlessSampleView";

HeadlessSampleViewProps::HeadlessSampleViewProps(
    const PropsParserContext& context,
    const HeadlessSampleViewProps& sourceProps,
    const RawProps& rawProps)
    : ViewProps(context, sourceProps, rawProps),
      value(convertRawProp(
          context, rawProps, "value", sourceProps.value, 0)),
      label(convertRawProp(
          context, rawProps, "label", sourceProps.label, std::string{})) {}

} // namespace facebook::react
