#include "HeadlessOfficialComponents.h"

#include <react/renderer/components/scrollview/AndroidHorizontalScrollContentViewComponentDescriptor.h>
#include <react/renderer/components/unimplementedview/UnimplementedViewComponentDescriptor.h>
#include <react/renderer/core/ComponentDescriptor.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/core/graphicsConversions.h>
#include <react/renderer/core/propsConversions.h>
#include <react/utils/ContextContainer.h>

#include <string>
#include <yoga/node/Node.h>

namespace facebook::react {

const char HeadlessActivityIndicatorViewName[] = "ActivityIndicatorView";
const char HeadlessAndroidSwitchName[] = "AndroidSwitch";
const char HeadlessSwitchName[] = "Switch";
const char HeadlessAndroidProgressBarName[] = "AndroidProgressBar";
const char HeadlessModalHostViewName[] = "ModalHostView";
const char HeadlessAndroidSwipeRefreshLayoutName[] = "AndroidSwipeRefreshLayout";
const char HeadlessAndroidDrawerLayoutName[] = "AndroidDrawerLayout";
const char HeadlessSafeAreaViewName[] = "SafeAreaView";

HeadlessActivityIndicatorViewProps::HeadlessActivityIndicatorViewProps(
    const PropsParserContext& context,
    const HeadlessActivityIndicatorViewProps& sourceProps,
    const RawProps& rawProps)
    : ViewProps(context, sourceProps, rawProps),
      animating(convertRawProp(
          context, rawProps, "animating", sourceProps.animating, true)),
      hidesWhenStopped(convertRawProp(
          context,
          rawProps,
          "hidesWhenStopped",
          sourceProps.hidesWhenStopped,
          true)),
      color(convertRawProp(
          context, rawProps, "color", sourceProps.color, SharedColor{})),
      size(convertRawProp(
          context,
          rawProps,
          "size",
          sourceProps.size,
          std::string{"small"})) {}

HeadlessAndroidProgressBarProps::HeadlessAndroidProgressBarProps(
    const PropsParserContext& context,
    const HeadlessAndroidProgressBarProps& sourceProps,
    const RawProps& rawProps)
    : ViewProps(context, sourceProps, rawProps),
      styleAttr(convertRawProp(
          context,
          rawProps,
          "styleAttr",
          sourceProps.styleAttr,
          std::string{"Normal"})),
      typeAttr(convertRawProp(
          context,
          rawProps,
          "typeAttr",
          sourceProps.typeAttr,
          std::string{})),
      indeterminate(convertRawProp(
          context,
          rawProps,
          "indeterminate",
          sourceProps.indeterminate,
          true)),
      progress(convertRawProp(
          context, rawProps, "progress", sourceProps.progress, 0.0)),
      animating(convertRawProp(
          context, rawProps, "animating", sourceProps.animating, true)),
      color(convertRawProp(
          context, rawProps, "color", sourceProps.color, SharedColor{})) {}

HeadlessAndroidSwitchProps::HeadlessAndroidSwitchProps(
    const PropsParserContext& context,
    const HeadlessAndroidSwitchProps& sourceProps,
    const RawProps& rawProps)
    : ViewProps(context, sourceProps, rawProps),
      value(convertRawProp(
          context, rawProps, "value", sourceProps.value, false)),
      on(convertRawProp(context, rawProps, "on", sourceProps.on, false)),
      disabled(convertRawProp(
          context, rawProps, "disabled", sourceProps.disabled, false)),
      enabled(convertRawProp(
          context, rawProps, "enabled", sourceProps.enabled, true)),
      thumbTintColor(convertRawProp(
          context,
          rawProps,
          "thumbTintColor",
          sourceProps.thumbTintColor,
          SharedColor{})),
      trackColorForFalse(convertRawProp(
          context,
          rawProps,
          "trackColorForFalse",
          sourceProps.trackColorForFalse,
          SharedColor{})),
      trackColorForTrue(convertRawProp(
          context,
          rawProps,
          "trackColorForTrue",
          sourceProps.trackColorForTrue,
          SharedColor{})),
      trackTintColor(convertRawProp(
          context,
          rawProps,
          "trackTintColor",
          sourceProps.trackTintColor,
          SharedColor{})) {}

HeadlessModalHostViewProps::HeadlessModalHostViewProps(
    const PropsParserContext& context,
    const HeadlessModalHostViewProps& sourceProps,
    const RawProps& rawProps)
    : ViewProps(context, sourceProps, rawProps),
      transparent(convertRawProp(
          context,
          rawProps,
          "transparent",
          sourceProps.transparent,
          false)),
      visible(convertRawProp(
          context, rawProps, "visible", sourceProps.visible, true)),
      animationType(convertRawProp(
          context,
          rawProps,
          "animationType",
          sourceProps.animationType,
          std::string{"none"})) {}

HeadlessAndroidSwipeRefreshLayoutProps::HeadlessAndroidSwipeRefreshLayoutProps(
    const PropsParserContext& context,
    const HeadlessAndroidSwipeRefreshLayoutProps& sourceProps,
    const RawProps& rawProps)
    : ViewProps(context, sourceProps, rawProps),
      enabled(convertRawProp(
          context, rawProps, "enabled", sourceProps.enabled, true)),
      refreshing(convertRawProp(
          context, rawProps, "refreshing", sourceProps.refreshing, false)),
      progressViewOffset(convertRawProp(
          context,
          rawProps,
          "progressViewOffset",
          sourceProps.progressViewOffset,
          0.0f)),
      size(convertRawProp(
          context, rawProps, "size", sourceProps.size, std::string{"default"})),
      progressBackgroundColor(convertRawProp(
          context,
          rawProps,
          "progressBackgroundColor",
          sourceProps.progressBackgroundColor,
          SharedColor{})),
      color(convertRawProp(
          context, rawProps, "color", sourceProps.color, SharedColor{})) {}

void HeadlessModalHostViewShadowNode::applyDialogWindowStyle(Size size) const {
  ensureUnsealed();
  auto style = yogaNode_.style();
  if (size.width > 0 && size.height > 0) {
    const auto width = yoga::StyleSizeLength::points(size.width);
    const auto height = yoga::StyleSizeLength::points(size.height);
    style.setDimension(yoga::Dimension::Width, width);
    style.setDimension(yoga::Dimension::Height, height);
    style.setMinDimension(yoga::Dimension::Width, width);
    style.setMinDimension(yoga::Dimension::Height, height);
    style.setMaxDimension(yoga::Dimension::Width, width);
    style.setMaxDimension(yoga::Dimension::Height, height);
  }
  style.setPositionType(yoga::PositionType::Absolute);
  style.setPosition(yoga::Edge::Left, yoga::StyleLength::points(0));
  style.setPosition(yoga::Edge::Top, yoga::StyleLength::points(0));
  yogaNode_.setStyle(style);
  yogaNode_.setDirty(true);
}

void HeadlessModalHostViewShadowNode::layout(LayoutContext layoutContext) {
  auto metrics = getLayoutMetrics();
  Size viewport{0, 0};
  if (const auto& container =
          getFamily().getComponentDescriptor().getContextContainer()) {
    if (auto found = container->find<HeadlessViewportSize>(
            kHeadlessViewportKey)) {
      viewport = {.width = found->width, .height = found->height};
    }
  }
  if (viewport.width > 0 && viewport.height > 0) {
    metrics.frame.size = viewport;
    float parentX = 0;
    float parentY = 0;
    const yoga::Node* owner = yogaNode_.getOwner();
    while (owner != nullptr) {
      const auto* parent =
          static_cast<const YogaLayoutableShadowNode*>(owner->getContext());
      if (parent == nullptr) {
        break;
      }
      const auto parentMetrics = parent->getLayoutMetrics();
      parentX += parentMetrics.frame.origin.x;
      parentY += parentMetrics.frame.origin.y;
      owner = owner->getOwner();
    }
    metrics.frame.origin.x = -parentX;
    metrics.frame.origin.y = -parentY;
    setLayoutMetrics(metrics);
  }
  ConcreteViewShadowNode::layout(layoutContext);
}

HeadlessAndroidDrawerLayoutProps::HeadlessAndroidDrawerLayoutProps(
    const PropsParserContext& context,
    const HeadlessAndroidDrawerLayoutProps& sourceProps,
    const RawProps& rawProps)
    : ViewProps(context, sourceProps, rawProps),
      drawerPosition(convertRawProp(
          context,
          rawProps,
          "drawerPosition",
          sourceProps.drawerPosition,
          std::string{"left"})),
      drawerLockMode(convertRawProp(
          context,
          rawProps,
          "drawerLockMode",
          sourceProps.drawerLockMode,
          std::string{"unlocked"})),
      drawerWidth(convertRawProp(
          context, rawProps, "drawerWidth", sourceProps.drawerWidth, -1.0f)),
      drawerBackgroundColor(convertRawProp(
          context,
          rawProps,
          "drawerBackgroundColor",
          sourceProps.drawerBackgroundColor,
          SharedColor{})) {}

void registerHeadlessOfficialComponents(
    ComponentDescriptorProviderRegistry& providers,
    std::vector<std::shared_ptr<std::string>>& flavorStorage) {
  providers.add(
      concreteComponentDescriptorProvider<
          HeadlessActivityIndicatorComponentDescriptor>());
  providers.add(
      concreteComponentDescriptorProvider<
          HeadlessAndroidSwitchComponentDescriptor>());
  providers.add(
      concreteComponentDescriptorProvider<
          HeadlessSwitchComponentDescriptor>());
  providers.add(
      concreteComponentDescriptorProvider<
          HeadlessAndroidProgressBarComponentDescriptor>());
  providers.add(
      concreteComponentDescriptorProvider<
          HeadlessModalHostViewComponentDescriptor>());
  providers.add(
      concreteComponentDescriptorProvider<
          AndroidHorizontalScrollContentViewComponentDescriptor>());
  providers.add(
      concreteComponentDescriptorProvider<
          HeadlessAndroidSwipeRefreshLayoutComponentDescriptor>());
  providers.add(
      concreteComponentDescriptorProvider<
          HeadlessAndroidDrawerLayoutComponentDescriptor>());
  providers.add(
      concreteComponentDescriptorProvider<
          HeadlessSafeAreaViewComponentDescriptor>());

  for (const auto& spec : kHeadlessOfficialComponents) {
    if (std::string(spec.name) == "ActivityIndicatorView" ||
        std::string(spec.name) == "AndroidSwitch" ||
        std::string(spec.name) == "Switch" ||
        std::string(spec.name) == "AndroidProgressBar" ||
        std::string(spec.name) == "ModalHostView" ||
        std::string(spec.name) == "AndroidHorizontalScrollView" ||
        std::string(spec.name) == "AndroidHorizontalScrollContentView" ||
        std::string(spec.name) == "AndroidSwipeRefreshLayout" ||
        std::string(spec.name) == "AndroidDrawerLayout" ||
        std::string(spec.name) == "SafeAreaView") {
      continue;
    }
    auto flavor = std::make_shared<std::string>(spec.name);
    flavorStorage.push_back(flavor);
    providers.add({
        reinterpret_cast<ComponentHandle>(flavor->c_str()),
        flavor->c_str(),
        flavor,
        &concreteComponentDescriptorConstructor<
            UnimplementedViewComponentDescriptor>});
  }
}

} // namespace facebook::react
