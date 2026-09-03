#include "HeadlessReactFabric.h"
#include "SimulatorEventLoop.h"
#include "HeadlessSampleComponent.h"
#include "HeadlessTextInputComponent.h"
#include "HeadlessTextLayoutManager.h"

#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/components/root/RootComponentDescriptor.h>
#include <react/renderer/components/scrollview/ScrollViewComponentDescriptor.h>
#include <react/renderer/components/image/ImageComponentDescriptor.h>
#include <react/renderer/components/image/ImageEventEmitter.h>
#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/components/image/ImageState.h>
#include <react/renderer/imagemanager/ImageResponseObserver.h>
#include <react/renderer/imagemanager/primitives.h>
#include <react/renderer/imagemanager/ImageManager.h>
#include "HeadlessImageAssets.h"
#include "HeadlessImageManager.h"
#include "HostChrome.h"
#include "HeadlessHttp.h"
#include "HeadlessI18n.h"
#include "HeadlessBackPress.h"
#include "HeadlessKeyboard.h"
#include "HeadlessOfficialComponents.h"
#include <react/renderer/components/scrollview/ScrollViewEventEmitter.h>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/scrollview/ScrollViewState.h>
#include <react/renderer/components/text/ParagraphComponentDescriptor.h>
#include <react/renderer/components/text/ParagraphProps.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/components/text/RawTextComponentDescriptor.h>
#include <react/renderer/components/text/RawTextProps.h>
#include <react/renderer/components/text/TextComponentDescriptor.h>
#include <react/renderer/components/textinput/BaseTextInputProps.h>
#include <react/renderer/components/textinput/TextInputEventEmitter.h>
#include <iostextinput/TextInputProps.h>
#include <react/renderer/components/view/BaseViewProps.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/components/view/conversions.h>
#include <react/renderer/core/EventListener.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/renderer/core/RawValue.h>
#include <react/renderer/graphics/Transform.h>
#include <react/renderer/graphics/ValueUnit.h>
#include <folly/dynamic.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/components/view/LayoutConformanceComponentDescriptor.h>
#include <react/renderer/components/view/TouchEventEmitter.h>
#include <react/renderer/components/unimplementedview/UnimplementedViewComponentDescriptor.h>
#include <react/renderer/core/EventDispatcher.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/core/EventQueueProcessor.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/core/LayoutableShadowNode.h>
#include <react/renderer/graphics/BackgroundImage.h>
#include <react/renderer/graphics/BlendMode.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/Filter.h>
#include <react/renderer/graphics/LinearGradient.h>
#include <react/renderer/graphics/RadialGradient.h>
#include <react/renderer/mounting/ShadowTree.h>
#include <react/renderer/runtimescheduler/RuntimeScheduler.h>
#include <react/renderer/animations/LayoutAnimationDriver.h>
#include <react/renderer/mounting/MountingCoordinator.h>
#include <react/renderer/uimanager/LayoutAnimationStatusDelegate.h>
#include <react/renderer/uimanager/UIManager.h>
#include <react/renderer/uimanager/UIManagerBinding.h>
#include <react/renderer/uimanager/UIManagerDelegate.h>
#include <react-native-simulator/SceneTransform.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace react = facebook::react;

