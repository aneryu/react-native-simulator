#pragma once

#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/components/view/YogaLayoutableShadowNode.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/RectangleEdges.h>
#include <yoga/Yoga.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace facebook::react {

struct HeadlessViewportSize {
  float width{0};
  float height{0};
  // Host status/nav chrome reserved around the Fabric window, not in-window
  // content insets.
  float insetTop{24};
  float insetRight{0};
  float insetBottom{0};
  float insetLeft{0};
};

inline constexpr const char kHeadlessViewportKey[] = "HeadlessViewport";

struct HeadlessOfficialComponentSpec {
  const char* name;
  const char* fidelity;
  bool leaf;
  float measureWidth;
  float measureHeight;
};

// Official RN component names that this host cannot compile from Codegen
// (FBReactNativeSpec is not in the GitHub source checkout). They are
// registered explicitly as layout-only placeholders, not silent fallbacks.
inline constexpr HeadlessOfficialComponentSpec kHeadlessOfficialComponents[] = {
    {"ActivityIndicatorView", "skia-activity-indicator", true, 20, 20},
    {"AndroidSwitch", "skia-switch", true, 52, 32},
    {"Switch", "layout-only-placeholder", true, 51, 31},
    {"AndroidProgressBar", "skia-progress-indicator", true, 48, 48},
    {"ModalHostView", "skia-modal-host", false, 0, 0},
    {"AndroidDrawerLayout", "skia-drawer-layout", false, 0, 0},
    {"AndroidSwipeRefreshLayout", "skia-refresh-control", false, 0, 0},
    {"PullToRefreshView", "layout-only-placeholder", false, 0, 0},
    {"AndroidHorizontalScrollView", "headless-viewport-state", false, 0, 0},
    {"AndroidHorizontalScrollContentView", "real-fabric-yoga", false, 0, 0},
    {"SafeAreaView", "window-relative-insets", false, 0, 0},
    {"InputAccessory", "layout-only-placeholder", false, 0, 0},
    {"VirtualView", "layout-only-placeholder", false, 0, 0},
    {"VirtualViewExperimental", "layout-only-placeholder", false, 0, 0},
    {"DebuggingOverlay", "layout-only-placeholder", false, 0, 0},
    {"RCTImageView", "layout-only-placeholder", true, 0, 0},
};

extern const char HeadlessActivityIndicatorViewName[];
extern const char HeadlessAndroidSwitchName[];
extern const char HeadlessSwitchName[];
extern const char HeadlessAndroidProgressBarName[];
extern const char HeadlessModalHostViewName[];
extern const char HeadlessAndroidSwipeRefreshLayoutName[];
extern const char HeadlessAndroidDrawerLayoutName[];
extern const char HeadlessSafeAreaViewName[];
extern const char HeadlessRNCSafeAreaProviderName[];
extern const char HeadlessRNCSafeAreaViewName[];

template <const char* Name, int Width, int Height>
class HeadlessMeasuredLeafShadowNode final
    : public ConcreteViewShadowNode<Name, ViewProps, ViewEventEmitter> {
 public:
  using ConcreteViewShadowNode<Name, ViewProps, ViewEventEmitter>::
      ConcreteViewShadowNode;

  static ShadowNodeTraits BaseTraits() {
    auto traits =
        ConcreteViewShadowNode<Name, ViewProps, ViewEventEmitter>::BaseTraits();
    traits.set(ShadowNodeTraits::Trait::LeafYogaNode);
    traits.set(ShadowNodeTraits::Trait::MeasurableYogaNode);
    return traits;
  }

  Size measureContent(
      const LayoutContext&,
      const LayoutConstraints& constraints) const override {
    auto width = Width > 0 ? static_cast<float>(Width)
                           : constraints.maximumSize.width;
    auto height = Height > 0 ? static_cast<float>(Height)
                             : constraints.maximumSize.height;
    if (!std::isfinite(width) || width <= 0 || width > 100000.0f) {
      width = std::max(constraints.minimumSize.width, 0.0f);
    }
    if (!std::isfinite(height) || height <= 0 || height > 100000.0f) {
      height = std::max(constraints.minimumSize.height, 0.0f);
    }
    return {.width = width, .height = height};
  }
};

