#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace ReactNativeSimulator {

class SkiaPreparedParagraph;

// Renderer-independent retained output of Fabric mounting. Coordinates are
// React Native logical points; frontends apply pointScaleFactor at raster time.
struct SceneNode {
  int tag{0};
  std::optional<int> parentTag;
  std::size_t childIndex{0};
  std::size_t depth{0};
  std::string componentName;
  std::string nativeId;
  bool layoutable{false};
  float x{0};
  float y{0};
  float width{0};
  float height{0};
  float absoluteX{0};
  float absoluteY{0};
  float contentInsetTop{0};
  float contentInsetRight{0};
  float contentInsetBottom{0};
  float contentInsetLeft{0};
  float borderTop{0};
  float borderRight{0};
  float borderBottom{0};
  float borderLeft{0};
  std::string display{"flex"};
  std::string position{"relative"};
  float opacity{1};
  bool hasTransform{false};
  // RN column-major 4x4. `perspective` writes m[11] = -1/p; rotateX/Y move
  // Y/X into Z so the perspective term foreshortens the view quad.
  float transformM[16]{
      1, 0, 0, 0,
      0, 1, 0, 0,
      0, 0, 1, 0,
      0, 0, 0, 1};
  // Extra translation RN resolveTransform wraps around the view center when
  // transformOrigin is set. Native-driven transform updates reuse this so the
  // origin survives synchronouslyUpdateViewOnUIThread.
  bool hasTransformOrigin{false};
  float transformOriginX{0};
  float transformOriginY{0};
  bool hasBackgroundColor{false};
  float backgroundRed{0};
  float backgroundGreen{0};
  float backgroundBlue{0};
  float backgroundAlpha{0};
  bool hasBorderColor{false};
  float borderRed{0};
  float borderGreen{0};
  float borderBlue{0};
  float borderAlpha{0};
  bool hasBorderTopColor{false};
  float borderTopRed{0};
  float borderTopGreen{0};
  float borderTopBlue{0};
  float borderTopAlpha{0};
  bool hasBorderRightColor{false};
  float borderRightRed{0};
  float borderRightGreen{0};
  float borderRightBlue{0};
  float borderRightAlpha{0};
  bool hasBorderBottomColor{false};
  float borderBottomRed{0};
  float borderBottomGreen{0};
  float borderBottomBlue{0};
  float borderBottomAlpha{0};
  bool hasBorderLeftColor{false};
  float borderLeftRed{0};
  float borderLeftGreen{0};
  float borderLeftBlue{0};
  float borderLeftAlpha{0};
  std::string borderStyleTop{"solid"};
  std::string borderStyleRight{"solid"};
  std::string borderStyleBottom{"solid"};
  std::string borderStyleLeft{"solid"};
  float outlineWidth{0};
  float outlineOffset{0};
  bool hasOutlineColor{false};
  float outlineRed{0};
  float outlineGreen{0};
  float outlineBlue{0};
  float outlineAlpha{0};
  std::string outlineStyle{"solid"};
  float borderRadius{0};
  float borderRadiusTopLeft{0};
  float borderRadiusTopRight{0};
  float borderRadiusBottomRight{0};
  float borderRadiusBottomLeft{0};
  float borderRadiusTopLeftX{0};
  float borderRadiusTopLeftY{0};
  float borderRadiusTopRightX{0};
  float borderRadiusTopRightY{0};
  float borderRadiusBottomRightX{0};
  float borderRadiusBottomRightY{0};
  float borderRadiusBottomLeftX{0};
  float borderRadiusBottomLeftY{0};
  std::optional<int> zIndex;
  std::string pointerEvents{"auto"};
  bool clipsContentToBounds{false};
  // Fabric inline Image/View inside <Text>. Android ReplacementSpan hosts
  // are not clipped by the Text overflow box (Pixel 4a RN Tester
  // "clipped by <Text>" still paints outside the grey frame).
  bool inlineAttachment{false};
  float hitSlopTop{0};
  float hitSlopRight{0};
  float hitSlopBottom{0};
  float hitSlopLeft{0};
  bool collapsable{true};
  std::optional<int> customValue;
  std::string customLabel;
  std::string text;
  float fontSize{0};
  int fontWeight{400};
  float lineHeight{0};
  bool hasExplicitLineHeight{false};
  bool includeFontPadding{true};
  bool subpixelText{false};
  int textAlignVertical{0};
  std::string fontFamily;
  bool hasTextColor{false};
  float textRed{0};
  float textGreen{0};
  float textBlue{0};
  float textAlpha{1};
  // In-process scenes retain the exact Skia paragraph prepared by the text
  // layout service. Serialized diagnostic scenes intentionally omit it.
  std::shared_ptr<const SkiaPreparedParagraph> preparedText;
  bool textInput{false};
  bool editable{true};
  bool multiline{false};
  bool focused{false};
  std::string placeholder;
  std::size_t selectionStart{0};
  std::size_t selectionEnd{0};
  bool scrollable{false};
  float scrollOffsetX{0};
  float scrollOffsetY{0};
  float scrollContentWidth{0};
  float scrollContentHeight{0};
  std::string imageUri;
  std::string imagePath;
  std::string imageDefaultPath;
  std::string imageResizeMode{"stretch"};
  float imageBlurRadius{0};
  bool hasImageTint{false};
  float imageTintRed{0};
  float imageTintGreen{0};
  float imageTintBlue{0};
  float imageTintAlpha{1};
  bool hasBoxShadow{false};
  float boxShadowOffsetX{0};
  float boxShadowOffsetY{0};
  float boxShadowBlur{0};
  float boxShadowSpread{0};
  float boxShadowRed{0};
  float boxShadowGreen{0};
  float boxShadowBlue{0};
  float boxShadowAlpha{1};
  bool boxShadowInset{false};
  struct BoxShadowLayer {
    float offsetX{0};
    float offsetY{0};
    float blur{0};
    float spread{0};
    float red{0};
    float green{0};
    float blue{0};
    float alpha{1};
    bool inset{false};
  };
  std::vector<BoxShadowLayer> boxShadows;
  bool backfaceHidden{false};
  bool needsOffscreenAlphaCompositing{false};
  bool nativeRipple{false};
  bool nativeRipplePressed{false};
  bool nativeRippleBorderless{false};
  float nativeRippleRed{0};
  float nativeRippleGreen{0};
  float nativeRippleBlue{0};
  float nativeRippleAlpha{0.2f};
  bool activityIndicator{false};
  bool activityIndicatorAnimating{true};
  bool activityIndicatorHidesWhenStopped{true};
  bool activityIndicatorHorizontal{false};
  float activityIndicatorProgress{0};
  bool hasActivityIndicatorColor{false};
  float activityIndicatorRed{0.6f};
  float activityIndicatorGreen{0.6f};
  float activityIndicatorBlue{0.6f};
  float activityIndicatorAlpha{1};
  bool androidSwitch{false};
  bool androidSwitchOn{false};
  bool androidSwitchEnabled{true};
  bool hasSwitchThumbColor{false};
  float switchThumbRed{1};
  float switchThumbGreen{1};
  float switchThumbBlue{1};
  float switchThumbAlpha{1};
  bool hasSwitchTrackColor{false};
  float switchTrackRed{0.6f};
  float switchTrackGreen{0.6f};
  float switchTrackBlue{0.6f};
  float switchTrackAlpha{1};
  bool modalHost{false};
  bool modalTransparent{false};
  bool swipeRefresh{false};
  bool swipeRefreshing{false};
  bool swipeRefreshEnabled{true};
  float swipeRefreshOffset{0};
  float swipeRefreshRed{0.22745098f};
  float swipeRefreshGreen{0.5137255f};
  float swipeRefreshBlue{0.46666667f};
  float swipeRefreshAlpha{1};
  bool drawerLayout{false};
  bool drawerFromLeft{true};
  bool drawerLocked{false};
  float drawerWidth{0};
  float drawerOffset{0}; // 0 closed (Android default), 1 fully open
  float drawerPanelRed{1};
  float drawerPanelGreen{1};
  float drawerPanelBlue{1};
  float drawerPanelAlpha{1};
  std::string mixBlendMode;
  bool isolationIsolate{false};
  float filterBlur{0};
  float filterBrightness{1};
  float filterContrast{1};
  float filterGrayscale{0};
  float filterSaturate{1};
  float filterSepia{0};
  float filterInvert{0};
  float filterHueRotate{0};
  struct FilterOp {
    std::string type;
    float amount{0};
    float dropShadowOffsetX{0};
    float dropShadowOffsetY{0};
    float dropShadowStdDev{0};
    float dropShadowRed{0};
    float dropShadowGreen{0};
    float dropShadowBlue{0};
    float dropShadowAlpha{1};
  };
  std::vector<FilterOp> filters;
  bool hasBackgroundGradient{false};
  bool backgroundGradientRadial{false};
  float backgroundGradientX0{0};
  float backgroundGradientY0{0};
  float backgroundGradientX1{0};
  float backgroundGradientY1{1};
  float backgroundGradientR0{0};
  float backgroundGradientG0{0};
  float backgroundGradientB0{0};
  float backgroundGradientA0{1};
  float backgroundGradientR1{1};
  float backgroundGradientG1{1};
  float backgroundGradientB1{1};
  float backgroundGradientA1{1};
  struct GradientStop {
    float offset{0};
    float red{0};
    float green{0};
    float blue{0};
    float alpha{1};
  };
  struct BackgroundImageLayer {
    bool radial{false};
    bool ellipse{false};
    float x0{0.5f};
    float y0{0.5f};
    float x1{0.5f};
    float y1{1.0f};
    std::vector<GradientStop> stops;
  };
  std::vector<BackgroundImageLayer> backgroundImageLayers;
};

struct SceneSnapshot {
  int surfaceId{0};
  std::int64_t revision{0};
  int rootTag{0};
  float viewportWidth{0};
  float viewportHeight{0};
  float pointScaleFactor{1};
  float insetTop{0};
  float insetBottom{0};
  std::vector<SceneNode> nodes;
  std::int64_t shadowRevision{0};
  int shadowRootTag{0};
  std::vector<SceneNode> shadowNodes;
  std::vector<std::string> mountingErrors;
  bool statusBarHidden{true};
  float statusBarHeight{24};
  float statusBarRed{0};
  float statusBarGreen{0};
  float statusBarBlue{0};
  float statusBarAlpha{1};
  std::string toastMessage;
  int toastGravity{81};
  float toastOffsetX{0};
  float toastOffsetY{0};
  std::int64_t toastUntilMs{0};

  bool empty() const noexcept {
    return nodes.empty();
  }
};

} // namespace ReactNativeSimulator