namespace {
std::weak_ptr<react::UIManager> gHeadlessUIManager;

constexpr react::SurfaceId kReactSurfaceId = 21;
constexpr float kPointerScrollSlop = 8.0f;

void applyTransformMatrix(
    ReactNativeSimulator::SceneNode& node,
    const react::Transform& transform) {
  const auto& matrix = transform.matrix;
  for (int index = 0; index < 16; ++index) {
    node.transformM[index] = static_cast<float>(matrix[index]);
  }
  node.hasTransform =
      !ReactNativeSimulator::transformMatrixIsIdentity(node.transformM);
}

void applyResolvedSpinnerColor(
    ReactNativeSimulator::SceneNode& node,
    const react::SharedColor& color,
    float red,
    float green,
    float blue,
    float alpha) {
  node.hasActivityIndicatorColor = true;
  if (color) {
    const auto components = react::colorComponentsFromColor(color);
    node.activityIndicatorRed = components.red;
    node.activityIndicatorGreen = components.green;
    node.activityIndicatorBlue = components.blue;
    node.activityIndicatorAlpha = components.alpha;
    return;
  }
  node.activityIndicatorRed = red;
  node.activityIndicatorGreen = green;
  node.activityIndicatorBlue = blue;
  node.activityIndicatorAlpha = alpha;
}

void applyActivityIndicator(
    ReactNativeSimulator::SceneNode& node,
    const react::Props::Shared& props) {
  if (const auto indicator = std::dynamic_pointer_cast<
          const react::HeadlessActivityIndicatorViewProps>(props)) {
    node.activityIndicator = true;
    node.activityIndicatorAnimating = indicator->animating;
    node.activityIndicatorHidesWhenStopped = indicator->hidesWhenStopped;
    node.activityIndicatorHorizontal = false;
    applyResolvedSpinnerColor(
        node, indicator->color, 0.6f, 0.6f, 0.6f, 1.0f);
    return;
  }
  if (const auto bar = std::dynamic_pointer_cast<
          const react::HeadlessAndroidProgressBarProps>(props)) {
    node.activityIndicator = true;
    node.activityIndicatorAnimating = bar->animating;
    node.activityIndicatorHidesWhenStopped = true;
    node.activityIndicatorHorizontal = bar->styleAttr == "Horizontal";
    node.activityIndicatorProgress =
        static_cast<float>(std::clamp(bar->progress, 0.0, 1.0));
    // Pixel 4a AppCompat DayNight ProgressBar uses colorControlActivated,
    // sampled from the device ActivityIndicator example as #3A8377.
    applyResolvedSpinnerColor(
        node, bar->color, 0.22745098f, 0.5137255f, 0.46666667f, 1.0f);
  }
}

void applyViewTransform(
    ReactNativeSimulator::SceneNode& node,
    const react::BaseViewProps& viewProps) {
  applyTransformMatrix(
      node,
      react::BaseViewProps::resolveTransform(
          {.width = node.width, .height = node.height},
          viewProps.transform,
          viewProps.transformOrigin));
  const auto& origin = viewProps.transformOrigin;
  if (!origin.isSet()) {
    node.hasTransformOrigin = false;
    node.transformOriginX = 0;
    node.transformOriginY = 0;
    return;
  }
  node.hasTransformOrigin = true;
  const float viewCenterX = node.width * 0.5f;
  const float viewCenterY = node.height * 0.5f;
  float originX = viewCenterX;
  float originY = viewCenterY;
  if (origin.xy[0].unit == react::UnitType::Point) {
    originX = origin.xy[0].value;
  } else if (origin.xy[0].unit == react::UnitType::Percent) {
    originX = node.width * origin.xy[0].value / 100.0f;
  }
  if (origin.xy[1].unit == react::UnitType::Point) {
    originY = origin.xy[1].value;
  } else if (origin.xy[1].unit == react::UnitType::Percent) {
    originY = node.height * origin.xy[1].value / 100.0f;
  }
  node.transformOriginX = -viewCenterX + originX;
  node.transformOriginY = -viewCenterY + originY;
}

react::TransformOrigin animatedTransformOrigin(
    const ReactNativeSimulator::SceneNode& node) {
  if (!node.hasTransformOrigin) {
    return {};
  }
  react::TransformOrigin origin;
  origin.xy[0] = react::ValueUnit(
      node.width * 0.5f + node.transformOriginX, react::UnitType::Point);
  origin.xy[1] = react::ValueUnit(
      node.height * 0.5f + node.transformOriginY, react::UnitType::Point);
  return origin;
}

bool usesIosScrollContentInset(const std::string& platform) {
  return platform == "ios" || platform == "macos";
}

void clampScrollOffset(
    ReactNativeSimulator::SceneNode& node,
    bool iosStyleInsets) {
  float minX = 0;
  float minY = 0;
  float maxX = std::max(0.0f, node.scrollContentWidth - node.width);
  float maxY = std::max(0.0f, node.scrollContentHeight - node.height);
  if (iosStyleInsets) {
    // UIScrollView contentOffset may rest at -contentInset so the inset
    // area is empty padding. Android ScrollView.scrollTo clamps n<0 to 0
    // and ignores contentInset, which is why SectionList contentInset
    // examples start with the first sticky header under the overlay.
    minX = -node.contentInsetLeft;
    minY = -node.contentInsetTop;
    maxX = std::max(
        minX,
        node.scrollContentWidth - node.width + node.contentInsetRight);
    maxY = std::max(
        minY,
        node.scrollContentHeight - node.height + node.contentInsetBottom);
  }
  node.scrollOffsetX = std::clamp(node.scrollOffsetX, minX, maxX);
  node.scrollOffsetY = std::clamp(node.scrollOffsetY, minY, maxY);
}

void applyScrollViewMetrics(
    ReactNativeSimulator::SceneNode& node,
    const react::Props::Shared& props,
    const react::State::Shared& state,
    bool iosStyleInsets) {
  // AndroidHorizontalScrollContentView reuses ScrollViewState for RTL/culling
  // but is the content container. Marking it scrollable makes findScrollableTag
  // pick it first; its width already equals its children so deltaX clamps to 0.
  if (node.componentName == "AndroidHorizontalScrollContentView") {
    return;
  }
  const auto scrollProps =
      std::dynamic_pointer_cast<const react::ScrollViewProps>(props);
  const auto scrollState = std::dynamic_pointer_cast<
      const react::ConcreteState<react::ScrollViewState>>(state);
  if (!scrollProps || !scrollState || !scrollProps->scrollEnabled) {
    return;
  }
  const auto& data = scrollState->getData();
  node.scrollable = true;
  const auto contentSize = data.getContentSize();
  node.scrollContentWidth = contentSize.width;
  node.scrollContentHeight = contentSize.height;
  node.contentInsetTop = scrollProps->contentInset.top;
  node.contentInsetRight = scrollProps->contentInset.right;
  node.contentInsetBottom = scrollProps->contentInset.bottom;
  node.contentInsetLeft = scrollProps->contentInset.left;
  node.scrollOffsetX = data.contentOffset.x;
  node.scrollOffsetY = data.contentOffset.y;
  clampScrollOffset(node, iosStyleInsets);
}

void applyPackedColor(
    float& red,
    float& green,
    float& blue,
    float& alpha,
    std::uint32_t packed) {
  alpha = static_cast<float>((packed >> 24) & 0xff) / 255.0f;
  red = static_cast<float>((packed >> 16) & 0xff) / 255.0f;
  green = static_cast<float>((packed >> 8) & 0xff) / 255.0f;
  blue = static_cast<float>(packed & 0xff) / 255.0f;
}

void applyDynamicAnimatedProps(
    ReactNativeSimulator::SceneNode& node,
    const folly::dynamic& props,
    const react::ContextContainer& contextContainer) {
  if (!props.isObject()) {
    return;
  }
  if (const auto* opacity = props.get_ptr("opacity");
      opacity != nullptr && opacity->isNumber()) {
    node.opacity = static_cast<float>(opacity->asDouble());
  }
  if (const auto* transform = props.get_ptr("transform");
      transform != nullptr) {
    react::Transform parsed;
    react::PropsParserContext parserContext{kReactSurfaceId, contextContainer};
    react::fromRawValue(
        parserContext, react::RawValue(*transform), parsed);
    applyTransformMatrix(
        node,
        react::BaseViewProps::resolveTransform(
            {.width = node.width, .height = node.height},
            parsed,
            animatedTransformOrigin(node)));
  } else {
    const auto* scaleX = props.get_ptr("scaleX");
    const auto* scaleY = props.get_ptr("scaleY");
    const auto* translateX = props.get_ptr("translateX");
    const auto* translateY = props.get_ptr("translateY");
    if ((scaleX != nullptr && scaleX->isNumber()) ||
        (scaleY != nullptr && scaleY->isNumber()) ||
        (translateX != nullptr && translateX->isNumber()) ||
        (translateY != nullptr && translateY->isNumber())) {
      auto transform = react::Transform::Identity();
      if (scaleX != nullptr && scaleX->isNumber()) {
        transform = transform *
            react::Transform::Scale(
                static_cast<react::Float>(scaleX->asDouble()), 1, 1);
      }
      if (scaleY != nullptr && scaleY->isNumber()) {
        transform = transform *
            react::Transform::Scale(
                1, static_cast<react::Float>(scaleY->asDouble()), 1);
      }
      if (translateX != nullptr && translateX->isNumber()) {
        transform = transform *
            react::Transform::Translate(
                static_cast<react::Float>(translateX->asDouble()), 0, 0);
      }
      if (translateY != nullptr && translateY->isNumber()) {
        transform = transform *
            react::Transform::Translate(
                0, static_cast<react::Float>(translateY->asDouble()), 0);
      }
      applyTransformMatrix(
          node,
          react::BaseViewProps::resolveTransform(
              {.width = node.width, .height = node.height},
              transform,
              animatedTransformOrigin(node)));
    }
  }
  if (const auto* background = props.get_ptr("backgroundColor");
      background != nullptr && background->isNumber()) {
    node.hasBackgroundColor = true;
    applyPackedColor(
        node.backgroundRed,
        node.backgroundGreen,
        node.backgroundBlue,
        node.backgroundAlpha,
        static_cast<std::uint32_t>(background->asInt()));
  }
}

void mergeDynamicObjects(folly::dynamic& out, const folly::dynamic& incoming) {
  if (!incoming.isObject()) {
    return;
  }
  if (!out.isObject() || out.empty()) {
    out = incoming;
    return;
  }
  for (const auto& pair : incoming.items()) {
    out[pair.first] = pair.second;
  }
}

void applyOutline(
    ReactNativeSimulator::SceneNode& node,
    const react::BaseViewProps& viewProps) {
  node.outlineWidth = viewProps.outlineWidth;
  node.outlineOffset = viewProps.outlineOffset;
  switch (viewProps.outlineStyle) {
    case react::OutlineStyle::Dotted:
      node.outlineStyle = "dotted";
      break;
    case react::OutlineStyle::Dashed:
      node.outlineStyle = "dashed";
      break;
    case react::OutlineStyle::Solid:
    default:
      node.outlineStyle = "solid";
      break;
  }
  if (viewProps.outlineColor) {
    const auto color = react::colorComponentsFromColor(viewProps.outlineColor);
    node.hasOutlineColor = true;
    node.outlineRed = color.red;
    node.outlineGreen = color.green;
    node.outlineBlue = color.blue;
    node.outlineAlpha = color.alpha;
  }
}

void syncPrimaryBoxShadow(ReactNativeSimulator::SceneNode& node) {
  node.hasBoxShadow = !node.boxShadows.empty();
  if (node.boxShadows.empty()) {
    return;
  }
  const auto& shadow = node.boxShadows.front();
  node.boxShadowOffsetX = shadow.offsetX;
  node.boxShadowOffsetY = shadow.offsetY;
  node.boxShadowBlur = shadow.blur;
  node.boxShadowSpread = shadow.spread;
  node.boxShadowInset = shadow.inset;
  node.boxShadowRed = shadow.red;
  node.boxShadowGreen = shadow.green;
  node.boxShadowBlue = shadow.blue;
  node.boxShadowAlpha = shadow.alpha;
}

void applyBoxShadow(
    ReactNativeSimulator::SceneNode& node,
    const react::BaseViewProps& viewProps) {
  node.boxShadows.clear();
  node.hasBoxShadow = false;
  const auto pushShadow = [&node](
                              float offsetX,
                              float offsetY,
                              float blur,
                              float spread,
                              float red,
                              float green,
                              float blue,
                              float alpha,
                              bool inset) {
    if (alpha <= 0.001f) {
      return;
    }
    node.boxShadows.push_back({
        .offsetX = offsetX,
        .offsetY = offsetY,
        .blur = blur,
        .spread = spread,
        .red = red,
        .green = green,
        .blue = blue,
        .alpha = alpha,
        .inset = inset,
    });
  };
  if (!viewProps.boxShadow.empty()) {
    for (const auto& shadow : viewProps.boxShadow) {
      float red = 0;
      float green = 0;
      float blue = 0;
      float alpha = 1;
      if (shadow.color) {
        const auto color = react::colorComponentsFromColor(shadow.color);
        red = color.red;
        green = color.green;
        blue = color.blue;
        alpha = color.alpha;
      } else if (shadow.blurRadius > 0.0f) {
        // JS processColor(rgba(0,0,0,0)) is 0, same as SharedColor{}.
        // Android Java uses hasKey("color") and treats 0 as transparent.
        // ViewExample's omitted color is `0px 10px` (blur 0 → black);
        // explicit transparent is `5px 5px 5px 0px rgba(0,0,0,0)` (blur>0).
        continue;
      }
      pushShadow(
          shadow.offsetX,
          shadow.offsetY,
          shadow.blurRadius,
          shadow.spreadDistance,
          red,
          green,
          blue,
          alpha,
          shadow.inset);
    }
  } else if (const auto* view = dynamic_cast<const react::ViewProps*>(&viewProps);
             view != nullptr && view->elevation > 0) {
    const auto elevation = view->elevation;
    pushShadow(0, 0, elevation, 0, 0, 0, 0, 0.12f, false);
    pushShadow(0, elevation * 0.5f, elevation, 0, 0, 0, 0, 0.24f, false);
  }
  syncPrimaryBoxShadow(node);
}

void applyResolvedBorder(
    ReactNativeSimulator::SceneNode& node,
    const react::BaseViewProps& viewProps,
    const react::LayoutMetrics& layout) {
  const auto border = viewProps.resolveBorderMetrics(layout);
  node.borderRadiusTopLeftX = border.borderRadii.topLeft.horizontal;
  node.borderRadiusTopLeftY = border.borderRadii.topLeft.vertical;
  node.borderRadiusTopRightX = border.borderRadii.topRight.horizontal;
  node.borderRadiusTopRightY = border.borderRadii.topRight.vertical;
  node.borderRadiusBottomRightX = border.borderRadii.bottomRight.horizontal;
  node.borderRadiusBottomRightY = border.borderRadii.bottomRight.vertical;
  node.borderRadiusBottomLeftX = border.borderRadii.bottomLeft.horizontal;
  node.borderRadiusBottomLeftY = border.borderRadii.bottomLeft.vertical;
  node.borderRadiusTopLeft = std::min(
      node.borderRadiusTopLeftX, node.borderRadiusTopLeftY);
  node.borderRadiusTopRight = std::min(
      node.borderRadiusTopRightX, node.borderRadiusTopRightY);
  node.borderRadiusBottomRight = std::min(
      node.borderRadiusBottomRightX, node.borderRadiusBottomRightY);
  node.borderRadiusBottomLeft = std::min(
      node.borderRadiusBottomLeftX, node.borderRadiusBottomLeftY);
  node.borderRadius = node.borderRadiusTopLeft;
  const auto applyEdgeColor =
      [](const react::SharedColor& color,
         bool& has,
         float& red,
         float& green,
         float& blue,
         float& alpha) {
        if (!color) {
          has = false;
          return;
        }
        const auto components = react::colorComponentsFromColor(color);
        has = true;
        red = components.red;
        green = components.green;
        blue = components.blue;
        alpha = components.alpha;
      };
  applyEdgeColor(
      border.borderColors.top,
      node.hasBorderTopColor,
      node.borderTopRed,
      node.borderTopGreen,
      node.borderTopBlue,
      node.borderTopAlpha);
  applyEdgeColor(
      border.borderColors.right,
      node.hasBorderRightColor,
      node.borderRightRed,
      node.borderRightGreen,
      node.borderRightBlue,
      node.borderRightAlpha);
  applyEdgeColor(
      border.borderColors.bottom,
      node.hasBorderBottomColor,
      node.borderBottomRed,
      node.borderBottomGreen,
      node.borderBottomBlue,
      node.borderBottomAlpha);
  applyEdgeColor(
      border.borderColors.left,
      node.hasBorderLeftColor,
      node.borderLeftRed,
      node.borderLeftGreen,
      node.borderLeftBlue,
      node.borderLeftAlpha);
  const auto styleName = [](react::BorderStyle style) {
    switch (style) {
      case react::BorderStyle::Dotted:
        return "dotted";
      case react::BorderStyle::Dashed:
        return "dashed";
      case react::BorderStyle::Solid:
      default:
        return "solid";
    }
  };
  node.borderStyleTop = styleName(border.borderStyles.top);
  node.borderStyleRight = styleName(border.borderStyles.right);
  node.borderStyleBottom = styleName(border.borderStyles.bottom);
  node.borderStyleLeft = styleName(border.borderStyles.left);
  if (node.hasBorderTopColor) {
    node.hasBorderColor = true;
    node.borderRed = node.borderTopRed;
    node.borderGreen = node.borderTopGreen;
    node.borderBlue = node.borderTopBlue;
    node.borderAlpha = node.borderTopAlpha;
  } else if (node.hasBorderLeftColor) {
    node.hasBorderColor = true;
    node.borderRed = node.borderLeftRed;
    node.borderGreen = node.borderLeftGreen;
    node.borderBlue = node.borderLeftBlue;
    node.borderAlpha = node.borderLeftAlpha;
  } else if (node.hasBorderRightColor) {
    node.hasBorderColor = true;
    node.borderRed = node.borderRightRed;
    node.borderGreen = node.borderRightGreen;
    node.borderBlue = node.borderRightBlue;
    node.borderAlpha = node.borderRightAlpha;
  } else if (node.hasBorderBottomColor) {
    node.hasBorderColor = true;
    node.borderRed = node.borderBottomRed;
    node.borderGreen = node.borderBottomGreen;
    node.borderBlue = node.borderBottomBlue;
    node.borderAlpha = node.borderBottomAlpha;
  }
}

void applyTextInputColors(
    ReactNativeSimulator::SceneNode& node,
    const react::BaseTextInputProps& inputProps) {
  const auto attributes = inputProps.getEffectiveTextAttributes(1.0);
  node.fontSize = std::isfinite(attributes.fontSize)
      ? attributes.fontSize : 14.0f;
  node.fontWeight = attributes.fontWeight
      ? static_cast<int>(*attributes.fontWeight) : 400;
  node.fontFamily = attributes.fontFamily;
  const auto color = node.text.empty() && inputProps.placeholderTextColor
      ? inputProps.placeholderTextColor
      : attributes.foregroundColor;
  if (color) {
    const auto components = react::colorComponentsFromColor(color);
    node.hasTextColor = true;
    node.textRed = components.red;
    node.textGreen = components.green;
    node.textBlue = components.blue;
    node.textAlpha = components.alpha;
  }
}

void applyImageSource(
    ReactNativeSimulator::SceneNode& node,
    const react::Props::Shared& props,
    const std::filesystem::path& assetRoot) {
  const auto imageProps =
      std::dynamic_pointer_cast<const react::ImageProps>(props);
  if (!imageProps || imageProps->sources.empty()) {
    return;
  }
  node.imageUri = imageProps->sources.front().uri;
  auto resolved = resolveHeadlessLocalImage(node.imageUri, assetRoot);
  if (resolved.empty()) {
    resolved = headlessCachedImagePath(node.imageUri);
  }
  if (!resolved.empty()) {
    node.imagePath = resolved.string();
  }
  if (!imageProps->defaultSource.uri.empty()) {
    auto fallback = resolveHeadlessLocalImage(
        imageProps->defaultSource.uri, assetRoot);
    if (fallback.empty()) {
      fallback = headlessCachedImagePath(imageProps->defaultSource.uri);
    }
    if (!fallback.empty()) {
      node.imageDefaultPath = fallback.string();
    }
  }
  node.imageBlurRadius = imageProps->blurRadius;
  switch (imageProps->resizeMode) {
    case react::ImageResizeMode::Cover:
      node.imageResizeMode = "cover";
      break;
    case react::ImageResizeMode::Contain:
      node.imageResizeMode = "contain";
      break;
    case react::ImageResizeMode::Center:
      node.imageResizeMode = "center";
      break;
    case react::ImageResizeMode::Repeat:
      node.imageResizeMode = "repeat";
      break;
    case react::ImageResizeMode::None:
      node.imageResizeMode = "none";
      break;
    case react::ImageResizeMode::Stretch:
    default:
      node.imageResizeMode = "stretch";
      break;
  }
  if (imageProps->tintColor) {
    const auto color = react::colorComponentsFromColor(imageProps->tintColor);
    node.hasImageTint = true;
    node.imageTintRed = color.red;
    node.imageTintGreen = color.green;
    node.imageTintBlue = color.blue;
    node.imageTintAlpha = color.alpha;
  }
}

void applyAndroidSwitch(
    ReactNativeSimulator::SceneNode& node,
    const react::Props::Shared& props) {
  const auto switchProps = std::dynamic_pointer_cast<
      const react::HeadlessAndroidSwitchProps>(props);
  if (!switchProps) {
    return;
  }
  node.androidSwitch = true;
  node.androidSwitchOn = switchProps->isOn();
  node.androidSwitchEnabled = switchProps->isEnabled();
  const auto applyColor = [](const react::SharedColor& color,
                             bool& has,
                             float& red,
                             float& green,
                             float& blue,
                             float& alpha) {
    if (!color) {
      return;
    }
    const auto components = react::colorComponentsFromColor(color);
    has = true;
    red = components.red;
    green = components.green;
    blue = components.blue;
    alpha = components.alpha;
  };
  applyColor(
      switchProps->thumbTintColor,
      node.hasSwitchThumbColor,
      node.switchThumbRed,
      node.switchThumbGreen,
      node.switchThumbBlue,
      node.switchThumbAlpha);
  auto track = switchProps->trackTintColor;
  if (!track) {
    track = switchProps->isOn() ? switchProps->trackColorForTrue
                                : switchProps->trackColorForFalse;
  }
  applyColor(
      track,
      node.hasSwitchTrackColor,
      node.switchTrackRed,
      node.switchTrackGreen,
      node.switchTrackBlue,
      node.switchTrackAlpha);
}

void applyModalHost(
    ReactNativeSimulator::SceneNode& node,
    const react::Props::Shared& props) {
  const auto modal = std::dynamic_pointer_cast<
      const react::HeadlessModalHostViewProps>(props);
  if (!modal) {
    return;
  }
  node.modalHost = true;
  node.modalTransparent = modal->transparent;
}

// Android Dialog windows cover the engine viewport at the window origin.
void placeModalDialogWindow(
    ReactNativeSimulator::SceneNode& node,
    float viewportWidth,
    float viewportHeight) {
  if (!node.modalHost || viewportWidth <= 0 || viewportHeight <= 0) {
    return;
  }
  node.x = 0;
  node.y = 0;
  node.width = viewportWidth;
  node.height = viewportHeight;
  node.absoluteX = 0;
  node.absoluteY = 0;
}

void applyViewEffects(
    ReactNativeSimulator::SceneNode& node,
    const react::BaseViewProps& viewProps) {
  if (viewProps.mixBlendMode == react::BlendMode::Normal) {
    node.mixBlendMode.clear();
  } else {
    node.mixBlendMode = react::toString(viewProps.mixBlendMode);
  }
  node.isolationIsolate = viewProps.isolation == react::Isolation::Isolate;
  node.backfaceHidden =
      viewProps.backfaceVisibility == react::BackfaceVisibility::Hidden;
  if (const auto* android = dynamic_cast<const react::ViewProps*>(&viewProps)) {
    node.needsOffscreenAlphaCompositing =
        android->needsOffscreenAlphaCompositing;
    node.nativeRipple = false;
    node.nativeRippleBorderless = false;
    const auto applyDrawable =
        [&node](const std::optional<react::NativeDrawable>& drawable) {
          if (!drawable) {
            return;
          }
          node.nativeRipple = true;
          if (drawable->kind == react::NativeDrawable::Kind::Ripple) {
            node.nativeRippleBorderless = drawable->ripple.borderless;
            if (drawable->ripple.color) {
              const auto color =
                  react::colorComponentsFromColor(*drawable->ripple.color);
              node.nativeRippleRed = color.red;
              node.nativeRippleGreen = color.green;
              node.nativeRippleBlue = color.blue;
              node.nativeRippleAlpha = drawable->ripple.alpha
                  ? static_cast<float>(*drawable->ripple.alpha)
                  : color.alpha;
            } else {
              node.nativeRippleRed = 0;
              node.nativeRippleGreen = 0;
              node.nativeRippleBlue = 0;
              node.nativeRippleAlpha = 0.2f;
            }
          } else {
            node.nativeRippleRed = 0;
            node.nativeRippleGreen = 0;
            node.nativeRippleBlue = 0;
            node.nativeRippleAlpha = 0.2f;
          }
        };
    applyDrawable(android->nativeBackground);
    if (!node.nativeRipple) {
      applyDrawable(android->nativeForeground);
    }
  }
  node.filters.clear();
  for (const auto& filter : viewProps.filter) {
    ReactNativeSimulator::SceneNode::FilterOp op;
    op.type = react::toString(filter.type);
    if (const auto* shadow =
            std::get_if<react::DropShadowParams>(&filter.parameters)) {
      op.dropShadowOffsetX = static_cast<float>(shadow->offsetX);
      op.dropShadowOffsetY = static_cast<float>(shadow->offsetY);
      op.dropShadowStdDev = static_cast<float>(shadow->standardDeviation);
      if (shadow->color) {
        const auto color = react::colorComponentsFromColor(shadow->color);
        op.dropShadowRed = color.red;
        op.dropShadowGreen = color.green;
        op.dropShadowBlue = color.blue;
        op.dropShadowAlpha = color.alpha;
      }
    } else if (std::holds_alternative<react::Float>(filter.parameters)) {
      op.amount = static_cast<float>(std::get<react::Float>(filter.parameters));
    }
    switch (filter.type) {
      case react::FilterType::Blur:
        node.filterBlur = op.amount;
        break;
      case react::FilterType::Brightness:
        node.filterBrightness = op.amount;
        break;
      case react::FilterType::Contrast:
        node.filterContrast = op.amount;
        break;
      case react::FilterType::Grayscale:
        node.filterGrayscale = op.amount;
        break;
      case react::FilterType::Saturate:
        node.filterSaturate = op.amount;
        break;
      case react::FilterType::Sepia:
        node.filterSepia = op.amount;
        break;
      case react::FilterType::Invert:
        node.filterInvert = op.amount;
        break;
      case react::FilterType::HueRotate:
        node.filterHueRotate = op.amount;
        break;
      case react::FilterType::Opacity:
        node.opacity *= op.amount;
        break;
      case react::FilterType::DropShadow:
        break;
    }
    node.filters.push_back(std::move(op));
  }
  if (viewProps.backgroundImage.empty()) {
    return;
  }
  // background-repeat / background-size are not painted.
  const auto convertStops =
      [](const std::vector<react::ColorStop>& stops) {
        std::vector<ReactNativeSimulator::SceneNode::GradientStop> out;
        std::vector<char> hasPos;
        out.reserve(stops.size());
        hasPos.reserve(stops.size());
        for (const auto& stop : stops) {
          if (!stop.color) {
            continue;
          }
          const auto color = react::colorComponentsFromColor(stop.color);
          ReactNativeSimulator::SceneNode::GradientStop converted;
          converted.red = color.red;
          converted.green = color.green;
          converted.blue = color.blue;
          converted.alpha = color.alpha;
          const bool defined =
              stop.position.unit != react::UnitType::Undefined;
          if (defined) {
            converted.offset = stop.position.resolve(1.f);
          }
          out.push_back(converted);
          hasPos.push_back(defined ? 1 : 0);
        }
        if (out.empty()) {
          return out;
        }
        if (out.size() == 1) {
          if (!hasPos.front()) {
            out.front().offset = 0;
          }
          return out;
        }
        if (!hasPos.front()) {
          out.front().offset = 0;
          hasPos.front() = 1;
        }
        if (!hasPos.back()) {
          out.back().offset = 1;
          hasPos.back() = 1;
        }
        for (std::size_t i = 1; i < out.size();) {
          if (hasPos[i]) {
            out[i].offset = std::max(out[i].offset, out[i - 1].offset);
            ++i;
            continue;
          }
          std::size_t j = i;
          while (j < out.size() && !hasPos[j]) {
            ++j;
          }
          const float start = out[i - 1].offset;
          const float end = out[j].offset;
          const float denom = static_cast<float>(j - i + 1);
          for (std::size_t k = i; k < j; ++k) {
            out[k].offset =
                start + (end - start) * static_cast<float>(k - i + 1) / denom;
            hasPos[k] = 1;
          }
          i = j;
        }
        return out;
      };
  const auto applyLegacyStops =
      [&node](const std::vector<ReactNativeSimulator::SceneNode::GradientStop>&
                  stops) {
        if (stops.empty()) {
          return;
        }
        const auto& first = stops.front();
        node.backgroundGradientR0 = first.red;
        node.backgroundGradientG0 = first.green;
        node.backgroundGradientB0 = first.blue;
        node.backgroundGradientA0 = first.alpha;
        const auto& last = stops.back();
        node.backgroundGradientR1 = last.red;
        node.backgroundGradientG1 = last.green;
        node.backgroundGradientB1 = last.blue;
        node.backgroundGradientA1 = last.alpha;
      };
  const auto unitCoord =
      [](const std::optional<react::ValueUnit>& unit, float size, float fallback) {
        if (!unit || unit->unit == react::UnitType::Undefined) {
          return fallback;
        }
        if (unit->unit == react::UnitType::Percent) {
          return unit->resolve(1.f);
        }
        return size > 0 ? unit->value / size : fallback;
      };
  node.backgroundImageLayers.clear();
  bool firstLayer = true;
  for (const auto& image : viewProps.backgroundImage) {
    ReactNativeSimulator::SceneNode::BackgroundImageLayer layer;
    if (const auto* linear = std::get_if<react::LinearGradient>(&image)) {
      layer.radial = false;
      if (const auto* degrees =
              std::get_if<react::Float>(&linear->direction)) {
        const auto angle = static_cast<float>(*degrees);
        const auto radians = angle * 3.14159265f / 180.0f;
        layer.x0 = 0.5f - 0.5f * std::sin(radians);
        layer.y0 = 0.5f + 0.5f * std::cos(radians);
        layer.x1 = 0.5f + 0.5f * std::sin(radians);
        layer.y1 = 0.5f - 0.5f * std::cos(radians);
      } else if (
          const auto* keyword =
              std::get_if<react::GradientKeyword>(&linear->direction)) {
        switch (*keyword) {
          case react::GradientKeyword::ToTopRight:
            layer.x0 = 0;
            layer.y0 = 1;
            layer.x1 = 1;
            layer.y1 = 0;
            break;
          case react::GradientKeyword::ToBottomRight:
            layer.x0 = 0;
            layer.y0 = 0;
            layer.x1 = 1;
            layer.y1 = 1;
            break;
          case react::GradientKeyword::ToTopLeft:
            layer.x0 = 1;
            layer.y0 = 1;
            layer.x1 = 0;
            layer.y1 = 0;
            break;
          case react::GradientKeyword::ToBottomLeft:
            layer.x0 = 1;
            layer.y0 = 0;
            layer.x1 = 0;
            layer.y1 = 1;
            break;
        }
      }
      layer.stops = convertStops(linear->colorStops);
    } else if (const auto* radial = std::get_if<react::RadialGradient>(&image)) {
      layer.radial = true;
      layer.ellipse = radial->shape == react::RadialGradientShape::Ellipse;
      layer.x0 = 0.5f;
      layer.y0 = 0.5f;
      if (radial->position.left) {
        layer.x0 = unitCoord(radial->position.left, node.width, 0.5f);
      } else if (radial->position.right) {
        layer.x0 = 1.0f - unitCoord(radial->position.right, node.width, 0.5f);
      }
      if (radial->position.top) {
        layer.y0 = unitCoord(radial->position.top, node.height, 0.5f);
      } else if (radial->position.bottom) {
        layer.y0 = 1.0f - unitCoord(radial->position.bottom, node.height, 0.5f);
      }
      layer.x1 = layer.x0;
      layer.y1 = layer.y0;
      layer.stops = convertStops(radial->colorStops);
    } else {
      continue;
    }
    if (firstLayer) {
      node.hasBackgroundGradient = true;
      node.backgroundGradientRadial = layer.radial;
      node.backgroundGradientX0 = layer.x0;
      node.backgroundGradientY0 = layer.y0;
      node.backgroundGradientX1 = layer.x1;
      node.backgroundGradientY1 = layer.y1;
      applyLegacyStops(layer.stops);
      firstLayer = false;
    }
    node.backgroundImageLayers.push_back(std::move(layer));
  }
}

void applySwipeRefresh(
    ReactNativeSimulator::SceneNode& node,
    const react::Props::Shared& props) {
  const auto refresh = std::dynamic_pointer_cast<
      const react::HeadlessAndroidSwipeRefreshLayoutProps>(props);
  if (!refresh) {
    return;
  }
  node.swipeRefresh = true;
  node.swipeRefreshing = refresh->refreshing;
  node.swipeRefreshEnabled = refresh->enabled;
  node.swipeRefreshOffset = refresh->progressViewOffset;
  if (refresh->color) {
    const auto color = react::colorComponentsFromColor(refresh->color);
    node.swipeRefreshRed = color.red;
    node.swipeRefreshGreen = color.green;
    node.swipeRefreshBlue = color.blue;
    node.swipeRefreshAlpha = color.alpha;
  }
}

void applyDrawerLayout(
    ReactNativeSimulator::SceneNode& node,
    const react::Props::Shared& props) {
  const auto drawer = std::dynamic_pointer_cast<
      const react::HeadlessAndroidDrawerLayoutProps>(props);
  if (!drawer) {
    return;
  }
  node.drawerLayout = true;
  node.drawerFromLeft = drawer->drawerPosition != "right";
  node.drawerLocked = drawer->drawerLockMode == "locked-closed" ||
      drawer->drawerLockMode == "locked-open";
  // Android DrawerLayout starts closed. locked-open is the only prop that
  // forces an initial open frame; openDrawer/closeDrawer keep host state.
  if (drawer->drawerLockMode == "locked-open") {
    node.drawerOffset = 1;
  }
  node.drawerWidth = drawer->drawerWidth > 0 ? drawer->drawerWidth
                                             : node.width * 0.8f;
  if (drawer->drawerBackgroundColor) {
    const auto color =
        react::colorComponentsFromColor(drawer->drawerBackgroundColor);
    node.drawerPanelRed = color.red;
    node.drawerPanelGreen = color.green;
    node.drawerPanelBlue = color.blue;
    node.drawerPanelAlpha = color.alpha;
  }
}

bool near(float actual, float expected) {
  return std::fabs(actual - expected) < 0.01F;
}

const char* displayName(react::DisplayType display) {
  switch (display) {
    case react::DisplayType::None:
      return "none";
    case react::DisplayType::Flex:
      return "flex";
    case react::DisplayType::Contents:
      return "contents";
    case react::DisplayType::Grid:
      return "grid";
  }
  return "unknown";
}

const char* positionName(react::PositionType position) {
  switch (position) {
    case react::PositionType::Static:
      return "static";
    case react::PositionType::Relative:
      return "relative";
    case react::PositionType::Absolute:
      return "absolute";
  }
  return "unknown";
}

const char* pointerEventsName(react::PointerEventsMode pointerEvents) {
  switch (pointerEvents) {
    case react::PointerEventsMode::None:
      return "none";
    case react::PointerEventsMode::BoxNone:
      return "box-none";
    case react::PointerEventsMode::BoxOnly:
      return "box-only";
    case react::PointerEventsMode::Auto:
      return "auto";
  }
  return "auto";
}

struct Utf8Scalar {
  std::size_t byteOffset{0};
  std::size_t utf16Offset{0};
  char32_t value{0};
};

std::vector<Utf8Scalar> decodeUtf8(const std::string& text) {
  std::vector<Utf8Scalar> result;
  std::size_t utf16Offset = 0;
  for (std::size_t index = 0; index < text.size();) {
    const auto first = static_cast<unsigned char>(text[index]);
    std::size_t length = first < 0x80 ? 1 : first < 0xE0 ? 2 :
        first < 0xF0 ? 3 : 4;
    if (index + length > text.size()) {
      length = 1;
    }
    char32_t value = first & (length == 1 ? 0x7F :
        length == 2 ? 0x1F : length == 3 ? 0x0F : 0x07);
    bool valid = length == 1 || first >= (length == 2 ? 0xC2 : 0xE0);
    for (std::size_t cursor = 1; cursor < length; ++cursor) {
      const auto continuation = static_cast<unsigned char>(text[index + cursor]);
      if ((continuation & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      value = (value << 6) | (continuation & 0x3F);
    }
    if (!valid || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) {
      value = 0xFFFD;
      length = 1;
    }
    result.push_back({index, utf16Offset, value});
    utf16Offset += value > 0xFFFF ? 2 : 1;
    index += length;
  }
  return result;
}

std::size_t utf16Length(const std::string& text) {
  const auto scalars = decodeUtf8(text);
  if (scalars.empty()) {
    return 0;
  }
  return scalars.back().utf16Offset +
      (scalars.back().value > 0xFFFF ? 2 : 1);
}

std::size_t byteOffsetForUtf16(
    const std::string& text,
    std::size_t offset) {
  for (const auto& scalar : decodeUtf8(text)) {
    if (scalar.utf16Offset >= offset) {
      return scalar.byteOffset;
    }
  }
  return text.size();
}

bool isGraphemeExtension(char32_t value) {
  return (value >= 0x0300 && value <= 0x036F) ||
      (value >= 0x1AB0 && value <= 0x1AFF) ||
      (value >= 0x1DC0 && value <= 0x1DFF) ||
      (value >= 0x20D0 && value <= 0x20FF) ||
      (value >= 0xFE00 && value <= 0xFE0F) ||
      (value >= 0xFE20 && value <= 0xFE2F) ||
      (value >= 0x1F3FB && value <= 0x1F3FF) ||
      (value >= 0xE0100 && value <= 0xE01EF);
}

std::size_t previousGraphemeOffset(
    const std::string& text,
    std::size_t utf16Offset) {
  const auto scalars = decodeUtf8(text);
  std::size_t end = 0;
  while (end < scalars.size() && scalars[end].utf16Offset < utf16Offset) {
    ++end;
  }
  if (end == 0) {
    return 0;
  }
  std::size_t start = end - 1;
  while (start > 0 && isGraphemeExtension(scalars[start].value)) {
    --start;
  }
  while (start > 0 && scalars[start - 1].value == 0x200D) {
    --start;
    if (start > 0) {
      --start;
      while (start > 0 && isGraphemeExtension(scalars[start].value)) {
        --start;
      }
    }
  }
  return scalars[start].utf16Offset;
}

class HeadlessEventBeat final : public react::EventBeat {
 public:
  HeadlessEventBeat(
      std::shared_ptr<OwnerBox> ownerBox,
      react::RuntimeScheduler& runtimeScheduler)
      : EventBeat(std::move(ownerBox), runtimeScheduler) {}

  void request() const override {
    EventBeat::request();
    induce();
  }

  void requestSynchronous() const override {
    EventBeat::requestSynchronous();
    induce();
  }
};
} // namespace

class HeadlessReactFabricHost final
    : public react::UIManagerDelegate,
      public react::LayoutAnimationStatusDelegate,
      public std::enable_shared_from_this<HeadlessReactFabricHost> {
  struct MountedViewRecord {
    ReactNativeSimulator::SceneNode node;
    std::vector<int> children;
    react::EventEmitter::Shared eventEmitter;
    react::State::Shared state;
    std::shared_ptr<const react::ImageResponseObserver> imageObserver;
    int eventCount{0};
  };

  class ImageObserver final : public react::ImageResponseObserver {
   public:
    ImageObserver(
        std::weak_ptr<HeadlessReactFabricHost> host,
        int tag,
        bool notify,
        std::shared_ptr<const react::ImageEventEmitter> emitter,
        react::ImageSource source)
        : host_(std::move(host)),
          tag_(tag),
          notify_(notify),
          emitter_(std::move(emitter)),
          source_(std::move(source)) {}

    void didReceiveProgress(
        float progress,
        int64_t loaded,
        int64_t total) const override {
      if (notify_ && emitter_) {
        emitter_->onProgress(progress, loaded, total);
      }
    }

    void didReceiveImage(const react::ImageResponse& response) const override {
      auto host = host_.lock();
      if (!host) {
        return;
      }
      if (const auto path =
              std::static_pointer_cast<std::string>(response.getImage())) {
        host->onImageLoaded(tag_, *path);
      }
      if (notify_ && emitter_) {
        emitter_->onLoad(source_);
        emitter_->onLoadEnd();
      }
    }

    void didReceiveFailure(const react::ImageLoadError& error) const override {
      auto host = host_.lock();
      if (!host || !notify_ || !emitter_) {
        return;
      }
      react::ImageErrorInfo info;
      if (const auto message =
              std::static_pointer_cast<std::string>(error.getError())) {
        info.error = *message;
      } else {
        info.error = "Could not load image";
      }
      emitter_->onError(info);
      emitter_->onLoadEnd();
    }

   private:
    std::weak_ptr<HeadlessReactFabricHost> host_;
    int tag_;
    bool notify_{false};
    std::shared_ptr<const react::ImageEventEmitter> emitter_;
    react::ImageSource source_;
  };

  struct ActivePointer {
    int targetTag{0};
    int scrollTag{0};
    int drawerTag{0};
    float drawerStartOffset{0};
    float downX{0};
    float downY{0};
    float lastX{0};
    float lastY{0};
    bool scrolling{false};
  };

 public:
  HeadlessReactFabricHost(
      facebook::jsi::Runtime& runtime,
      react::RuntimeExecutor runtimeExecutor,
      std::shared_ptr<react::RuntimeScheduler> runtimeScheduler,
      std::shared_ptr<SimulatorEventLoop> eventLoop,
      float viewportWidth,
      float viewportHeight,
      float pointScaleFactor,
      float insetTop,
      float insetBottom,
      const std::filesystem::path& fontDirectory,
      const std::filesystem::path& assetDirectory,
      const std::string& platform,
      std::vector<ReactNativeSimulator::AddonComponentDeclaration>
          addonComponents,
      std::vector<react::ComponentDescriptorProvider> addonProviders,
      HeadlessReactFabricUpdate onUpdate)
      : contextContainer_(std::make_shared<react::ContextContainer>()),
        runtimeExecutor_(runtimeExecutor),
        runtimeScheduler_(std::move(runtimeScheduler)),
        eventLoop_(std::move(eventLoop)),
        uiManager_(std::make_shared<react::UIManager>(
            runtimeExecutor, contextContainer_)),
        assetDirectory_(assetDirectory),
        viewportWidth_(viewportWidth),
        viewportHeight_(viewportHeight),
        platform_(platform),
        onUpdate_(std::move(onUpdate)) {
    textLayoutManager_ = std::make_shared<react::HeadlessTextLayoutManager>(
        contextContainer_, fontDirectory, platform);
    contextContainer_->insert(
        react::TextLayoutManagerKey,
        std::shared_ptr<react::TextLayoutManager>(textLayoutManager_));
    contextContainer_->insert(
        react::ImageManagerKey,
        std::shared_ptr<react::ImageManager>(
            std::make_shared<HeadlessImageManager>(
                contextContainer_, assetDirectory_, runtimeExecutor_)));
    contextContainer_->insert(
        react::kHeadlessViewportKey,
        react::HeadlessViewportSize{
            .width = viewportWidth,
            .height = viewportHeight,
            .insetTop = insetTop,
            .insetRight = 0,
            .insetBottom = insetBottom,
            .insetLeft = 0});
    headlessKeyboard().viewportWidth = viewportWidth;
    headlessKeyboard().viewportHeight = viewportHeight;
    MountedViewRecord rootView;
    rootView.node.tag = kReactSurfaceId;
    rootView.node.componentName = "Root";
    rootView.node.layoutable = true;
    rootView.node.width = viewportWidth;
    rootView.node.height = viewportHeight;
    mountedViews_.emplace(kReactSurfaceId, std::move(rootView));
    providers_.setComponentDescriptorProviderRequest(
        [this](react::ComponentName componentName) {
          fallbackComponentNames_.insert(componentName);
          auto flavor = std::make_shared<std::string>(componentName);
          providers_.add({
              reinterpret_cast<react::ComponentHandle>(flavor->c_str()),
              flavor->c_str(),
              flavor,
              &react::concreteComponentDescriptorConstructor<
                  react::UnimplementedViewComponentDescriptor>});
        });
    auto ownerBox = std::make_shared<react::EventBeat::OwnerBox>();
    auto eventPipe = [uiManager = uiManager_](
                         facebook::jsi::Runtime& runtime,
                         react::EventTarget* eventTarget,
                         const std::string& type,
                         react::ReactEventPriority priority,
                         const react::EventPayload& payload,
                         react::HighResTimeStamp eventTimestamp) {
      uiManager->visitBinding(
          [&](const react::UIManagerBinding& binding) {
            binding.dispatchEvent(
                runtime,
                eventTarget,
                type,
                priority,
                payload,
                eventTimestamp);
          },
          runtime);
    };
    auto statePipe = [uiManager = uiManager_](
                         const react::StateUpdate& stateUpdate) {
      uiManager->updateState(stateUpdate);
    };
    auto eventProcessor = react::EventQueueProcessor(
        std::move(eventPipe),
        [scheduler = runtimeScheduler_](facebook::jsi::Runtime& runtime) {
          if (scheduler) {
            scheduler->callExpiredTasks(runtime);
          }
        },
        statePipe,
        {});
    eventDispatcher_ = std::make_shared<react::EventDispatcher>(
        eventProcessor,
        std::make_unique<HeadlessEventBeat>(ownerBox, *runtimeScheduler_),
        statePipe,
        std::weak_ptr<react::EventLogger>{});
    ownerBox->owner = eventDispatcher_;
    registry_ = providers_.createComponentDescriptorRegistry({
        .eventDispatcher = eventDispatcher_,
        .contextContainer = contextContainer_,
        .flavor = nullptr});
    providers_.add(
        react::concreteComponentDescriptorProvider<
            react::RootComponentDescriptor>());
    providers_.add(
        react::concreteComponentDescriptorProvider<
            react::ViewComponentDescriptor>());
    providers_.add(
        react::concreteComponentDescriptorProvider<
            react::LayoutConformanceComponentDescriptor>());
    providers_.add(
        react::concreteComponentDescriptorProvider<
            react::RawTextComponentDescriptor>());
    providers_.add(
        react::concreteComponentDescriptorProvider<
            react::TextComponentDescriptor>());
    providers_.add(
        react::concreteComponentDescriptorProvider<
            react::ParagraphComponentDescriptor>());
    providers_.add(
        react::concreteComponentDescriptorProvider<
            react::ScrollViewComponentDescriptor>());
    providers_.add(
        react::concreteComponentDescriptorProvider<
            react::ImageComponentDescriptor>());
    react::registerHeadlessOfficialComponents(
        providers_, officialComponentFlavors_);
    if (platform == "ios") {
      providers_.add(
          react::concreteComponentDescriptorProvider<
              react::HeadlessIOSTextInputComponentDescriptor>());
    } else {
      providers_.add(
          react::concreteComponentDescriptorProvider<
              react::HeadlessAndroidTextInputComponentDescriptor>());
    }
    providers_.add(
        react::concreteComponentDescriptorProvider<
            react::HeadlessSampleViewComponentDescriptor>());
    for (const auto& component : addonComponents) {
      if (component.kind !=
          ReactNativeSimulator::AddonComponentKind::DescriptorOnlyMock) {
        continue;
      }
      auto flavor = std::make_shared<std::string>(component.name);
      addonComponentFlavors_.push_back(flavor);
      addonMockComponentNames_.push_back(component.name);
      providers_.add({
          reinterpret_cast<react::ComponentHandle>(flavor->c_str()),
          flavor->c_str(),
          flavor,
          &react::concreteComponentDescriptorConstructor<
              react::UnimplementedViewComponentDescriptor>});
    }
    for (const auto& provider : addonProviders) {
      providers_.add(provider);
    }
    uiManager_->setComponentDescriptorRegistry(registry_);
    uiManager_->setDelegate(this);
    gHeadlessUIManager = uiManager_;
    react::UIManagerBinding::createAndInstallIfNeeded(runtime, uiManager_);
    uiManager_->startEmptySurface(std::make_unique<react::ShadowTree>(
        kReactSurfaceId,
        react::LayoutConstraints{
            .minimumSize = {
                .width = viewportWidth, .height = viewportHeight},
            .maximumSize = {
                .width = viewportWidth, .height = viewportHeight},
            .layoutDirection = headlessI18n().isRTL()
                ? react::LayoutDirection::RightToLeft
                : react::LayoutDirection::LeftToRight},
        react::LayoutContext{
            .pointScaleFactor = pointScaleFactor,
            .swapLeftAndRightInRTL = headlessI18n().doLeftAndRightSwapInRTL,
            .viewportSize = {
                .width = viewportWidth, .height = viewportHeight}},
        *uiManager_,
        *contextContainer_));
    animationContext_ = contextContainer_;
    animationDriver_ = std::make_shared<react::LayoutAnimationDriver>(
        runtimeExecutor_, animationContext_, this);
    uiManager_->setAnimationDelegate(animationDriver_.get());
  }

  ~HeadlessReactFabricHost() override {
    layoutAnimationRunning_ = false;
    for (auto& entry : mountedViews_) {
      unbindMountedImage(entry.second);
    }
    if (gHeadlessUIManager.lock() == uiManager_) {
      gHeadlessUIManager.reset();
    }
    uiManager_->setAnimationDelegate(nullptr);
    uiManager_->setDelegate(nullptr);
    uiManager_->stopSurface(kReactSurfaceId);
  }

  void onAnimationStarted() override {
    layoutAnimationRunning_ = true;
    scheduleLayoutAnimationTick();
  }

  void onAllAnimationsComplete() override {
    layoutAnimationRunning_ = false;
  }

  HeadlessReactFabricResult result() const {
    HeadlessReactFabricResult result;
    result.transactions = transactions_;
    result.creates = creates_;
    result.inserts = inserts_;
    result.updates = updates_;
    result.removes = removes_;
    result.deletes = deletes_;
    std::vector<float> widths;
    widths.reserve(frames_.size());
    for (const auto& [tag, frame] : frames_) {
      if (tag != kReactSurfaceId) {
        widths.push_back(frame.size.width);
      }
    }
    const auto hasWidth = [&widths](float expected) {
      return std::any_of(widths.begin(), widths.end(), [expected](float width) {
        return near(width, expected);
      });
    };
    result.hasExpectedYogaWidths =
        sawInitialYogaWidths_ && sawUpdatedYogaWidths_ && hasWidth(120) &&
        hasWidth(180);
    result.eventDispatcherInstalled = eventDispatcher_ != nullptr;
    result.commitMs = commitMs_;
    result.layoutMs = layoutMs_;
    result.diffMs = diffMs_;
    result.customComponentCreated = customComponentCreated_;
    result.customComponentValue = customComponentValue_;
    result.customComponentLabel = customComponentLabel_;
    result.customCommands = customCommands_;
    result.fallbackComponentNames.assign(
        fallbackComponentNames_.begin(), fallbackComponentNames_.end());
    std::sort(
        result.fallbackComponentNames.begin(),
        result.fallbackComponentNames.end());
    result.mockedComponents = result.fallbackComponentNames.size();
    result.addonMockComponentNames = addonMockComponentNames_;
    result.mockedComponents += result.addonMockComponentNames.size();
    result.traceSpans = traceSpans_;
    result.shadowTreeSurfaceId = shadowTreeSurfaceId_;
    result.shadowTreeRevision = shadowTreeRevision_;
    result.shadowTreeRootTag = shadowTreeRootTag_;
    result.shadowTreeNodes = shadowTreeNodes_;
    result.mountingRevision = lastNonEmptyMountingRevision_;
    result.mountedRootTag = kReactSurfaceId;
    result.mountedViewNodes = lastNonEmptyMountedViewNodes_;
    result.mountingErrors = mountingErrors_;
    // Generic runtime health must not depend on the repository's sample
    // component or fixed Yoga widths. Workload-specific requirements are
    // reported independently and can be enforced by the caller.
    result.passed = transactions_ >= 1 && result.eventDispatcherInstalled &&
        result.mountingErrors.empty();
    if (!result.passed) {
      result.error = !result.mountingErrors.empty()
          ? "Fabric mounting backend rejected one or more mutations"
          : "React reconciler did not produce a Fabric transaction";
    }
    return result;
  }

  void uiManagerDidFinishTransaction(
      std::shared_ptr<const react::MountingCoordinator> coordinator,
      bool) override {
    if (animationDriver_ && !mountingOverrideInstalled_) {
      coordinator->setMountingOverrideDelegate(animationDriver_);
      mountingOverrideInstalled_ = true;
    }
    auto transaction = coordinator->pullTransaction();
    if (!transaction) {
      return;
    }
    captureShadowTreeRevision(
        coordinator->getBaseRevision(), coordinator->getSurfaceId());
    ++transactions_;
    const auto& telemetry = transaction->getTelemetry();
    const auto milliseconds = [](auto start, auto end) {
      return std::chrono::duration<double, std::milli>(end - start).count();
    };
    commitMs_ += milliseconds(
        telemetry.getCommitStartTime(), telemetry.getCommitEndTime());
    layoutMs_ += milliseconds(
        telemetry.getLayoutStartTime(), telemetry.getLayoutEndTime());
    diffMs_ += milliseconds(
        telemetry.getDiffStartTime(), telemetry.getDiffEndTime());
    traceSpans_.push_back({
        "Fabric commit",
        telemetry.getCommitStartTime(),
        telemetry.getCommitEndTime(),
        transactions_});
    traceSpans_.push_back({
        "Fabric layout",
        telemetry.getLayoutStartTime(),
        telemetry.getLayoutEndTime(),
        transactions_});
    traceSpans_.push_back({
        "Fabric diff",
        telemetry.getDiffStartTime(),
        telemetry.getDiffEndTime(),
        transactions_});
    mountingRevision_ = transaction->getNumber();
    std::vector<float> transactionWidths;
    for (const auto& mutation : transaction->getMutations()) {
      applyMountingMutation(mutation);
      const auto& candidate = mutation.type == react::ShadowViewMutation::Delete ||
              mutation.type == react::ShadowViewMutation::Remove
          ? mutation.oldChildShadowView
          : mutation.newChildShadowView;
      if (candidate.componentName != nullptr &&
          std::string(candidate.componentName) ==
              react::HeadlessSampleViewComponentName) {
        if (auto props = std::dynamic_pointer_cast<
                const react::HeadlessSampleViewProps>(candidate.props)) {
          customComponentCreated_ = true;
          customComponentValue_ = props->value;
          customComponentLabel_ = props->label;
        }
      }
      switch (mutation.type) {
        case react::ShadowViewMutation::Create:
          ++creates_;
          frames_[mutation.newChildShadowView.tag] =
              mutation.newChildShadowView.layoutMetrics.frame;
          transactionWidths.push_back(
              mutation.newChildShadowView.layoutMetrics.frame.size.width);
          break;
        case react::ShadowViewMutation::Insert:
          ++inserts_;
          frames_[mutation.newChildShadowView.tag] =
              mutation.newChildShadowView.layoutMetrics.frame;
          transactionWidths.push_back(
              mutation.newChildShadowView.layoutMetrics.frame.size.width);
          break;
        case react::ShadowViewMutation::Update:
          ++updates_;
          frames_[mutation.newChildShadowView.tag] =
              mutation.newChildShadowView.layoutMetrics.frame;
          transactionWidths.push_back(
              mutation.newChildShadowView.layoutMetrics.frame.size.width);
          break;
        case react::ShadowViewMutation::Remove:
          ++removes_;
          break;
        case react::ShadowViewMutation::Delete:
          ++deletes_;
          frames_.erase(mutation.oldChildShadowView.tag);
          animatedDirectProps_.erase(mutation.oldChildShadowView.tag);
          break;
      }
    }
    const auto transactionHasWidth =
        [&transactionWidths](float expected) {
          return std::any_of(
              transactionWidths.begin(),
              transactionWidths.end(),
              [expected](float width) { return near(width, expected); });
        };
    sawInitialYogaWidths_ =
        sawInitialYogaWidths_ ||
        (transactionHasWidth(100) && transactionHasWidth(200));
    sawUpdatedYogaWidths_ =
        sawUpdatedYogaWidths_ ||
        (transactionHasWidth(120) && transactionHasWidth(180));
    validateMountedViewTree();
    snapshotMountedViewTree();
    if (onUpdate_) {
      onUpdate_(result());
    }
    if (animationDriver_ && animationDriver_->shouldAnimateFrame()) {
      scheduleLayoutAnimationTick();
    }
  }

  void uiManagerDidCreateShadowNode(const react::ShadowNode&) override {}
  void uiManagerDidDispatchCommand(
      const std::shared_ptr<const react::ShadowNode>& shadowNode,
      const std::string& commandName,
      const folly::dynamic& args) override {
    if (shadowNode->getComponentName() != nullptr &&
        std::string(shadowNode->getComponentName()) ==
            react::HeadlessSampleViewComponentName &&
        commandName == "setNativeValue" && args.isArray() && !args.empty() &&
        args[0].isNumber()) {
      ++customCommands_;
      const auto value = args[0].asInt();
      shadowNode->getEventEmitter()->dispatchEvent(
          "headlessChange", folly::dynamic::object("value", value));
      return;
    }
    if (commandName == "setNativeValue" && args.isArray() && !args.empty() &&
        (args[0].isBool() || args[0].isNumber())) {
      const auto tag = shadowNode->getTag();
      const auto found = mountedViews_.find(tag);
      if (found != mountedViews_.end() && found->second.node.androidSwitch) {
        found->second.node.androidSwitchOn = args[0].isBool()
            ? args[0].asBool()
            : args[0].asDouble() != 0;
        snapshotMountedViewTree();
        if (onUpdate_) {
          onUpdate_(result());
        }
      }
      return;
    }
    const auto tag = shadowNode->getTag();
    const auto found = mountedViews_.find(tag);
    if (found != mountedViews_.end() && found->second.node.swipeRefresh &&
        commandName == "setNativeRefreshing" && args.isArray() &&
        !args.empty() && (args[0].isBool() || args[0].isNumber())) {
      found->second.node.swipeRefreshing = args[0].isBool()
          ? args[0].asBool()
          : args[0].asDouble() != 0;
      snapshotMountedViewTree();
      if (onUpdate_) {
        onUpdate_(result());
      }
      return;
    }
    if (found != mountedViews_.end() && found->second.node.drawerLayout &&
        (commandName == "openDrawer" || commandName == "closeDrawer")) {
      const bool open = commandName == "openDrawer";
      found->second.node.drawerOffset = open ? 1.0f : 0.0f;
      if (found->second.eventEmitter) {
        found->second.eventEmitter->dispatchEvent(
            open ? "topDrawerOpen" : "topDrawerClose",
            folly::dynamic::object());
      }
      snapshotMountedViewTree();
      if (onUpdate_) {
        onUpdate_(result());
      }
      return;
    }
    if (found == mountedViews_.end() || !found->second.node.textInput) {
      return;
    }
    if (commandName == "focus") {
      focusTextInput(tag);
      snapshotMountedViewTree();
    } else if (commandName == "blur") {
      if (focusedTextInputTag_ == tag) {
        focusTextInput(-1);
        snapshotMountedViewTree();
      }
    } else if (commandName == "setTextAndSelection" && args.isArray() &&
               args.size() >= 4 && args[1].isString() &&
               args[2].isNumber() && args[3].isNumber()) {
      auto& node = found->second.node;
      node.text = args[1].asString();
      const auto length = utf16Length(node.text);
      node.selectionStart = std::min<std::size_t>(args[2].asInt(), length);
      node.selectionEnd = std::min<std::size_t>(args[3].asInt(), length);
      snapshotMountedViewTree();
    }
    if (onUpdate_) {
      onUpdate_(result());
    }
  }
  void uiManagerDidSendAccessibilityEvent(
      const std::shared_ptr<const react::ShadowNode>&,
      const std::string&) override {}
  void uiManagerDidSetIsJSResponder(
      const std::shared_ptr<const react::ShadowNode>&,
      bool,
      bool) override {}
  void uiManagerShouldSynchronouslyUpdateViewOnUIThread(
      react::Tag tag,
      const folly::dynamic& props) override {
    auto found = mountedViews_.find(tag);
    if (found == mountedViews_.end()) {
      return;
    }
    mergeDynamicObjects(animatedDirectProps_[tag], props);
    applyDynamicAnimatedProps(
        found->second.node, animatedDirectProps_[tag], *contextContainer_);
    requestAnimatedSceneFlush();
  }
  void uiManagerDidUpdateShadowTree(
      const std::unordered_map<react::Tag, folly::dynamic>&) override {}
  void uiManagerShouldAddEventListener(
      std::shared_ptr<const react::EventListener> listener) override {
    if (eventDispatcher_ && listener) {
      eventDispatcher_->addListener(std::move(listener));
    }
  }
  void uiManagerShouldRemoveEventListener(
      const std::shared_ptr<const react::EventListener>& listener) override {
    if (eventDispatcher_ && listener) {
      eventDispatcher_->removeListener(listener);
    }
  }
  void uiManagerDidStartSurface(const react::ShadowTree& shadowTree) override {
    for (auto& callback : callbacks_) {
      callback(shadowTree);
    }
  }
  void uiManagerDidFinishReactCommit(
      const react::ShadowTree& shadowTree) override {
    const auto revision = shadowTree.getCurrentReactRevision();
    if (revision) {
      captureShadowTreeRevision(*revision, shadowTree.getSurfaceId());
    }
  }
  void uiManagerDidPromoteReactRevision(const react::ShadowTree&) override {}
  void uiManagerShouldAddOnSurfaceStartCallback(
      OnSurfaceStartCallback&& callback) override {
    callbacks_.push_back(std::move(callback));
  }
  void uiManagerDidCaptureViewSnapshot(react::Tag, react::SurfaceId) override {}
  void uiManagerDidSetViewSnapshot(
      react::Tag,
      react::Tag,
      react::SurfaceId) override {}
  void uiManagerDidClearPendingSnapshots() override {}

  ReactNativeSimulator::InteractionResult dispatchInteraction(
      const ReactNativeSimulator::InteractionAction& action,
      std::uint64_t sequence) {
    ReactNativeSimulator::InteractionResult result{
        .sequence = sequence,
        .sceneRevision = lastNonEmptyMountingRevision_,
    };
    if (action.type ==
        ReactNativeSimulator::InteractionActionType::HardwareBackPress) {
      if (!headlessBackPress().press()) {
        result.error = "DeviceEventManager is unavailable";
      }
      return result;
    }

    ReactNativeSimulator::SceneSnapshot scene;
    scene.rootTag = kReactSurfaceId;
    scene.nodes = lastNonEmptyMountedViewNodes_;
    const auto hit = ReactNativeSimulator::hitTestScene(scene, action.x, action.y);

    if (action.type == ReactNativeSimulator::InteractionActionType::Scroll) {
      const auto scrollTag = findScrollableAtPoint(
          scene, hit ? std::optional<int>(hit->tag) : std::nullopt,
          action.x, action.y);
      if (scrollTag == 0) {
        return result;
      }
      applyScrollDelta(scrollTag, action.deltaX, action.deltaY);
      result.targetTag = scrollTag;
      result.sceneRevision = lastNonEmptyMountingRevision_;
      return result;
    }

    if (action.type == ReactNativeSimulator::InteractionActionType::TextInput ||
        action.type == ReactNativeSimulator::InteractionActionType::KeyDown) {
      if (!focusedTextInputTag_) {
        result.error = "no TextInput is focused";
        return result;
      }
      auto input = mountedViews_.find(*focusedTextInputTag_);
      if (input == mountedViews_.end() || !input->second.node.textInput ||
          !input->second.node.editable) {
        result.error = "focused TextInput is unavailable or read-only";
        return result;
      }
      auto& record = input->second;
      auto& node = record.node;
      const auto selectionStart = std::min(
          node.selectionStart, utf16Length(node.text));
      const auto selectionEnd = std::min(
          node.selectionEnd, utf16Length(node.text));
      const auto lower = std::min(selectionStart, selectionEnd);
      const auto upper = std::max(selectionStart, selectionEnd);
      std::string replacement;
      std::size_t replaceStart = lower;
      if (action.type == ReactNativeSimulator::InteractionActionType::TextInput) {
        replacement = action.text;
      } else if (action.key == "Backspace") {
        if (lower == upper) {
          replaceStart = previousGraphemeOffset(node.text, lower);
        }
      } else if (action.key == "Enter") {
        if (!node.multiline) {
          emitTextInputEvent(record, "submitEditing");
          result.targetTag = input->first;
          return result;
        }
        replacement = "\n";
      } else if (action.key.size() == 1) {
        replacement = action.key;
      } else {
        emitTextInputEvent(record, "keyPress", action.key);
        result.targetTag = input->first;
        return result;
      }
      const auto firstByte = byteOffsetForUtf16(node.text, replaceStart);
      const auto lastByte = byteOffsetForUtf16(node.text, upper);
      node.text.replace(firstByte, lastByte - firstByte, replacement);
      node.selectionStart = replaceStart + utf16Length(replacement);
      node.selectionEnd = node.selectionStart;
      ++record.eventCount;
      if (action.type == ReactNativeSimulator::InteractionActionType::KeyDown) {
        emitTextInputEvent(record, "keyPress", action.key);
      }
      emitTextInputEvent(record, "change");
      emitTextInputEvent(record, "selectionChange");
      snapshotMountedViewTree();
      if (onUpdate_) {
        onUpdate_(this->result());
      }
      result.targetTag = input->first;
      result.sceneRevision = lastNonEmptyMountingRevision_;
      return result;
    }

    std::optional<int> targetTag;
    if (action.type == ReactNativeSimulator::InteractionActionType::PointerDown) {
      if (hit) {
        ActivePointer pointer;
        pointer.targetTag = hit->tag;
        pointer.scrollTag = findScrollableAtPoint(
            scene, hit->tag, action.x, action.y);
        pointer.downX = action.x;
        pointer.downY = action.y;
        pointer.lastX = action.x;
        pointer.lastY = action.y;
        auto drawer = mountedViews_.find(hit->tag);
        while (drawer != mountedViews_.end()) {
          if (drawer->second.node.drawerLayout &&
              !drawer->second.node.drawerLocked) {
            const auto& node = drawer->second.node;
            const bool nearEdge = node.drawerFromLeft
                ? action.x < node.absoluteX + 24.0f || node.drawerOffset > 0.05f
                : action.x > node.absoluteX + node.width - 24.0f ||
                    node.drawerOffset > 0.05f;
            if (nearEdge) {
              pointer.drawerTag = drawer->first;
              pointer.drawerStartOffset = node.drawerOffset;
            }
            break;
          }
          if (!drawer->second.node.parentTag) {
            break;
          }
          drawer = mountedViews_.find(*drawer->second.node.parentTag);
        }
        activePointers_[action.pointerId] = pointer;
        targetTag = hit->tag;
        focusTextInput(hit->tag);
        setNativeRipplePressed(hit->tag);
      }
    } else {
      const auto active = activePointers_.find(action.pointerId);
      if (active != activePointers_.end()) {
        targetTag = active->second.targetTag;
      } else if (
          action.type ==
              ReactNativeSimulator::InteractionActionType::PointerMove ||
          action.type ==
              ReactNativeSimulator::InteractionActionType::PointerUp ||
          action.type ==
              ReactNativeSimulator::InteractionActionType::PointerCancel) {
        return result;
      }
    }
    if (!targetTag) {
      result.error = "pointer action has no target";
      return result;
    }
    auto activePointer = activePointers_.find(action.pointerId);
    if (activePointer != activePointers_.end() &&
        activePointer->second.drawerTag != 0 &&
        (action.type ==
             ReactNativeSimulator::InteractionActionType::PointerMove ||
         action.type ==
             ReactNativeSimulator::InteractionActionType::PointerUp ||
         action.type ==
             ReactNativeSimulator::InteractionActionType::PointerCancel)) {
      auto drawer = mountedViews_.find(activePointer->second.drawerTag);
      if (drawer != mountedViews_.end() && drawer->second.node.drawerLayout) {
        auto& node = drawer->second.node;
        const auto width =
            node.drawerWidth > 0 ? node.drawerWidth : node.width * 0.8f;
        const auto delta = node.drawerFromLeft
            ? action.x - activePointer->second.downX
            : activePointer->second.downX - action.x;
        auto offset = activePointer->second.drawerStartOffset +
            (width > 0 ? delta / width : 0);
        offset = std::clamp(offset, 0.0f, 1.0f);
        if (action.type ==
            ReactNativeSimulator::InteractionActionType::PointerMove) {
          node.drawerOffset = offset;
          if (drawer->second.eventEmitter) {
            drawer->second.eventEmitter->dispatchEvent(
                "topDrawerSlide",
                folly::dynamic::object("offset", offset));
          }
        } else {
          const bool open = offset >= 0.5f;
          node.drawerOffset = open ? 1.0f : 0.0f;
          if (drawer->second.eventEmitter) {
            drawer->second.eventEmitter->dispatchEvent(
                open ? "topDrawerOpen" : "topDrawerClose",
                folly::dynamic::object());
          }
          activePointers_.erase(activePointer);
        }
        snapshotMountedViewTree();
        if (onUpdate_) {
          onUpdate_(this->result());
        }
        result.targetTag = drawer->first;
        result.sceneRevision = lastNonEmptyMountingRevision_;
        return result;
      }
    }
    if (activePointer != activePointers_.end() &&
        action.type ==
            ReactNativeSimulator::InteractionActionType::PointerMove &&
        activePointer->second.scrollTag != 0) {
      auto& pointer = activePointer->second;
      const float draggedX = action.x - pointer.downX;
      const float draggedY = action.y - pointer.downY;
      if (!pointer.scrolling &&
          draggedX * draggedX + draggedY * draggedY >=
              kPointerScrollSlop * kPointerScrollSlop) {
        pointer.scrolling = true;
        emitScrollBeginDrag(pointer.scrollTag);
        auto original = mountedViews_.find(pointer.targetTag);
        if (original != mountedViews_.end()) {
          if (auto touchEmitter = std::dynamic_pointer_cast<
                  react::TouchEventEmitter>(original->second.eventEmitter)) {
            react::TouchEvent cancelEvent;
            react::Touch cancelTouch;
            cancelTouch.pagePoint = {.x = action.x, .y = action.y};
            cancelTouch.identifier = action.pointerId;
            cancelTouch.target = pointer.targetTag;
            cancelTouch.timeStamp = react::HighResTimeStamp::now();
            cancelEvent.changedTouches.insert(cancelTouch);
            touchEmitter->onTouchCancel(cancelEvent);
            react::PointerEvent cancelPointer;
            cancelPointer.pointerId = action.pointerId;
            cancelPointer.pointerType = "mouse";
            cancelPointer.clientPoint = {.x = action.x, .y = action.y};
            cancelPointer.timeStamp = cancelTouch.timeStamp;
            touchEmitter->onPointerCancel(cancelPointer);
          }
        }
      }
      if (pointer.scrolling) {
        applyScrollDelta(
            pointer.scrollTag,
            pointer.lastX - action.x,
            pointer.lastY - action.y);
        const auto scroll = mountedViews_.find(pointer.scrollTag);
        if (scroll != mountedViews_.end() &&
            scroll->second.node.scrollOffsetY <= 0 &&
            action.y - pointer.downY > 64.0f) {
          auto ancestor = scroll;
          while (ancestor != mountedViews_.end() &&
                 ancestor->second.node.parentTag) {
            ancestor = mountedViews_.find(*ancestor->second.node.parentTag);
            if (ancestor != mountedViews_.end() &&
                ancestor->second.node.swipeRefresh &&
                ancestor->second.node.swipeRefreshEnabled &&
                !ancestor->second.node.swipeRefreshing) {
              ancestor->second.node.swipeRefreshing = true;
              if (ancestor->second.eventEmitter) {
                ancestor->second.eventEmitter->dispatchEvent(
                    "topRefresh", folly::dynamic::object());
              }
              snapshotMountedViewTree();
              if (onUpdate_) {
                onUpdate_(this->result());
              }
              break;
            }
          }
        }
        pointer.lastX = action.x;
        pointer.lastY = action.y;
        result.targetTag = pointer.scrollTag;
        result.sceneRevision = lastNonEmptyMountingRevision_;
        return result;
      }
      pointer.lastX = action.x;
      pointer.lastY = action.y;
    }
    if (activePointer != activePointers_.end() &&
        activePointer->second.scrolling &&
        (action.type ==
             ReactNativeSimulator::InteractionActionType::PointerUp ||
         action.type ==
             ReactNativeSimulator::InteractionActionType::PointerCancel)) {
      emitScrollEndDrag(activePointer->second.scrollTag);
      result.targetTag = activePointer->second.scrollTag;
      result.sceneRevision = lastNonEmptyMountingRevision_;
      activePointers_.erase(activePointer);
      return result;
    }
    const auto dispatchTarget = findPointerDispatchTarget(*targetTag);
    if (!dispatchTarget.emitter) {
      if (action.type ==
              ReactNativeSimulator::InteractionActionType::PointerUp ||
          action.type ==
              ReactNativeSimulator::InteractionActionType::PointerCancel) {
        activePointers_.erase(action.pointerId);
      }
      result.targetTag = *targetTag;
      return result;
    }
    const auto dispatchTag = dispatchTarget.tag;
    const auto targetNode = std::find_if(
        scene.nodes.begin(), scene.nodes.end(),
        [dispatchTag](const auto& node) { return node.tag == dispatchTag; });
    float presentationX = targetNode == scene.nodes.end()
        ? 0.0f : targetNode->absoluteX;
    float presentationY = targetNode == scene.nodes.end()
        ? 0.0f : targetNode->absoluteY;
    if (targetNode != scene.nodes.end()) {
      const auto* current = &*targetNode;
      while (current->parentTag) {
        const auto parent = std::find_if(
            scene.nodes.begin(), scene.nodes.end(),
            [current](const auto& node) {
              return node.tag == *current->parentTag;
            });
        if (parent == scene.nodes.end()) {
          break;
        }
        if (parent->scrollable) {
          presentationX -= parent->scrollOffsetX;
          presentationY -= parent->scrollOffsetY;
        }
        current = &*parent;
      }
    }
    const float localX = action.x - presentationX;
    const float localY = action.y - presentationY;
    auto touchEmitter = dispatchTarget.emitter;
    const auto timestamp = react::HighResTimeStamp::now();
    react::Touch touch;
    touch.pagePoint = {.x = action.x, .y = action.y};
    touch.offsetPoint = {.x = localX, .y = localY};
    touch.screenPoint = touch.pagePoint;
    touch.identifier = action.pointerId;
    touch.target = dispatchTag;
    touch.force = action.type ==
            ReactNativeSimulator::InteractionActionType::PointerUp
        ? 0.0f
        : 1.0f;
    touch.timeStamp = timestamp;
    react::TouchEvent touchEvent;
    touchEvent.changedTouches.insert(touch);
    if (action.type != ReactNativeSimulator::InteractionActionType::PointerUp &&
        action.type != ReactNativeSimulator::InteractionActionType::PointerCancel) {
      touchEvent.touches.insert(touch);
      touchEvent.targetTouches.insert(touch);
    }
    react::PointerEvent pointerEvent;
    pointerEvent.pointerId = action.pointerId;
    pointerEvent.pressure = touch.force;
    pointerEvent.pointerType = "mouse";
    pointerEvent.clientPoint = {.x = action.x, .y = action.y};
    pointerEvent.screenPoint = {.x = action.x, .y = action.y};
    pointerEvent.offsetPoint = {.x = localX, .y = localY};
    pointerEvent.width = 1;
    pointerEvent.height = 1;
    pointerEvent.tiltX = 0;
    pointerEvent.tiltY = 0;
    pointerEvent.detail = 0;
    pointerEvent.buttons = action.buttons;
    pointerEvent.tangentialPressure = 0;
    pointerEvent.twist = 0;
    pointerEvent.ctrlKey = action.ctrlKey;
    pointerEvent.shiftKey = action.shiftKey;
    pointerEvent.altKey = action.altKey;
    pointerEvent.metaKey = action.metaKey;
    pointerEvent.isPrimary = action.pointerId == 1;
    pointerEvent.button = action.button;
    pointerEvent.timeStamp = timestamp;
    switch (action.type) {
      case ReactNativeSimulator::InteractionActionType::PointerDown:
        touchEmitter->onTouchStart(touchEvent);
        touchEmitter->onPointerDown(pointerEvent);
        break;
      case ReactNativeSimulator::InteractionActionType::PointerMove:
        touchEmitter->onTouchMove(touchEvent);
        touchEmitter->onPointerMove(pointerEvent);
        break;
      case ReactNativeSimulator::InteractionActionType::PointerUp:
        touchEmitter->onTouchEnd(touchEvent);
        touchEmitter->onPointerUp(pointerEvent);
        clearNativeRipplePressed();
        if (hit && hit->tag == *targetTag && action.button == 0) {
          touchEmitter->onClick(pointerEvent);
          const auto found = mountedViews_.find(*targetTag);
          if (found != mountedViews_.end() && found->second.eventEmitter) {
            if (found->second.node.androidSwitch &&
                found->second.node.androidSwitchEnabled) {
              const bool next = !found->second.node.androidSwitchOn;
              found->second.node.androidSwitchOn = next;
              found->second.eventEmitter->dispatchEvent(
                  "topChange",
                  folly::dynamic::object("value", next)(
                      "target", *targetTag));
              snapshotMountedViewTree();
              if (onUpdate_) {
                onUpdate_(this->result());
              }
            } else if (found->second.node.modalHost) {
              found->second.eventEmitter->dispatchEvent(
                  "topRequestClose", folly::dynamic::object());
            }
          }
        }
        activePointers_.erase(action.pointerId);
        break;
      case ReactNativeSimulator::InteractionActionType::PointerCancel:
        touchEmitter->onTouchCancel(touchEvent);
        touchEmitter->onPointerCancel(pointerEvent);
        clearNativeRipplePressed();
        activePointers_.erase(action.pointerId);
        break;
      default:
        break;
    }
    result.targetTag = *targetTag;
    return result;
  }

 private:
  int findScrollableTag(int tag) const {
    auto found = mountedViews_.find(tag);
    while (found != mountedViews_.end() &&
           (!found->second.node.scrollable ||
            found->second.node.componentName ==
                "AndroidHorizontalScrollContentView") &&
           found->second.node.parentTag) {
      found = mountedViews_.find(*found->second.node.parentTag);
    }
    if (found != mountedViews_.end() && found->second.node.scrollable &&
        found->second.node.componentName !=
            "AndroidHorizontalScrollContentView") {
      return found->first;
    }
    return 0;
  }

  int findScrollableAtPoint(
      const ReactNativeSimulator::SceneSnapshot& scene,
      std::optional<int> hitTag,
      float x,
      float y) const {
    if (hitTag) {
      if (const auto fromHit = findScrollableTag(*hitTag)) {
        return fromHit;
      }
    }
    int best = 0;
    std::size_t bestDepth = 0;
    std::unordered_map<int, const ReactNativeSimulator::SceneNode*> byTag;
    byTag.reserve(scene.nodes.size());
    for (const auto& node : scene.nodes) {
      byTag.emplace(node.tag, &node);
    }
    for (const auto& node : scene.nodes) {
      if (!node.scrollable || !node.layoutable || node.display == "none" ||
          node.width <= 0 || node.height <= 0 ||
          node.componentName == "AndroidHorizontalScrollContentView") {
        continue;
      }
      float originX = node.absoluteX;
      float originY = node.absoluteY;
      const auto* current = &node;
      while (current->parentTag) {
        const auto parent = byTag.find(*current->parentTag);
        if (parent == byTag.end()) {
          break;
        }
        if (parent->second->scrollable) {
          originX -= parent->second->scrollOffsetX;
          originY -= parent->second->scrollOffsetY;
        }
        current = parent->second;
      }
      if (x < originX || y < originY || x > originX + node.width ||
          y > originY + node.height) {
        continue;
      }
      if (node.depth >= bestDepth) {
        best = node.tag;
        bestDepth = node.depth;
      }
    }
    return best;
  }

  struct PointerDispatchTarget {
    int tag{0};
    std::shared_ptr<react::TouchEventEmitter> emitter;
  };

  PointerDispatchTarget findPointerDispatchTarget(int tag) const {
    auto found = mountedViews_.find(tag);
    while (found != mountedViews_.end()) {
      if (auto touch = std::dynamic_pointer_cast<react::TouchEventEmitter>(
              found->second.eventEmitter)) {
        return {.tag = found->first, .emitter = std::move(touch)};
      }
      if (!found->second.node.parentTag) {
        break;
      }
      found = mountedViews_.find(*found->second.node.parentTag);
    }
    return {};
  }

  react::ScrollEvent makeScrollEvent(const MountedViewRecord& record) const {
    react::ScrollEvent event;
    event.contentOffset = {
        record.node.scrollOffsetX, record.node.scrollOffsetY};
    event.contentSize = {
        record.node.scrollContentWidth, record.node.scrollContentHeight};
    event.contentInset = {
        .left = record.node.contentInsetLeft,
        .top = record.node.contentInsetTop,
        .right = record.node.contentInsetRight,
        .bottom = record.node.contentInsetBottom};
    event.containerSize = {record.node.width, record.node.height};
    event.zoomScale = 1;
    event.timestamp = std::chrono::duration<react::Float>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return event;
  }

  std::shared_ptr<react::ScrollViewEventEmitter> scrollEmitter(int tag) const {
    const auto found = mountedViews_.find(tag);
    if (found == mountedViews_.end()) {
      return nullptr;
    }
    return std::dynamic_pointer_cast<react::ScrollViewEventEmitter>(
        found->second.eventEmitter);
  }

  void emitScrollBeginDrag(int tag) {
    if (const auto emitter = scrollEmitter(tag)) {
      emitter->onScrollBeginDrag(makeScrollEvent(mountedViews_.at(tag)));
    }
  }

  void emitScrollEndDrag(int tag) {
    const auto found = mountedViews_.find(tag);
    if (found == mountedViews_.end()) {
      return;
    }
    if (const auto emitter = scrollEmitter(tag)) {
      react::ScrollEndDragEvent event(makeScrollEvent(found->second));
      event.targetContentOffset = event.contentOffset;
      event.velocity = {0, 0};
      emitter->onScrollEndDrag(event);
    }
  }

  void applyInlineAttachmentFrames(MountedViewRecord& record) {
#if RNS_ENABLE_SKIA
    if (!record.node.preparedText) {
      return;
    }
    const auto& attachments = record.node.preparedText->attachments();
    if (attachments.empty()) {
      return;
    }
    std::unordered_map<int, std::optional<int>> shadowParents;
    shadowParents.reserve(shadowTreeNodes_.size());
    for (const auto& shadowNode : shadowTreeNodes_) {
      shadowParents.emplace(shadowNode.tag, shadowNode.parentTag);
    }
    const auto isShadowDescendant = [&](int tag, int ancestorTag) {
      auto parent = shadowParents.find(tag);
      while (parent != shadowParents.end() && parent->second) {
        if (*parent->second == ancestorTag) {
          return true;
        }
        parent = shadowParents.find(*parent->second);
      }
      return false;
    };
    std::unordered_set<int> mountedChildren(
        record.children.begin(), record.children.end());
    const auto hasMountedChildAncestor = [&](int tag) {
      auto parent = shadowParents.find(tag);
      while (parent != shadowParents.end() && parent->second) {
        if (mountedChildren.contains(*parent->second)) {
          return true;
        }
        parent = shadowParents.find(*parent->second);
      }
      return false;
    };
    std::size_t attachmentIndex = 0;
    std::vector<int> attachmentHosts;
    for (const auto childTag : record.children) {
      const auto child = mountedViews_.find(childTag);
      if (child == mountedViews_.end()) {
        continue;
      }
      auto& node = child->second.node;
      // Fabric mounts inline Image/View hosts as Paragraph children. Nested
      // Text/RawText stay virtual and never receive an attachment frame.
      if (node.componentName == "Paragraph" ||
          node.componentName == "Text" ||
          node.componentName == "RawText" ||
          !node.layoutable || hasMountedChildAncestor(childTag)) {
        continue;
      }
      if (attachmentIndex >= attachments.size()) {
        break;
      }
      const auto& attachment = attachments[attachmentIndex++];
      if (attachment.clipped) {
        node.display = "none";
        node.width = 0;
        node.height = 0;
        continue;
      }
      node.x = attachment.x + record.node.contentInsetLeft;
      // getRectsForPlaceholders already returns the baseline-aligned top of
      // the replacement span. Reconstructing it from aggregate line metrics
      // selected the preceding text line for a wrapped tall attachment and
      // moved the host to y=0. That made inline-clipped cover its prefix and
      // shortened the visible nested-relayout host. Preserve the paragraph's
      // actual placeholder frame; an EXACT-height Text may still let the
      // native host overflow, matching Android TextInlineViewPlaceholderSpan.
      node.y = attachment.y + record.node.contentInsetTop;
      node.width = attachment.width;
      node.height = attachment.height;
      node.display = "flex";
      node.inlineAttachment = true;
      attachmentHosts.push_back(childTag);
    }
    // Fabric flattens virtual Text descendants into Paragraph children. A
    // nested View can therefore have the Paragraph as its mounted parent even
    // though the ShadowTree still places it inside the ReplacementSpan host.
    // Mark the complete source subtree so Paragraph overflow:hidden does not
    // clip only the inner rectangle/image at the Text edge.
    for (const auto childTag : record.children) {
      const auto child = mountedViews_.find(childTag);
      if (child == mountedViews_.end()) {
        continue;
      }
      for (const auto hostTag : attachmentHosts) {
        if (childTag != hostTag &&
            isShadowDescendant(childTag, hostTag)) {
          child->second.node.inlineAttachment = true;
          break;
        }
      }
    }
#else
    (void)record;
#endif
  }

  void expandScrollContentFromChildren(MountedViewRecord& record) {
    if (!record.node.scrollable) {
      return;
    }
    for (const auto childTag : record.children) {
      const auto child = mountedViews_.find(childTag);
      if (child == mountedViews_.end()) {
        continue;
      }
      record.node.scrollContentWidth = std::max(
          record.node.scrollContentWidth,
          child->second.node.x + child->second.node.width);
      record.node.scrollContentHeight = std::max(
          record.node.scrollContentHeight,
          child->second.node.y + child->second.node.height);
    }
  }

  bool applyScrollDelta(int tag, float deltaX, float deltaY) {
    auto found = mountedViews_.find(tag);
    while (found != mountedViews_.end()) {
      if (found->second.node.scrollable &&
          found->second.node.componentName !=
              "AndroidHorizontalScrollContentView") {
        expandScrollContentFromChildren(found->second);
        auto& node = found->second.node;
        const float previousX = node.scrollOffsetX;
        const float previousY = node.scrollOffsetY;
        node.scrollOffsetX += deltaX;
        node.scrollOffsetY += deltaY;
        clampScrollOffset(node, usesIosScrollContentInset(platform_));
        if (previousX != node.scrollOffsetX ||
            previousY != node.scrollOffsetY) {
          const int scrolledTag = found->first;
          const react::Point offset{node.scrollOffsetX, node.scrollOffsetY};
          if (const auto scrollState = std::dynamic_pointer_cast<const
                  react::ConcreteState<react::ScrollViewState>>(
                  found->second.state)) {
            scrollState->updateState(
                [offset](const react::ScrollViewState& previous)
                    -> react::StateData::Shared {
                  auto next = previous;
                  next.contentOffset = offset;
                  return std::make_shared<const react::ScrollViewState>(
                      std::move(next));
                });
          }
          if (const auto emitter = scrollEmitter(scrolledTag)) {
            emitter->onScroll(makeScrollEvent(found->second));
          }
          snapshotMountedViewTree();
          if (onUpdate_) {
            onUpdate_(this->result());
          }
          return true;
        }
      }
      if (!found->second.node.parentTag) {
        break;
      }
      found = mountedViews_.find(*found->second.node.parentTag);
    }
    return false;
  }

  void emitTextInputEvent(
      MountedViewRecord& record,
      const std::string& name,
      const std::string& key = {}) {
    if (!record.eventEmitter) {
      return;
    }
    if (name == "keyPress") {
      record.eventEmitter->dispatchEvent(
          name,
          folly::dynamic::object
              ("key", key.empty() ? "Backspace" : key)
              ("eventCount", record.eventCount));
      return;
    }
    record.eventEmitter->dispatchEvent(
        name,
        folly::dynamic::object
            ("text", record.node.text)
            ("target", record.node.tag)
            ("eventCount", record.eventCount)
            ("selection", folly::dynamic::object
                ("start", record.node.selectionStart)
                ("end", record.node.selectionEnd)));
  }

  void focusTextInput(int tag) {
    const auto candidate = mountedViews_.find(tag);
    const std::optional<int> next = candidate != mountedViews_.end() &&
            candidate->second.node.textInput && candidate->second.node.editable
        ? std::optional<int>(tag)
        : std::nullopt;
    if (next == focusedTextInputTag_) {
      return;
    }
    if (focusedTextInputTag_) {
      const auto previous = mountedViews_.find(*focusedTextInputTag_);
      if (previous != mountedViews_.end()) {
        previous->second.node.focused = false;
        emitTextInputEvent(previous->second, "blur");
      }
    }
    focusedTextInputTag_ = next;
    if (focusedTextInputTag_) {
      auto& record = mountedViews_.at(*focusedTextInputTag_);
      record.node.focused = true;
      if (record.node.selectionStart > utf16Length(record.node.text)) {
        record.node.selectionStart = utf16Length(record.node.text);
        record.node.selectionEnd = record.node.selectionStart;
      }
      emitTextInputEvent(record, "focus");
      headlessKeyboard().setVisible(true);
    } else {
      headlessKeyboard().setVisible(false);
    }
  }

  void captureShadowTreeRevision(
      const react::ShadowTreeRevision& revision,
      react::SurfaceId surfaceId) {
    if (!revision.rootShadowNode ||
        revision.rootShadowNode->getChildren().empty()) {
      // React unmounts the surface before metrics are emitted. Keep the last
      // non-empty revision so diagnostics describe the rendered tree.
      return;
    }
    std::vector<ReactNativeSimulator::SceneNode> nodes;
    const auto& root = revision.rootShadowNode;
    const auto capture = [&](const auto& self,
                             const react::ShadowNode& shadowNode,
                             std::optional<int> parentTag,
                             std::size_t childIndex,
                             std::size_t depth,
                             float parentAbsoluteX,
                             float parentAbsoluteY) -> void {
      ReactNativeSimulator::SceneNode node;
      node.tag = shadowNode.getTag();
      node.parentTag = parentTag;
      node.childIndex = childIndex;
      node.depth = depth;
      node.componentName = shadowNode.getComponentName() == nullptr
          ? "Unknown"
          : shadowNode.getComponentName();
      const auto& props = shadowNode.getProps();
      if (props) {
        node.nativeId = props->nativeId;
      }

      if (const auto* layoutable =
              dynamic_cast<const react::LayoutableShadowNode*>(&shadowNode)) {
        const auto layout = layoutable->getLayoutMetrics();
        node.layoutable = true;
        node.x = layout.frame.origin.x;
        node.y = layout.frame.origin.y;
        node.width = layout.frame.size.width;
        node.height = layout.frame.size.height;
        node.absoluteX = parentAbsoluteX + node.x;
        node.absoluteY = parentAbsoluteY + node.y;
        node.contentInsetTop = layout.contentInsets.top;
        node.contentInsetRight = layout.contentInsets.right;
        node.contentInsetBottom = layout.contentInsets.bottom;
        node.contentInsetLeft = layout.contentInsets.left;
        node.borderTop = layout.borderWidth.top;
        node.borderRight = layout.borderWidth.right;
        node.borderBottom = layout.borderWidth.bottom;
        node.borderLeft = layout.borderWidth.left;
        node.display = displayName(layout.displayType);
        node.position = positionName(layout.positionType);
      } else {
        node.absoluteX = parentAbsoluteX;
        node.absoluteY = parentAbsoluteY;
      }

      if (const auto viewProps =
              std::dynamic_pointer_cast<const react::BaseViewProps>(props)) {
        node.opacity = viewProps->opacity;
        node.zIndex = viewProps->zIndex;
        node.pointerEvents = pointerEventsName(viewProps->pointerEvents);
        node.clipsContentToBounds = viewProps->getClipsContentToBounds();
        node.hitSlopTop = viewProps->hitSlop.top;
        node.hitSlopRight = viewProps->hitSlop.right;
        node.hitSlopBottom = viewProps->hitSlop.bottom;
        node.hitSlopLeft = viewProps->hitSlop.left;
        node.collapsable = viewProps->collapsable;
        if (viewProps->backgroundColor) {
          const auto color =
              react::colorComponentsFromColor(viewProps->backgroundColor);
          node.hasBackgroundColor = true;
          node.backgroundRed = color.red;
          node.backgroundGreen = color.green;
          node.backgroundBlue = color.blue;
          node.backgroundAlpha = color.alpha;
        }
        if (const auto* layoutable =
                dynamic_cast<const react::LayoutableShadowNode*>(&shadowNode)) {
          applyResolvedBorder(
              node, *viewProps, layoutable->getLayoutMetrics());
        }
        applyBoxShadow(node, *viewProps);
        applyOutline(node, *viewProps);
        applyViewTransform(node, *viewProps);
        applyViewEffects(node, *viewProps);
      }
      if (const auto inputProps = std::dynamic_pointer_cast<const
              react::BaseTextInputProps>(props)) {
        node.textInput = true;
        node.editable = inputProps->editable && !inputProps->readOnly;
        node.multiline = inputProps->multiline;
        node.placeholder = inputProps->placeholder;
        node.text = inputProps->text.empty()
            ? inputProps->defaultValue
            : inputProps->text;
        node.selectionStart = utf16Length(node.text);
        node.selectionEnd = node.selectionStart;
        applyTextInputColors(node, *inputProps);
      }
      if (const auto iosInputProps = std::dynamic_pointer_cast<const
              react::TextInputProps>(props);
          iosInputProps && iosInputProps->selection) {
        const auto length = utf16Length(node.text);
        node.selectionStart = std::min<std::size_t>(
            std::max(iosInputProps->selection->start, 0), length);
        node.selectionEnd = std::min<std::size_t>(
            std::max(iosInputProps->selection->end, 0), length);
      }
      if (const auto sampleProps = std::dynamic_pointer_cast<
              const react::HeadlessSampleViewProps>(props)) {
        node.customValue = sampleProps->value;
        node.customLabel = sampleProps->label;
      }
      applyImageSource(node, props, assetDirectory_);
      applyActivityIndicator(node, props);
      applyAndroidSwitch(node, props);
      applyModalHost(node, props);
      placeModalDialogWindow(node, viewportWidth_, viewportHeight_);
      applySwipeRefresh(node, props);
      applyDrawerLayout(node, props);

      const auto absoluteX = node.absoluteX;
      const auto absoluteY = node.absoluteY;
      nodes.push_back(std::move(node));
      const auto& children = shadowNode.getChildren();
      for (std::size_t index = 0; index < children.size(); ++index) {
        self(
            self,
            *children[index],
            shadowNode.getTag(),
            index,
            depth + 1,
            absoluteX,
            absoluteY);
      }
    };
    capture(capture, *root, std::nullopt, 0, 0, 0, 0);

    shadowTreeSurfaceId_ = surfaceId;
    shadowTreeRevision_ = revision.number;
    shadowTreeRootTag_ = root->getTag();
    shadowTreeNodes_ = std::move(nodes);
  }
  ReactNativeSimulator::SceneNode mountedNodeFromShadowView(
      const react::ShadowView& shadowView) const {
    ReactNativeSimulator::SceneNode node;
    node.tag = shadowView.tag;
    node.componentName = shadowView.componentName == nullptr
        ? "Unknown"
        : shadowView.componentName;
    node.layoutable = shadowView.layoutMetrics != react::EmptyLayoutMetrics;
    const auto& layout = shadowView.layoutMetrics;
    node.x = layout.frame.origin.x;
    node.y = layout.frame.origin.y;
    node.width = layout.frame.size.width;
    node.height = layout.frame.size.height;
    node.contentInsetTop = layout.contentInsets.top;
    node.contentInsetRight = layout.contentInsets.right;
    node.contentInsetBottom = layout.contentInsets.bottom;
    node.contentInsetLeft = layout.contentInsets.left;
    node.borderTop = layout.borderWidth.top;
    node.borderRight = layout.borderWidth.right;
    node.borderBottom = layout.borderWidth.bottom;
    node.borderLeft = layout.borderWidth.left;
    node.display = displayName(layout.displayType);
    node.position = positionName(layout.positionType);
    if (shadowView.props) {
      node.nativeId = shadowView.props->nativeId;
    }
    if (const auto viewProps = std::dynamic_pointer_cast<
            const react::BaseViewProps>(shadowView.props)) {
      node.opacity = viewProps->opacity;
      node.zIndex = viewProps->zIndex;
      node.pointerEvents = pointerEventsName(viewProps->pointerEvents);
      node.clipsContentToBounds = viewProps->getClipsContentToBounds();
      node.hitSlopTop = viewProps->hitSlop.top;
      node.hitSlopRight = viewProps->hitSlop.right;
      node.hitSlopBottom = viewProps->hitSlop.bottom;
      node.hitSlopLeft = viewProps->hitSlop.left;
      node.collapsable = viewProps->collapsable;
      if (viewProps->backgroundColor) {
        const auto color =
            react::colorComponentsFromColor(viewProps->backgroundColor);
        node.hasBackgroundColor = true;
        node.backgroundRed = color.red;
        node.backgroundGreen = color.green;
        node.backgroundBlue = color.blue;
        node.backgroundAlpha = color.alpha;
      }
      applyResolvedBorder(node, *viewProps, shadowView.layoutMetrics);
      applyBoxShadow(node, *viewProps);
      applyOutline(node, *viewProps);
      applyViewTransform(node, *viewProps);
      applyViewEffects(node, *viewProps);
    }
    if (const auto sampleProps = std::dynamic_pointer_cast<
            const react::HeadlessSampleViewProps>(shadowView.props)) {
      node.customValue = sampleProps->value;
      node.customLabel = sampleProps->label;
    }
    applyImageSource(node, shadowView.props, assetDirectory_);
    applyActivityIndicator(node, shadowView.props);
    applyAndroidSwitch(node, shadowView.props);
    applyModalHost(node, shadowView.props);
    applySwipeRefresh(node, shadowView.props);
    applyDrawerLayout(node, shadowView.props);
    if (const auto inputProps = std::dynamic_pointer_cast<const
            react::BaseTextInputProps>(shadowView.props)) {
      node.textInput = true;
      node.editable = inputProps->editable && !inputProps->readOnly;
      node.multiline = inputProps->multiline;
      node.placeholder = inputProps->placeholder;
      node.text = inputProps->text.empty()
          ? inputProps->defaultValue
          : inputProps->text;
      node.selectionStart = utf16Length(node.text);
      node.selectionEnd = node.selectionStart;
      applyTextInputColors(node, *inputProps);
    }
    if (const auto iosInputProps = std::dynamic_pointer_cast<const
            react::TextInputProps>(shadowView.props);
        iosInputProps && iosInputProps->selection) {
      const auto length = utf16Length(node.text);
      node.selectionStart = std::min<std::size_t>(
          std::max(iosInputProps->selection->start, 0), length);
      node.selectionEnd = std::min<std::size_t>(
          std::max(iosInputProps->selection->end, 0), length);
    }
    if (const auto rawTextProps =
            std::dynamic_pointer_cast<const react::RawTextProps>(
                shadowView.props)) {
      node.text = rawTextProps->text;
    }
    if (const auto inputState = std::dynamic_pointer_cast<const
            react::ConcreteState<react::TextInputState>>(shadowView.state)) {
      const auto& stateData = inputState->getData();
      const auto& attributedString = stateData.attributedStringBox.getMode() ==
              react::AttributedStringBox::Mode::Value &&
              !stateData.attributedStringBox.getValue().isEmpty()
          ? stateData.attributedStringBox.getValue()
          : stateData.reactTreeAttributedString;
      if (!attributedString.isEmpty()) {
        node.text = attributedString.getString();
        node.includeFontPadding =
            stateData.paragraphAttributes.includeFontPadding;
#if RNS_ENABLE_SKIA
        const auto contentWidth = std::max(
            0.0f,
            node.width - node.contentInsetLeft - node.contentInsetRight);
        const auto contentHeight = std::max(
            0.0f,
            node.height - node.contentInsetTop - node.contentInsetBottom);
        node.preparedText = textLayoutManager_->prepareForPaint(
            attributedString,
            stateData.paragraphAttributes,
            react::TextLayoutContext{
                .pointScaleFactor = layout.pointScaleFactor,
                .surfaceId = shadowTreeSurfaceId_},
            react::LayoutConstraints{
                .minimumSize = {0, 0},
                .maximumSize = {
                    .width = contentWidth > 0 ? contentWidth : node.width,
                    .height = contentHeight}},
            ReactNativeSimulator::TextDataDetector::None);
#endif
      }
    }
    if (const auto paragraphState = std::dynamic_pointer_cast<const
            react::ConcreteState<react::ParagraphState>>(shadowView.state)) {
      const auto& attributedString =
          paragraphState->getData().attributedString;
      node.includeFontPadding =
          paragraphState->getData().paragraphAttributes.includeFontPadding;
      node.text = attributedString.getString();
#if RNS_ENABLE_SKIA
      auto dataDetector = ReactNativeSimulator::TextDataDetector::None;
#endif
      if (const auto paragraphProps = std::dynamic_pointer_cast<
              const react::ParagraphProps>(shadowView.props)) {
#if RNS_ENABLE_SKIA
        if (paragraphProps->dataDetectorType) {
          switch (*paragraphProps->dataDetectorType) {
            case react::DataDetectorType::PhoneNumber:
              dataDetector = ReactNativeSimulator::TextDataDetector::Phone;
              break;
            case react::DataDetectorType::Link:
              dataDetector = ReactNativeSimulator::TextDataDetector::Link;
              break;
            case react::DataDetectorType::Email:
              dataDetector = ReactNativeSimulator::TextDataDetector::Email;
              break;
            case react::DataDetectorType::All:
              dataDetector = ReactNativeSimulator::TextDataDetector::All;
              break;
            case react::DataDetectorType::None:
              dataDetector = ReactNativeSimulator::TextDataDetector::None;
              break;
          }
        }
#endif
        if (paragraphProps->paragraphAttributes.textAlignVertical) {
          switch (*paragraphProps->paragraphAttributes.textAlignVertical) {
            case react::TextAlignmentVertical::Center:
              node.textAlignVertical = 1;
              break;
            case react::TextAlignmentVertical::Bottom:
              node.textAlignVertical = 2;
              break;
            case react::TextAlignmentVertical::Auto:
            case react::TextAlignmentVertical::Top:
              node.textAlignVertical = 0;
              break;
          }
        }
      }
#if RNS_ENABLE_SKIA
      if (const auto layoutManager = std::dynamic_pointer_cast<
              const react::HeadlessTextLayoutManager>(
              paragraphState->getData().layoutManager.lock())) {
        // Android TextView onMeasure uses the laid-out content box, so
        // adjustsFontSizeToFit can shrink to maxHeight / parent height.
        // Passing infinite height here rebuilt an unfitted paragraph.
        const auto contentWidth = std::max(
            0.0f,
            node.width - node.contentInsetLeft - node.contentInsetRight);
        const auto contentHeight = std::max(
            0.0f,
            node.height - node.contentInsetTop - node.contentInsetBottom);
        node.preparedText = layoutManager->prepareForPaint(
            attributedString,
            paragraphState->getData().paragraphAttributes,
            react::TextLayoutContext{
                .pointScaleFactor = layout.pointScaleFactor,
                .surfaceId = shadowTreeSurfaceId_},
            react::LayoutConstraints{
                .minimumSize = {0, 0},
                .maximumSize = {
                    .width = contentWidth > 0 ? contentWidth : node.width,
                    .height = contentHeight}},
            dataDetector);
      }
#endif
      if (!attributedString.getFragments().empty()) {
        const auto& attributes =
            attributedString.getFragments().front().textAttributes;
        node.fontSize = std::isfinite(attributes.fontSize)
            ? attributes.fontSize
            : 14.0f;
        node.fontWeight = attributes.fontWeight
            ? static_cast<int>(*attributes.fontWeight)
            : 400;
        node.hasExplicitLineHeight = std::isfinite(attributes.lineHeight);
        node.lineHeight = node.hasExplicitLineHeight
            ? attributes.lineHeight
            : 0.0f;
        node.fontFamily = attributes.fontFamily;
        // React Native's Android CustomStyleSpan enables subpixel text only
        // when font family, weight, or style is explicitly supplied.
        node.subpixelText = !attributes.fontFamily.empty() ||
            attributes.fontWeight.has_value() ||
            attributes.fontStyle.has_value();
        if (attributes.foregroundColor) {
          const auto color =
              react::colorComponentsFromColor(attributes.foregroundColor);
          node.hasTextColor = true;
          node.textRed = color.red;
          node.textGreen = color.green;
          node.textBlue = color.blue;
          node.textAlpha = color.alpha;
        }
      }
    }
    applyScrollViewMetrics(
        node,
        shadowView.props,
        shadowView.state,
        usesIosScrollContentInset(platform_));
    return node;
  }

  void onImageLoaded(int tag, const std::string& path) {
    if (path.empty() || !runtimeExecutor_) {
      return;
    }
    auto weak = weak_from_this();
    runtimeExecutor_([weak, tag, path](facebook::jsi::Runtime&) {
      auto host = weak.lock();
      if (!host) {
        return;
      }
      const auto found = host->mountedViews_.find(tag);
      if (found == host->mountedViews_.end()) {
        return;
      }
      found->second.node.imagePath = path;
      host->snapshotMountedViewTree();
      if (host->onUpdate_) {
        host->onUpdate_(host->result());
      }
    });
  }

  void unbindMountedImage(MountedViewRecord& record) {
    if (!record.imageObserver) {
      return;
    }
    if (const auto imageState = std::dynamic_pointer_cast<
            const react::ImageShadowNode::ConcreteState>(record.state)) {
      imageState->getData()
          .getImageRequest()
          .getObserverCoordinator()
          .removeObserver(record.imageObserver);
    }
    record.imageObserver.reset();
  }

  void bindMountedImage(
      MountedViewRecord& record,
      const react::ShadowView& shadowView) {
    const auto imageState =
        std::dynamic_pointer_cast<const react::ImageShadowNode::ConcreteState>(
            shadowView.state);
    if (record.imageObserver && record.state == shadowView.state) {
      return;
    }
    unbindMountedImage(record);
    if (!imageState) {
      return;
    }
    const auto imageProps = std::dynamic_pointer_cast<const react::ImageProps>(
        shadowView.props);
    const auto emitter =
        std::dynamic_pointer_cast<const react::ImageEventEmitter>(
            shadowView.eventEmitter);
    const bool notify =
        imageProps && imageProps->shouldNotifyLoadEvents && emitter;
    auto observer = std::make_shared<ImageObserver>(
        weak_from_this(),
        shadowView.tag,
        notify,
        emitter,
        imageState->getData().getImageSource());
    record.imageObserver = observer;
    if (notify) {
      emitter->onLoadStart();
    }
    imageState->getData()
        .getImageRequest()
        .getObserverCoordinator()
        .addObserver(observer);
  }

  void clearNativeRipplePressed() {
    bool changed = false;
    for (auto& entry : mountedViews_) {
      if (entry.second.node.nativeRipplePressed) {
        entry.second.node.nativeRipplePressed = false;
        changed = true;
      }
    }
    if (changed) {
      snapshotMountedViewTree();
      if (onUpdate_) {
        onUpdate_(result());
      }
    }
  }

  void setNativeRipplePressed(int tag) {
    clearNativeRipplePressed();
    auto found = mountedViews_.find(tag);
    while (found != mountedViews_.end()) {
      if (found->second.node.nativeRipple) {
        found->second.node.nativeRipplePressed = true;
        snapshotMountedViewTree();
        if (onUpdate_) {
          onUpdate_(result());
        }
        return;
      }
      if (!found->second.node.parentTag) {
        break;
      }
      found = mountedViews_.find(*found->second.node.parentTag);
    }
  }

  void reportMountingError(std::string error) {
    if (std::find(mountingErrors_.begin(), mountingErrors_.end(), error) ==
        mountingErrors_.end()) {
      mountingErrors_.push_back(std::move(error));
    }
  }

  void emitSafeAreaInsetsIfNeeded(const react::ShadowView& view) {
    if (view.componentName == nullptr ||
        std::string(view.componentName) !=
            react::HeadlessRNCSafeAreaProviderName ||
        !view.eventEmitter) {
      return;
    }
    // Notch/status and nav chrome live outside the Fabric window. Emitting the
    // host chrome sizes here would double-pad safe-area consumers.
    const auto& frame = view.layoutMetrics.frame;
    view.eventEmitter->dispatchEvent(
        "topInsetsChange",
        folly::dynamic::object
            ("insets",
             folly::dynamic::object
                 ("top", 0)
                 ("right", 0)
                 ("bottom", 0)
                 ("left", 0))
            ("frame",
             folly::dynamic::object
                 ("x", frame.origin.x)
                 ("y", frame.origin.y)
                 ("width", frame.size.width)
                 ("height", frame.size.height)));
  }

  void applyMountingMutation(const react::ShadowViewMutation& mutation) {
    const auto tag = mutation.type == react::ShadowViewMutation::Delete ||
            mutation.type == react::ShadowViewMutation::Remove
        ? mutation.oldChildShadowView.tag
        : mutation.newChildShadowView.tag;
    switch (mutation.type) {
      case react::ShadowViewMutation::Create: {
        if (mountedViews_.contains(tag)) {
          if (tag == kReactSurfaceId) {
            auto& record = mountedViews_.at(tag);
            auto updated =
                mountedNodeFromShadowView(mutation.newChildShadowView);
            updated.parentTag = std::nullopt;
            record.node = std::move(updated);
            record.eventEmitter = mutation.newChildShadowView.eventEmitter;
            record.state = mutation.newChildShadowView.state;
            break;
          }
          reportMountingError(
              "Create received for existing tag " + std::to_string(tag));
          break;
        }
        MountedViewRecord record;
        record.node = mountedNodeFromShadowView(mutation.newChildShadowView);
        record.eventEmitter = mutation.newChildShadowView.eventEmitter;
        record.state = mutation.newChildShadowView.state;
        bindMountedImage(record, mutation.newChildShadowView);
        mountedViews_.emplace(tag, std::move(record));
        if (mutation.newChildShadowView.componentName != nullptr &&
            std::string(mutation.newChildShadowView.componentName) ==
                "ModalHostView" &&
            mutation.newChildShadowView.eventEmitter) {
          mutation.newChildShadowView.eventEmitter->dispatchEvent(
              "topShow", folly::dynamic::object());
        }
        emitSafeAreaInsetsIfNeeded(mutation.newChildShadowView);
        break;
      }
      case react::ShadowViewMutation::Insert: {
        auto child = mountedViews_.find(tag);
        auto parent = mountedViews_.find(mutation.parentTag);
        if (child == mountedViews_.end() || parent == mountedViews_.end()) {
          reportMountingError(
              "Insert references missing tag " + std::to_string(tag) +
              " or parent " + std::to_string(mutation.parentTag));
          break;
        }
        if (child->second.node.parentTag) {
          reportMountingError(
              "Insert received for already mounted tag " +
              std::to_string(tag));
          break;
        }
        auto& children = parent->second.children;
        if (mutation.index < 0 ||
            mutation.index > static_cast<int>(children.size())) {
          reportMountingError(
              "Insert index " + std::to_string(mutation.index) +
              " is invalid for parent " +
              std::to_string(mutation.parentTag));
          break;
        }
        children.insert(children.begin() + mutation.index, tag);
        child->second.node.parentTag = mutation.parentTag;
        break;
      }
      case react::ShadowViewMutation::Update: {
        if (mutation.oldChildShadowView.tag !=
            mutation.newChildShadowView.tag) {
          reportMountingError("Update changed the mounted tag identity");
          break;
        }
        auto found = mountedViews_.find(tag);
        if (found == mountedViews_.end()) {
          reportMountingError(
              "Update references missing tag " + std::to_string(tag));
          break;
        }
        if (tag != kReactSurfaceId &&
            found->second.node.parentTag != mutation.parentTag) {
          reportMountingError(
              "Update parent mismatch for tag " + std::to_string(tag));
          break;
        }
        const auto parentTag = found->second.node.parentTag;
        const auto wasFocused = found->second.node.focused;
        const auto wasRipplePressed = found->second.node.nativeRipplePressed;
        const auto selectionStart = found->second.node.selectionStart;
        const auto selectionEnd = found->second.node.selectionEnd;
        const auto localText = found->second.node.text;
        const auto previousImagePath = found->second.node.imagePath;
        const auto previousDrawerOffset = found->second.node.drawerOffset;
        const bool previousDrawerLayout = found->second.node.drawerLayout;
        const auto previousScrollOffsetX = found->second.node.scrollOffsetX;
        const auto previousScrollOffsetY = found->second.node.scrollOffsetY;
        const bool previousScrollable = found->second.node.scrollable;
        const auto previousScrollState = std::dynamic_pointer_cast<
            const react::ConcreteState<react::ScrollViewState>>(
            found->second.state);
        const bool imageStateChanged =
            found->second.state != mutation.newChildShadowView.state;
        auto updated = mountedNodeFromShadowView(mutation.newChildShadowView);
        updated.parentTag = parentTag;
        if (updated.imagePath.empty() && !previousImagePath.empty()) {
          updated.imagePath = previousImagePath;
        }
        updated.nativeRipplePressed =
            wasRipplePressed && updated.nativeRipple;
        if (updated.drawerLayout && previousDrawerLayout &&
            !(updated.drawerLocked && updated.drawerOffset >= 1.0f)) {
          updated.drawerOffset = previousDrawerOffset;
        }
        if (updated.scrollable && previousScrollable && previousScrollState) {
          if (const auto nextScrollState = std::dynamic_pointer_cast<
                  const react::ConcreteState<react::ScrollViewState>>(
                  mutation.newChildShadowView.state)) {
            const auto& previousOffset =
                previousScrollState->getData().contentOffset;
            const auto& nextOffset = nextScrollState->getData().contentOffset;
            if (nextOffset.x == previousOffset.x &&
                nextOffset.y == previousOffset.y) {
              updated.scrollOffsetX = previousScrollOffsetX;
              updated.scrollOffsetY = previousScrollOffsetY;
            }
          }
        }
        if (wasFocused) {
          updated.focused = true;
          if (updated.text.empty() || updated.text == localText) {
            updated.text = localText;
          }
          const auto length = utf16Length(updated.text);
          updated.selectionStart = std::min(selectionStart, length);
          updated.selectionEnd = std::min(selectionEnd, length);
        }
        found->second.node = std::move(updated);
        found->second.eventEmitter = mutation.newChildShadowView.eventEmitter;
        found->second.state = mutation.newChildShadowView.state;
        emitSafeAreaInsetsIfNeeded(mutation.newChildShadowView);
        if (imageStateChanged || !found->second.imageObserver) {
          bindMountedImage(found->second, mutation.newChildShadowView);
        }
        if (const auto animated = animatedDirectProps_.find(tag);
            animated != animatedDirectProps_.end()) {
          applyDynamicAnimatedProps(
              found->second.node, animated->second, *contextContainer_);
        }
        break;
      }
      case react::ShadowViewMutation::Remove: {
        auto child = mountedViews_.find(tag);
        auto parent = mountedViews_.find(mutation.parentTag);
        if (child == mountedViews_.end() || parent == mountedViews_.end()) {
          reportMountingError(
              "Remove references missing tag " + std::to_string(tag) +
              " or parent " + std::to_string(mutation.parentTag));
          break;
        }
        if (child->second.node.parentTag != mutation.parentTag) {
          reportMountingError(
              "Remove parent mismatch for tag " + std::to_string(tag));
          break;
        }
        auto& children = parent->second.children;
        if (mutation.index < 0 ||
            mutation.index >= static_cast<int>(children.size()) ||
            children[mutation.index] != tag) {
          reportMountingError(
              "Remove index " + std::to_string(mutation.index) +
              " does not reference tag " + std::to_string(tag) +
              " in parent " + std::to_string(mutation.parentTag));
          break;
        }
        children.erase(children.begin() + mutation.index);
        child->second.node.parentTag.reset();
        break;
      }
      case react::ShadowViewMutation::Delete: {
        const auto found = mountedViews_.find(tag);
        if (found == mountedViews_.end()) {
          reportMountingError(
              "Delete references missing tag " + std::to_string(tag));
          break;
        }
        if (found->second.node.parentTag) {
          reportMountingError(
              "Delete received for mounted tag " + std::to_string(tag));
          break;
        }
        if (!found->second.children.empty()) {
          reportMountingError(
              "Delete received for tag with mounted children " +
              std::to_string(tag));
          break;
        }
        unbindMountedImage(found->second);
        mountedViews_.erase(found);
        if (focusedTextInputTag_ == tag) {
          focusedTextInputTag_.reset();
        }
        break;
      }
    }
  }

  void validateMountedViewTree() {
    const auto root = mountedViews_.find(kReactSurfaceId);
    if (root == mountedViews_.end()) {
      reportMountingError("Retained tree is missing its root");
      return;
    }
    if (root->second.node.parentTag) {
      reportMountingError("Retained tree root unexpectedly has a parent");
    }

    std::unordered_set<int> visiting;
    std::unordered_set<int> visited;
    const auto visit = [&](const auto& self, int tag) -> void {
      if (visiting.contains(tag)) {
        reportMountingError(
            "Retained tree contains a cycle at tag " +
            std::to_string(tag));
        return;
      }
      if (visited.contains(tag)) {
        reportMountingError(
            "Retained tree references tag more than once: " +
            std::to_string(tag));
        return;
      }
      const auto found = mountedViews_.find(tag);
      if (found == mountedViews_.end()) {
        reportMountingError(
            "Retained tree references missing tag " +
            std::to_string(tag));
        return;
      }
      visiting.insert(tag);
      visited.insert(tag);
      std::unordered_set<int> siblings;
      for (const auto childTag : found->second.children) {
        if (!siblings.insert(childTag).second) {
          reportMountingError(
              "Parent " + std::to_string(tag) +
              " contains duplicate child " + std::to_string(childTag));
          continue;
        }
        const auto child = mountedViews_.find(childTag);
        if (child == mountedViews_.end()) {
          reportMountingError(
              "Parent " + std::to_string(tag) +
              " references missing child " + std::to_string(childTag));
          continue;
        }
        if (child->second.node.parentTag != tag) {
          reportMountingError(
              "Parent link mismatch for child " +
              std::to_string(childTag));
          continue;
        }
        self(self, childTag);
      }
      visiting.erase(tag);
    };
    visit(visit, kReactSurfaceId);
    if (visited.size() != mountedViews_.size()) {
      reportMountingError(
          "Retained tree contains " +
          std::to_string(mountedViews_.size() - visited.size()) +
          " unreachable node(s)");
    }
  }

  void requestAnimatedSceneFlush() {
    if (pendingAnimatedSceneFlush_) {
      return;
    }
    pendingAnimatedSceneFlush_ = true;
    if (!eventLoop_) {
      pendingAnimatedSceneFlush_ = false;
      snapshotMountedViewTree();
      if (onUpdate_) {
        onUpdate_(result());
      }
      return;
    }
    eventLoop_->runOnQueue([weak = weak_from_this()] {
      const auto self = weak.lock();
      if (!self || !self->pendingAnimatedSceneFlush_) {
        return;
      }
      self->pendingAnimatedSceneFlush_ = false;
      self->snapshotMountedViewTree();
      if (self->onUpdate_) {
        self->onUpdate_(self->result());
      }
    });
  }

  void snapshotMountedViewTree() {
    const auto root = mountedViews_.find(kReactSurfaceId);
    if (root == mountedViews_.end() || root->second.children.empty()) {
      return;
    }
    std::vector<ReactNativeSimulator::SceneNode> nodes;
    const auto visit = [&](const auto& self,
                           int tag,
                           std::size_t childIndex,
                           std::size_t depth,
                           float parentX,
                           float parentY) -> void {
      const auto found = mountedViews_.find(tag);
      if (found == mountedViews_.end()) {
        return;
      }
      applyInlineAttachmentFrames(found->second);
      expandScrollContentFromChildren(found->second);
      auto node = found->second.node;
      if (node.imagePath.empty() &&
          (node.imageUri.starts_with("http://") ||
           node.imageUri.starts_with("https://"))) {
        const auto cached = headlessCachedImagePath(node.imageUri);
        if (!cached.empty()) {
          node.imagePath = cached.string();
          found->second.node.imagePath = node.imagePath;
        } else {
          const auto uri = node.imageUri;
          headlessPrefetchImage(uri, [uri] {
            if (!headlessCachedImagePath(uri).empty()) {
              hostChrome().invalidate();
            }
          });
        }
      }
      node.childIndex = childIndex;
      node.depth = depth;
      node.absoluteX = parentX + node.x;
      node.absoluteY = parentY + node.y;
      placeModalDialogWindow(node, viewportWidth_, viewportHeight_);
      const auto absoluteX = node.absoluteX;
      const auto absoluteY = node.absoluteY;
      nodes.push_back(std::move(node));
      for (std::size_t index = 0; index < found->second.children.size(); ++index) {
        auto childX = absoluteX;
        auto childY = absoluteY;
        // DrawerLayoutAndroid mounts main content at 0 and the navigation
        // drawer at 1 (ReactDrawerLayout.getChildAt(1)). Closed offset 0
        // parks the drawer off-screen; open slides it over the content.
        if (found->second.node.drawerLayout && index == 1) {
          const auto width = found->second.node.drawerWidth > 0
              ? found->second.node.drawerWidth
              : found->second.node.width * 0.8f;
          const auto open = width * found->second.node.drawerOffset;
          childX = found->second.node.drawerFromLeft
              ? absoluteX - width + open
              : absoluteX + found->second.node.width - open;
        }
        self(
            self,
            found->second.children[index],
            index,
            depth + 1,
            childX,
            childY);
      }
    };
    visit(visit, kReactSurfaceId, 0, 0, 0, 0);
    lastNonEmptyMountingRevision_ = ++sceneRevision_;
    lastNonEmptyMountedViewNodes_ = std::move(nodes);
  }

  void scheduleLayoutAnimationTick() {
    if (!runtimeScheduler_ || !layoutAnimationRunning_) {
      if (animationDriver_ && animationDriver_->shouldAnimateFrame()) {
        layoutAnimationRunning_ = true;
      } else {
        return;
      }
    }
    if (!runtimeScheduler_) {
      return;
    }
    runtimeScheduler_->scheduleWork([this](facebook::jsi::Runtime&) {
      if (uiManager_ &&
          (layoutAnimationRunning_ ||
           (animationDriver_ && animationDriver_->shouldAnimateFrame()))) {
        uiManager_->animationTick();
      }
    });
  }

  std::shared_ptr<react::ContextContainer> contextContainer_;
  std::shared_ptr<react::HeadlessTextLayoutManager> textLayoutManager_;
  std::shared_ptr<const react::ContextContainer> animationContext_;
  react::RuntimeExecutor runtimeExecutor_{};
  std::shared_ptr<react::RuntimeScheduler> runtimeScheduler_;
  std::shared_ptr<SimulatorEventLoop> eventLoop_;
  bool pendingAnimatedSceneFlush_{false};
  std::shared_ptr<react::LayoutAnimationDriver> animationDriver_;
  bool mountingOverrideInstalled_{false};
  bool layoutAnimationRunning_{false};
  react::ComponentDescriptorProviderRegistry providers_;
  react::ComponentDescriptorRegistry::Shared registry_;
  std::shared_ptr<react::UIManager> uiManager_;
  std::shared_ptr<react::EventDispatcher> eventDispatcher_;
  std::size_t transactions_{0};
  std::size_t creates_{0};
  std::size_t inserts_{0};
  std::size_t updates_{0};
  std::size_t removes_{0};
  std::size_t deletes_{0};
  std::int64_t sceneRevision_{0};
  std::unordered_map<int, ActivePointer> activePointers_;
  std::optional<int> focusedTextInputTag_;
  bool sawInitialYogaWidths_{false};
  bool sawUpdatedYogaWidths_{false};
  double commitMs_{0};
  double layoutMs_{0};
  double diffMs_{0};
  bool customComponentCreated_{false};
  int customComponentValue_{0};
  std::string customComponentLabel_;
  std::size_t customCommands_{0};
  std::unordered_set<std::string> fallbackComponentNames_;
  std::vector<std::shared_ptr<std::string>> officialComponentFlavors_;
  std::vector<std::shared_ptr<std::string>> addonComponentFlavors_;
  std::vector<std::string> addonMockComponentNames_;
  std::filesystem::path assetDirectory_;
  float viewportWidth_{0};
  float viewportHeight_{0};
  std::string platform_;
  std::vector<HeadlessReactFabricResult::TraceSpan> traceSpans_;
  int shadowTreeSurfaceId_{0};
  std::int64_t shadowTreeRevision_{0};
  int shadowTreeRootTag_{0};
  std::vector<ReactNativeSimulator::SceneNode> shadowTreeNodes_;
  std::int64_t mountingRevision_{0};
  std::int64_t lastNonEmptyMountingRevision_{0};
  std::unordered_map<int, MountedViewRecord> mountedViews_;
  std::vector<ReactNativeSimulator::SceneNode>
      lastNonEmptyMountedViewNodes_;
  std::vector<std::string> mountingErrors_;
  std::unordered_map<react::Tag, react::Rect> frames_;
  std::unordered_map<react::Tag, folly::dynamic> animatedDirectProps_;
  std::vector<OnSurfaceStartCallback> callbacks_;
  HeadlessReactFabricUpdate onUpdate_;
};

std::shared_ptr<HeadlessReactFabricHost> installHeadlessReactFabric(
    facebook::jsi::Runtime& runtime,
    react::RuntimeExecutor runtimeExecutor,
    std::shared_ptr<react::RuntimeScheduler> runtimeScheduler,
    std::shared_ptr<SimulatorEventLoop> eventLoop,
    float viewportWidth,
    float viewportHeight,
    float pointScaleFactor,
    float insetTop,
    float insetBottom,
    const std::filesystem::path& fontDirectory,
    const std::filesystem::path& assetDirectory,
    const std::string& platform,
    std::vector<ReactNativeSimulator::AddonComponentDeclaration>
        addonComponents,
    std::vector<react::ComponentDescriptorProvider> addonProviders,
    HeadlessReactFabricUpdate onUpdate) {
  return std::make_shared<HeadlessReactFabricHost>(
      runtime,
      std::move(runtimeExecutor),
      std::move(runtimeScheduler),
      std::move(eventLoop),
      viewportWidth,
      viewportHeight,
      pointScaleFactor,
      insetTop,
      insetBottom,
      fontDirectory,
      assetDirectory,
      platform,
      std::move(addonComponents),
      std::move(addonProviders),
      std::move(onUpdate));
}

std::shared_ptr<react::UIManager> getHeadlessReactFabricUIManager() {
  return gHeadlessUIManager.lock();
}

HeadlessReactFabricResult getHeadlessReactFabricResult(
    const HeadlessReactFabricHost& host) {
  return host.result();
}

ReactNativeSimulator::InteractionResult dispatchHeadlessReactFabricAction(
    HeadlessReactFabricHost& host,
    const ReactNativeSimulator::InteractionAction& action,
    std::uint64_t sequence) {
  return host.dispatchInteraction(action, sequence);
}