class HeadlessActivityIndicatorViewProps final : public ViewProps {
 public:
  HeadlessActivityIndicatorViewProps() = default;
  HeadlessActivityIndicatorViewProps(
      const PropsParserContext& context,
      const HeadlessActivityIndicatorViewProps& sourceProps,
      const RawProps& rawProps);

  bool animating{true};
  bool hidesWhenStopped{true};
  SharedColor color{};
  std::string size{"small"};
};

class HeadlessActivityIndicatorShadowNode final
    : public ConcreteViewShadowNode<
          HeadlessActivityIndicatorViewName,
          HeadlessActivityIndicatorViewProps,
          ViewEventEmitter> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  static ShadowNodeTraits BaseTraits() {
    auto traits = ConcreteViewShadowNode::BaseTraits();
    traits.set(ShadowNodeTraits::Trait::LeafYogaNode);
    traits.set(ShadowNodeTraits::Trait::MeasurableYogaNode);
    return traits;
  }

  Size measureContent(
      const LayoutContext&,
      const LayoutConstraints& constraints) const override {
    const auto large = getConcreteProps().size == "large";
    auto size = large ? 36.0f : 20.0f;
    size = std::clamp(
        size, constraints.minimumSize.width, constraints.maximumSize.width);
    auto height = large ? 36.0f : 20.0f;
    height = std::clamp(
        height,
        constraints.minimumSize.height,
        constraints.maximumSize.height);
    return {.width = size, .height = height};
  }
};

class HeadlessAndroidProgressBarProps final : public ViewProps {
 public:
  HeadlessAndroidProgressBarProps() = default;
  HeadlessAndroidProgressBarProps(
      const PropsParserContext& context,
      const HeadlessAndroidProgressBarProps& sourceProps,
      const RawProps& rawProps);

  std::string styleAttr{"Normal"};
  std::string typeAttr;
  bool indeterminate{true};
  double progress{0};
  bool animating{true};
  SharedColor color{};
};

class HeadlessAndroidProgressBarShadowNode final
    : public ConcreteViewShadowNode<
          HeadlessAndroidProgressBarName,
          HeadlessAndroidProgressBarProps,
          ViewEventEmitter> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  static ShadowNodeTraits BaseTraits() {
    auto traits = ConcreteViewShadowNode::BaseTraits();
    traits.set(ShadowNodeTraits::Trait::LeafYogaNode);
    traits.set(ShadowNodeTraits::Trait::MeasurableYogaNode);
    return traits;
  }

  Size measureContent(
      const LayoutContext&,
      const LayoutConstraints& constraints) const override {
    const auto& props = getConcreteProps();
    if (props.styleAttr == "Horizontal") {
      auto width = constraints.maximumSize.width;
      if (!std::isfinite(width) || width <= 0 || width > 100000.0f) {
        width = std::max(constraints.minimumSize.width, 0.0f);
      }
      auto height = 16.0f;
      height = std::clamp(
          height,
          constraints.minimumSize.height,
          constraints.maximumSize.height);
      return {.width = width, .height = height};
    }
    float size = 48.0f;
    if (props.styleAttr == "Small" || props.styleAttr == "SmallInverse") {
      size = 16.0f;
    } else if (
        props.styleAttr == "Large" || props.styleAttr == "LargeInverse") {
      size = 76.0f;
    }
    auto width = std::clamp(
        size, constraints.minimumSize.width, constraints.maximumSize.width);
    auto height = std::clamp(
        size, constraints.minimumSize.height, constraints.maximumSize.height);
    return {.width = width, .height = height};
  }
};

