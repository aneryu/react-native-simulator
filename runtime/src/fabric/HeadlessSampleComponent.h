#pragma once

#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>

#include <string>

namespace facebook::react {

extern const char HeadlessSampleViewComponentName[];

class HeadlessSampleViewProps final : public ViewProps {
 public:
  HeadlessSampleViewProps() = default;
  HeadlessSampleViewProps(
      const PropsParserContext& context,
      const HeadlessSampleViewProps& sourceProps,
      const RawProps& rawProps);

  int value{0};
  std::string label;
};

using HeadlessSampleViewShadowNode = ConcreteViewShadowNode<
    HeadlessSampleViewComponentName,
    HeadlessSampleViewProps,
    ViewEventEmitter>;
using HeadlessSampleViewComponentDescriptor =
    ConcreteComponentDescriptor<HeadlessSampleViewShadowNode>;

} // namespace facebook::react