class HeadlessAndroidSwitchProps final : public ViewProps {
 public:
  HeadlessAndroidSwitchProps() = default;
  HeadlessAndroidSwitchProps(
      const PropsParserContext& context,
      const HeadlessAndroidSwitchProps& sourceProps,
      const RawProps& rawProps);

  bool value{false};
  bool on{false};
  bool disabled{false};
  bool enabled{true};
  SharedColor thumbTintColor{};
  SharedColor trackColorForFalse{};
  SharedColor trackColorForTrue{};
  SharedColor trackTintColor{};

  bool isOn() const {
    return value || on;
  }
  bool isEnabled() const {
    return enabled && !disabled;
  }
};

class HeadlessAndroidSwitchShadowNode final
    : public ConcreteViewShadowNode<
          HeadlessAndroidSwitchName,
          HeadlessAndroidSwitchProps,
          ViewEventEmitter> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  static ShadowNodeTraits BaseTraits() {
    auto traits = ConcreteViewShadowNode::BaseTraits();
    traits.set(ShadowNodeTraits::Trait::LeafYogaNode);
    traits.set(ShadowNodeTraits::Trait::MeasurableYogaNode);
    return traits;
  }

  Size measureContent(
      const LayoutContext&,
      const LayoutConstraints& constraints) const override {
    auto width = std::clamp(
        52.0f, constraints.minimumSize.width, constraints.maximumSize.width);
    auto height = std::clamp(
        32.0f, constraints.minimumSize.height, constraints.maximumSize.height);
    return {.width = width, .height = height};
  }
};

using HeadlessSwitchShadowNode =
    HeadlessMeasuredLeafShadowNode<HeadlessSwitchName, 51, 31>;

class HeadlessModalHostViewProps final : public ViewProps {
 public:
  HeadlessModalHostViewProps() = default;
  HeadlessModalHostViewProps(
      const PropsParserContext& context,
      const HeadlessModalHostViewProps& sourceProps,
      const RawProps& rawProps);

  bool transparent{false};
  bool visible{true};
  std::string animationType{"none"};
};

using HeadlessActivityIndicatorComponentDescriptor =
    ConcreteComponentDescriptor<HeadlessActivityIndicatorShadowNode>;
using HeadlessAndroidSwitchComponentDescriptor =
    ConcreteComponentDescriptor<HeadlessAndroidSwitchShadowNode>;
using HeadlessSwitchComponentDescriptor =
    ConcreteComponentDescriptor<HeadlessSwitchShadowNode>;
using HeadlessAndroidProgressBarComponentDescriptor =
    ConcreteComponentDescriptor<HeadlessAndroidProgressBarShadowNode>;

class HeadlessModalHostViewShadowNode final
    : public ConcreteViewShadowNode<
          HeadlessModalHostViewName,
          HeadlessModalHostViewProps,
          ViewEventEmitter> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  static ShadowNodeTraits BaseTraits() {
    auto traits = ConcreteViewShadowNode::BaseTraits();
    traits.set(ShadowNodeTraits::Trait::RootNodeKind);
    traits.set(ShadowNodeTraits::Trait::Unstable_uncullableView);
    return traits;
  }

  // Size the Yoga node to the engine viewport so children layout like an
  // Android Dialog window rather than a card in the parent.
  void applyDialogWindowStyle(Size size) const;

  // Rebase the host frame to the window origin after Yoga places it in the
  // parent. Children keep viewport-relative coordinates.
  void layout(LayoutContext layoutContext) override;
};

class HeadlessModalHostViewComponentDescriptor final
    : public ConcreteComponentDescriptor<HeadlessModalHostViewShadowNode> {
 public:
  using ConcreteComponentDescriptor::ConcreteComponentDescriptor;

  void adopt(ShadowNode& shadowNode) const override {
    auto& modal = static_cast<HeadlessModalHostViewShadowNode&>(shadowNode);
    Size size{0, 0};
    if (this->contextContainer_) {
      if (auto viewport = this->contextContainer_->find<HeadlessViewportSize>(
              kHeadlessViewportKey)) {
        size = {.width = viewport->width, .height = viewport->height};
      }
    }
    modal.applyDialogWindowStyle(size);
    ConcreteComponentDescriptor::adopt(shadowNode);
  }
};

class HeadlessAndroidSwipeRefreshLayoutProps final : public ViewProps {
 public:
  HeadlessAndroidSwipeRefreshLayoutProps() = default;
  HeadlessAndroidSwipeRefreshLayoutProps(
      const PropsParserContext& context,
      const HeadlessAndroidSwipeRefreshLayoutProps& sourceProps,
      const RawProps& rawProps);

  bool enabled{true};
  bool refreshing{false};
  float progressViewOffset{0};
  std::string size{"default"};
  SharedColor progressBackgroundColor{};
  SharedColor color{};
};

using HeadlessAndroidSwipeRefreshLayoutShadowNode = ConcreteViewShadowNode<
    HeadlessAndroidSwipeRefreshLayoutName,
    HeadlessAndroidSwipeRefreshLayoutProps,
    ViewEventEmitter>;
using HeadlessAndroidSwipeRefreshLayoutComponentDescriptor =
    ConcreteComponentDescriptor<HeadlessAndroidSwipeRefreshLayoutShadowNode>;

class HeadlessAndroidDrawerLayoutProps final : public ViewProps {
 public:
  HeadlessAndroidDrawerLayoutProps() = default;
  HeadlessAndroidDrawerLayoutProps(
      const PropsParserContext& context,
      const HeadlessAndroidDrawerLayoutProps& sourceProps,
      const RawProps& rawProps);

  std::string drawerPosition{"left"};
  std::string drawerLockMode{"unlocked"};
  float drawerWidth{-1};
  SharedColor drawerBackgroundColor{};
};

// Host view, not a View stand-in. Child 0 is main content; child 1 is the
// navigation drawer (same order as RN DrawerLayoutAndroid / ReactDrawerLayout).
// Open state lives on SceneNode.drawerOffset (0 closed).

using HeadlessAndroidDrawerLayoutShadowNode = ConcreteViewShadowNode<
    HeadlessAndroidDrawerLayoutName,
    HeadlessAndroidDrawerLayoutProps,
    ViewEventEmitter>;
using HeadlessAndroidDrawerLayoutComponentDescriptor =
    ConcreteComponentDescriptor<HeadlessAndroidDrawerLayoutShadowNode>;

class HeadlessSafeAreaViewShadowNode final
    : public ConcreteViewShadowNode<
          HeadlessSafeAreaViewName,
          ViewProps,
          ViewEventEmitter> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;
};

class HeadlessSafeAreaViewComponentDescriptor final
    : public ConcreteComponentDescriptor<HeadlessSafeAreaViewShadowNode> {
 public:
  using ConcreteComponentDescriptor::ConcreteComponentDescriptor;
};

using HeadlessRNCSafeAreaProviderShadowNode = ConcreteViewShadowNode<
    HeadlessRNCSafeAreaProviderName,
    ViewProps,
    ViewEventEmitter>;
using HeadlessRNCSafeAreaProviderComponentDescriptor =
    ConcreteComponentDescriptor<HeadlessRNCSafeAreaProviderShadowNode>;

using HeadlessRNCSafeAreaViewShadowNode = ConcreteViewShadowNode<
    HeadlessRNCSafeAreaViewName,
    ViewProps,
    ViewEventEmitter>;
using HeadlessRNCSafeAreaViewComponentDescriptor =
    ConcreteComponentDescriptor<HeadlessRNCSafeAreaViewShadowNode>;

void registerHeadlessOfficialComponents(
    ComponentDescriptorProviderRegistry& providers,
    std::vector<std::shared_ptr<std::string>>& flavorStorage);

} // namespace facebook::react
