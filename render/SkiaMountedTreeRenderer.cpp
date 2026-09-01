#include "SkiaMountedTreeRenderer.h"
#include "SkiaTextLayoutEngine.h"

#include <folly/dynamic.h>
#include <react-native-simulator/SceneTransform.h>

#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPoint.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkColorMatrix.h"
#include "include/core/SkColor.h"
#include "include/core/SkSpan.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkImageFilters.h"
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
#include "include/ports/SkImageGeneratorCG.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <unordered_set>
#include <vector>
#include <unordered_map>
#include <utility>

namespace ReactNativeSimulator {
namespace {

const folly::dynamic* field(const folly::dynamic& object, const char* name) {
  return object.isObject() ? object.get_ptr(name) : nullptr;
}

double numberValue(
    const folly::dynamic& object,
    const char* name,
    double fallback = 0.0) {
  const auto* value = field(object, name);
  return value != nullptr && value->isNumber() ? value->asDouble() : fallback;
}

bool booleanValue(
    const folly::dynamic& object,
    const char* name,
    bool fallback = false) {
  const auto* value = field(object, name);
  return value != nullptr && value->isBool() ? value->asBool() : fallback;
}

std::string stringValue(
    const folly::dynamic& object,
    const char* name,
    std::string fallback = {}) {
  const auto* value = field(object, name);
  return value != nullptr && value->isString() ? value->asString()
                                               : std::move(fallback);
}

void readSceneColor(
    const folly::dynamic& color,
    bool& has,
    float& red,
    float& green,
    float& blue,
    float& alpha) {
  if (!color.isObject()) {
    return;
  }
  has = true;
  red = static_cast<float>(numberValue(color, "red"));
  green = static_cast<float>(numberValue(color, "green"));
  blue = static_cast<float>(numberValue(color, "blue"));
  alpha = static_cast<float>(numberValue(color, "alpha", 1.0));
}

void readCornerRadius(
    const folly::dynamic& radii,
    const char* name,
    float& circular,
    float& x,
    float& y) {
  const auto* value = field(radii, name);
  if (value == nullptr) {
    return;
  }
  if (value->isNumber()) {
    circular = x = y = static_cast<float>(value->asDouble());
    return;
  }
  if (value->isObject()) {
    x = static_cast<float>(numberValue(*value, "x"));
    y = static_cast<float>(numberValue(*value, "y"));
    circular = std::min(x, y);
  }
}

SceneNode::BoxShadowLayer readBoxShadowLayer(const folly::dynamic& shadow) {
  SceneNode::BoxShadowLayer layer;
  layer.offsetX = static_cast<float>(numberValue(shadow, "offsetX"));
  layer.offsetY = static_cast<float>(numberValue(shadow, "offsetY"));
  layer.blur = static_cast<float>(numberValue(shadow, "blurRadius"));
  layer.spread = static_cast<float>(numberValue(shadow, "spreadDistance"));
  layer.inset = booleanValue(shadow, "inset");
  if (const auto* color = field(shadow, "color");
      color != nullptr && color->isObject()) {
    layer.red = static_cast<float>(numberValue(*color, "red"));
    layer.green = static_cast<float>(numberValue(*color, "green"));
    layer.blue = static_cast<float>(numberValue(*color, "blue"));
    layer.alpha = static_cast<float>(numberValue(*color, "alpha", 1.0));
  }
  return layer;
}

void syncPrimaryBoxShadow(SceneNode& node) {
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

void applyBorderStyle(SkPaint& paint, const std::string& style, float width) {
  if (width <= 0.0f || style == "solid" || style.empty()) {
    return;
  }
  if (style == "dotted") {
    paint.setStrokeCap(SkPaint::kRound_Cap);
    const SkScalar intervals[] = {width, width};
    paint.setPathEffect(SkDashPathEffect::Make({intervals, 2}, 0));
    return;
  }
  if (style == "dashed") {
    paint.setStrokeCap(SkPaint::kButt_Cap);
    const SkScalar intervals[] = {width * 3.0f, width};
    paint.setPathEffect(SkDashPathEffect::Make({intervals, 2}, 0));
  }
}

// OutlineDrawable: BUTT cap, dashed 3w/3w, dotted 1w/1w. Round caps turn
// a thick dotted circle into sausages; Android's butt dashes look like a
// sunburst around rounded outlines.
void applyOutlineStyle(SkPaint& paint, const std::string& style, float width) {
  if (width <= 0.0f || style == "solid" || style.empty()) {
    return;
  }
  paint.setStrokeCap(SkPaint::kButt_Cap);
  if (style == "dotted") {
    const SkScalar intervals[] = {width, width};
    paint.setPathEffect(SkDashPathEffect::Make({intervals, 2}, 0));
    return;
  }
  if (style == "dashed") {
    const SkScalar intervals[] = {width * 3.0f, width * 3.0f};
    paint.setPathEffect(SkDashPathEffect::Make({intervals, 2}, 0));
  }
}

SceneNode outlineStrokeNode(const SceneNode& node, float grow) {
  SceneNode outline = node;
  auto expand = [grow](float radius) {
    return radius > 0.0f ? std::max(0.0f, radius + grow) : 0.0f;
  };
  outline.borderRadius = expand(node.borderRadius);
  outline.borderRadiusTopLeft = expand(node.borderRadiusTopLeft);
  outline.borderRadiusTopRight = expand(node.borderRadiusTopRight);
  outline.borderRadiusBottomRight = expand(node.borderRadiusBottomRight);
  outline.borderRadiusBottomLeft = expand(node.borderRadiusBottomLeft);
  outline.borderRadiusTopLeftX = expand(node.borderRadiusTopLeftX);
  outline.borderRadiusTopLeftY = expand(node.borderRadiusTopLeftY);
  outline.borderRadiusTopRightX = expand(node.borderRadiusTopRightX);
  outline.borderRadiusTopRightY = expand(node.borderRadiusTopRightY);
  outline.borderRadiusBottomRightX = expand(node.borderRadiusBottomRightX);
  outline.borderRadiusBottomRightY = expand(node.borderRadiusBottomRightY);
  outline.borderRadiusBottomLeftX = expand(node.borderRadiusBottomLeftX);
  outline.borderRadiusBottomLeftY = expand(node.borderRadiusBottomLeftY);
  return outline;
}

bool sameBorderColor(
    bool hasA,
    float aR,
    float aG,
    float aB,
    float aA,
    bool hasB,
    float bR,
    float bG,
    float bB,
    float bA) {
  if (hasA != hasB) {
    return false;
  }
  if (!hasA) {
    return true;
  }
  return std::fabs(aR - bR) < 0.001f && std::fabs(aG - bG) < 0.001f &&
      std::fabs(aB - bB) < 0.001f && std::fabs(aA - bA) < 0.001f;
}

const folly::dynamic* layoutOf(const folly::dynamic& node) {
  const auto* layout = field(node, "layout");
  return layout != nullptr && layout->isObject() ? layout : nullptr;
}

int tagOf(const folly::dynamic& node) {
  return static_cast<int>(numberValue(node, "tag"));
}

SkColor sceneColor(
    float red,
    float green,
    float blue,
    float alpha,
    float opacity = 1.0f);

std::int64_t wallClockMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::int64_t positiveMod(std::int64_t value, std::int64_t period) {
  auto remainder = value % period;
  return remainder < 0 ? remainder + period : remainder;
}

float cubicBezier(float u, float a, float b, float c, float d) {
  const auto i = 1.0f - u;
  return i * i * i * a + 3.0f * i * i * u * b + 3.0f * i * u * u * c +
      u * u * u * d;
}

float cubicYForX(
    float x,
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    float x3,
    float y3) {
  float lo = 0.0f;
  float hi = 1.0f;
  for (int i = 0; i < 24; ++i) {
    const auto mid = 0.5f * (lo + hi);
    if (cubicBezier(mid, x0, x1, x2, x3) < x) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return cubicBezier(0.5f * (lo + hi), y0, y1, y2, y3);
}

// AOSP interpolator/trim_start_interpolator.xml: L0.5,0 C 0.7,0 0.6,1 1,1
float trimStartInterpolator(float t) {
  if (t <= 0.5f) {
    return 0.0f;
  }
  return cubicYForX(t, 0.5f, 0.0f, 0.7f, 0.0f, 0.6f, 1.0f, 1.0f, 1.0f);
}

// AOSP interpolator/trim_end_interpolator.xml: C0.2,0 0.1,1 0.5,1 L1,1
float trimEndInterpolator(float t) {
  if (t >= 0.5f) {
    return 1.0f;
  }
  return cubicYForX(t, 0.0f, 0.0f, 0.2f, 0.0f, 0.1f, 1.0f, 0.5f, 1.0f);
}

void paintActivityIndicator(
    SkCanvas& canvas,
    const SceneNode& node,
    const SkRect& bounds,
    std::int64_t millis) {
  if (!node.activityIndicator) {
    return;
  }
  if (!node.activityIndicatorAnimating &&
      node.activityIndicatorHidesWhenStopped) {
    return;
  }
  const auto color = sceneColor(
      node.activityIndicatorRed,
      node.activityIndicatorGreen,
      node.activityIndicatorBlue,
      node.activityIndicatorAlpha,
      node.opacity);
  if (node.activityIndicatorHorizontal) {
    const auto barHeight = std::max(4.0f, bounds.height() * 0.45f);
    const SkRect track = SkRect::MakeLTRB(
        bounds.left(),
        bounds.centerY() - barHeight * 0.5f,
        bounds.right(),
        bounds.centerY() + barHeight * 0.5f);
    const auto radius = barHeight * 0.5f;
    SkPaint trackPaint;
    trackPaint.setAntiAlias(true);
    trackPaint.setColor(SkColorSetA(color, SkColorGetA(color) / 4));
    canvas.drawRoundRect(track, radius, radius, trackPaint);
    SkPaint fillPaint;
    fillPaint.setAntiAlias(true);
    fillPaint.setColor(color);
    if (node.activityIndicatorAnimating &&
        node.activityIndicatorProgress <= 0) {
      const auto cycle =
          static_cast<float>(positiveMod(millis, 1400)) / 1400.0f;
      const auto block = std::max(8.0f, track.width() * 0.3f);
      const auto x =
          track.left() + (track.width() + block) * cycle - block;
      canvas.save();
      canvas.clipRect(track);
      canvas.drawRoundRect(
          SkRect::MakeXYWH(x, track.top(), block, track.height()),
          radius,
          radius,
          fillPaint);
      canvas.restore();
    } else {
      const auto width = track.width() *
          std::clamp(node.activityIndicatorProgress, 0.0f, 1.0f);
      if (width > 0) {
        canvas.drawRoundRect(
            SkRect::MakeXYWH(track.left(), track.top(), width, track.height()),
            radius,
            radius,
            fillPaint);
      }
    }
    return;
  }
  const auto size = std::min(bounds.width(), bounds.height());
  // vector_drawable_progress_bar_medium.xml: 48dp viewport, r=18, stroke=4.
  const auto stroke = std::max(1.0f, size * (4.0f / 48.0f));
  const auto radius = size * (18.0f / 48.0f);
  const auto oval = SkRect::MakeLTRB(
      bounds.centerX() - radius,
      bounds.centerY() - radius,
      bounds.centerX() + radius,
      bounds.centerY() + radius);
  float startFraction = 0.0f;
  float sweep = 270.0f;
  float rotation = 0.0f;
  if (node.activityIndicatorAnimating) {
    // progress_indeterminate_material.xml: trimPath{Start,End} 0->0.75 in
    // 1333ms plus trimPathOffset 0->0.25. Rotation of the root group is
    // 0->720deg in 4444ms (progress_indeterminate_rotation_material.xml).
    const auto trimT =
        static_cast<float>(positiveMod(millis, 1333)) / 1333.0f;
    const auto trimStart = trimStartInterpolator(trimT) * 0.75f;
    const auto trimEnd = trimEndInterpolator(trimT) * 0.75f;
    const auto trimOffset = trimT * 0.25f;
    startFraction = trimStart + trimOffset;
    sweep = std::max((trimEnd - trimStart) * 360.0f, 0.0f);
    rotation =
        static_cast<float>(positiveMod(millis, 4444)) / 4444.0f * 720.0f;
  }
  if (sweep <= 0.0f) {
    return;
  }
  // Path starts at 12 o'clock and runs clockwise (same as the AOSP vector).
  const auto startAngle = -90.0f + startFraction * 360.0f + rotation;
  SkPaint spinner;
  spinner.setAntiAlias(true);
  spinner.setStyle(SkPaint::kStroke_Style);
  spinner.setStrokeWidth(stroke);
  spinner.setStrokeCap(SkPaint::kSquare_Cap);
  spinner.setColor(color);
  canvas.drawArc(oval, startAngle, sweep, false, spinner);
}

void nodeCornerRadii(const SceneNode& node, SkVector radii[4]) {
  auto topLeftX = node.borderRadiusTopLeftX;
  auto topLeftY = node.borderRadiusTopLeftY;
  auto topRightX = node.borderRadiusTopRightX;
  auto topRightY = node.borderRadiusTopRightY;
  auto bottomRightX = node.borderRadiusBottomRightX;
  auto bottomRightY = node.borderRadiusBottomRightY;
  auto bottomLeftX = node.borderRadiusBottomLeftX;
  auto bottomLeftY = node.borderRadiusBottomLeftY;
  if (topLeftX <= 0 && topLeftY <= 0) {
    topLeftX = topLeftY = node.borderRadiusTopLeft;
  }
  if (topRightX <= 0 && topRightY <= 0) {
    topRightX = topRightY = node.borderRadiusTopRight;
  }
  if (bottomRightX <= 0 && bottomRightY <= 0) {
    bottomRightX = bottomRightY = node.borderRadiusBottomRight;
  }
  if (bottomLeftX <= 0 && bottomLeftY <= 0) {
    bottomLeftX = bottomLeftY = node.borderRadiusBottomLeft;
  }
  if (topLeftX <= 0 && topRightX <= 0 && bottomRightX <= 0 &&
      bottomLeftX <= 0 && node.borderRadius > 0) {
    topLeftX = topLeftY = topRightX = topRightY = bottomRightX =
        bottomRightY = bottomLeftX = bottomLeftY = node.borderRadius;
  }
  radii[0] = {topLeftX, topLeftY};
  radii[1] = {topRightX, topRightY};
  radii[2] = {bottomRightX, bottomRightY};
  radii[3] = {bottomLeftX, bottomLeftY};
}

SkRRect nodeRoundRect(const SceneNode& node, const SkRect& bounds) {
  SkVector radii[4];
  nodeCornerRadii(node, radii);
  SkRRect rrect;
  rrect.setRectRadii(bounds, radii);
  return rrect;
}

float adjustRadiusForSpread(float radius, float spread) {
  if (spread == 0.0f) {
    return std::max(0.0f, radius);
  }
  const auto absSpread = std::abs(spread);
  const auto spreadMultiplier =
      radius < absSpread
          ? 1.0f +
              std::pow(radius / std::max(absSpread, 0.0001f) - 1.0f, 3.0f)
          : 1.0f;
  return std::max(0.0f, radius + spread * spreadMultiplier);
}

SkRRect spreadRoundRect(
    const SceneNode& node,
    const SkRect& bounds,
    float spread) {
  SkVector radii[4];
  nodeCornerRadii(node, radii);
  for (auto& radius : radii) {
    radius.fX = adjustRadiusForSpread(radius.fX, spread);
    radius.fY = adjustRadiusForSpread(radius.fY, spread);
  }
  SkRRect rrect;
  rrect.setRectRadii(bounds, radii);
  return rrect;
}

SkRect paddingBox(const SceneNode& node, const SkRect& bounds) {
  return SkRect::MakeLTRB(
      bounds.left() + node.borderLeft,
      bounds.top() + node.borderTop,
      bounds.right() - node.borderRight,
      bounds.bottom() - node.borderBottom);
}

SceneNode innerBorderNode(const SceneNode& node) {
  SceneNode inner = node;
  SkVector radii[4];
  nodeCornerRadii(node, radii);
  inner.borderRadiusTopLeftX = std::max(0.0f, radii[0].fX - node.borderLeft);
  inner.borderRadiusTopLeftY = std::max(0.0f, radii[0].fY - node.borderTop);
  inner.borderRadiusTopRightX = std::max(0.0f, radii[1].fX - node.borderRight);
  inner.borderRadiusTopRightY = std::max(0.0f, radii[1].fY - node.borderTop);
  inner.borderRadiusBottomRightX =
      std::max(0.0f, radii[2].fX - node.borderRight);
  inner.borderRadiusBottomRightY =
      std::max(0.0f, radii[2].fY - node.borderBottom);
  inner.borderRadiusBottomLeftX =
      std::max(0.0f, radii[3].fX - node.borderLeft);
  inner.borderRadiusBottomLeftY =
      std::max(0.0f, radii[3].fY - node.borderBottom);
  inner.borderRadiusTopLeft =
      std::min(inner.borderRadiusTopLeftX, inner.borderRadiusTopLeftY);
  inner.borderRadiusTopRight =
      std::min(inner.borderRadiusTopRightX, inner.borderRadiusTopRightY);
  inner.borderRadiusBottomRight =
      std::min(inner.borderRadiusBottomRightX, inner.borderRadiusBottomRightY);
  inner.borderRadiusBottomLeft =
      std::min(inner.borderRadiusBottomLeftX, inner.borderRadiusBottomLeftY);
  inner.borderRadius = inner.borderRadiusTopLeft;
  return inner;
}

// Android BackgroundStyleApplicator.clipToPaddingBox: overflow:hidden clips
// the padding box (inside borders) with inner radii, not the border box.
SkRRect overflowClipRRect(const SceneNode& node, const SkRect& bounds) {
  const auto pad = paddingBox(node, bounds);
  if (pad.width() < 0.5f || pad.height() < 0.5f) {
    return nodeRoundRect(node, bounds);
  }
  return nodeRoundRect(innerBorderNode(node), pad);
}

// BorderDrawable.getEllipseIntersectionWithLine: inner trapezoid corner
// lies on the inner radius ellipse, not at the inner-rect vertex.
SkPoint ellipseLineHit(
    const SkRect& ellipse,
    SkPoint lineStart,
    SkPoint lineEnd,
    SkPoint fallback) {
  const double cx =
      (static_cast<double>(ellipse.left()) + ellipse.right()) * 0.5;
  const double cy =
      (static_cast<double>(ellipse.top()) + ellipse.bottom()) * 0.5;
  double x1 = static_cast<double>(lineStart.x()) - cx;
  double y1 = static_cast<double>(lineStart.y()) - cy;
  double x2 = static_cast<double>(lineEnd.x()) - cx;
  double y2 = static_cast<double>(lineEnd.y()) - cy;
  const double a =
      std::abs(static_cast<double>(ellipse.right()) - ellipse.left()) * 0.5;
  const double b =
      std::abs(static_cast<double>(ellipse.bottom()) - ellipse.top()) * 0.5;
  if (a < 0.001 || b < 0.001 || std::abs(x2 - x1) < 1e-9) {
    return fallback;
  }
  const double m = (y2 - y1) / (x2 - x1);
  const double c = y1 - m * x1;
  const double A = b * b + a * a * m * m;
  const double B = 2.0 * a * a * c * m;
  const double C = a * a * (c * c - b * b);
  if (std::abs(A) < 1e-12) {
    return fallback;
  }
  const double inside = -C / A + (B / (2.0 * A)) * (B / (2.0 * A));
  if (inside < 0.0) {
    return fallback;
  }
  const double D = std::sqrt(inside);
  const double x = -B / (2.0 * A) - D;
  const double y = m * x + c;
  if (!std::isfinite(x) || !std::isfinite(y)) {
    return fallback;
  }
  return SkPoint::Make(
      static_cast<float>(x + cx), static_cast<float>(y + cy));
}

void innerBorderCorners(
    const SceneNode& node,
    const SkRect& bounds,
    SkPoint corners[4]) {
  const auto inner = paddingBox(node, bounds);
  corners[0] = SkPoint::Make(inner.left(), inner.top());
  corners[1] = SkPoint::Make(inner.right(), inner.top());
  corners[2] = SkPoint::Make(inner.right(), inner.bottom());
  corners[3] = SkPoint::Make(inner.left(), inner.bottom());
  const auto innerNode = innerBorderNode(node);
  SkVector radii[4];
  nodeCornerRadii(innerNode, radii);
  if (radii[0].fX > 0.5f && radii[0].fY > 0.5f) {
    corners[0] = ellipseLineHit(
        SkRect::MakeXYWH(
            inner.left(), inner.top(), radii[0].fX * 2, radii[0].fY * 2),
        SkPoint::Make(bounds.left(), bounds.top()),
        SkPoint::Make(inner.left(), inner.top()),
        corners[0]);
  }
  if (radii[1].fX > 0.5f && radii[1].fY > 0.5f) {
    corners[1] = ellipseLineHit(
        SkRect::MakeLTRB(
            inner.right() - radii[1].fX * 2,
            inner.top(),
            inner.right(),
            inner.top() + radii[1].fY * 2),
        SkPoint::Make(bounds.right(), bounds.top()),
        SkPoint::Make(inner.right(), inner.top()),
        corners[1]);
  }
  if (radii[2].fX > 0.5f && radii[2].fY > 0.5f) {
    corners[2] = ellipseLineHit(
        SkRect::MakeLTRB(
            inner.right() - radii[2].fX * 2,
            inner.bottom() - radii[2].fY * 2,
            inner.right(),
            inner.bottom()),
        SkPoint::Make(bounds.right(), bounds.bottom()),
        SkPoint::Make(inner.right(), inner.bottom()),
        corners[2]);
  }
  if (radii[3].fX > 0.5f && radii[3].fY > 0.5f) {
    corners[3] = ellipseLineHit(
        SkRect::MakeLTRB(
            inner.left(),
            inner.bottom() - radii[3].fY * 2,
            inner.left() + radii[3].fX * 2,
            inner.bottom()),
        SkPoint::Make(bounds.left(), bounds.bottom()),
        SkPoint::Make(inner.left(), inner.bottom()),
        corners[3]);
  }
}

void applyNodeTransform(
    SkCanvas& canvas,
    const SceneNode& node,
    const SkPoint& origin) {
  if (!node.hasTransform) {
    return;
  }
  // Project T(center)*M*T(-center) onto z=0. When perspective is present,
  // Android decomposes the 4x4 and reapplies camera+rotateX/Y (depth) or
  // scale+rotateZ (skewY+perspective diamond).
  const auto homography =
      nodePivotTransform(node, origin.x(), origin.y());
  SkMatrix matrix;
  matrix.setAll(
      homography.a,
      homography.c,
      homography.tx,
      homography.b,
      homography.d,
      homography.ty,
      homography.p0,
      homography.p1,
      homography.p2);
  canvas.concat(matrix);
}

bool nodeFacesBack(const SceneNode& node) {
  if (!node.backfaceHidden || !node.hasTransform) {
    return false;
  }
  return node.transformM[0] * node.transformM[5] -
          node.transformM[1] * node.transformM[4] <
      0.0f;
}

void paintBoxShadows(
    SkCanvas& canvas,
    const SceneNode& node,
    const SkRect& bounds,
    float opacity,
    bool inset) {
  const auto layers = !node.boxShadows.empty()
      ? node.boxShadows
      : (node.hasBoxShadow
             ? std::vector<SceneNode::BoxShadowLayer>{{
                   node.boxShadowOffsetX,
                   node.boxShadowOffsetY,
                   node.boxShadowBlur,
                   node.boxShadowSpread,
                   node.boxShadowRed,
                   node.boxShadowGreen,
                   node.boxShadowBlue,
                   node.boxShadowAlpha,
                   node.boxShadowInset,
               }}
             : std::vector<SceneNode::BoxShadowLayer>{});
  // CSS box-shadow is front-to-back: the first specified shadow paints on top.
  for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
    const auto& shadow = *it;
    if (shadow.inset != inset || shadow.alpha <= 0.001f) {
      continue;
    }
    const auto sigma = std::clamp(shadow.blur * 0.5f, 0.0f, 32.0f);
    const auto color = sceneColor(
        shadow.red, shadow.green, shadow.blue, shadow.alpha, opacity);
    SkPaint fill;
    fill.setAntiAlias(true);
    fill.setColor(color);
    if (sigma > 0) {
      // Android Inset/OutsetBoxShadowDrawable uses BlurMaskFilter so the
      // shadow shape is blurred as coverage, then clipped. ImageFilter
      // would blur after the clip and empty a spread=0 inset ring.
      fill.setMaskFilter(
          SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma));
    }
    if (inset) {
      // InsetBoxShadowDrawable: clip to the padding rrect, then
      // drawDoubleRoundRect(outer = padding ± blurExtent, inner =
      // padding inset/offset with spread-adjusted radii). MaskFilter
      // blurs the ring as coverage so spread=0 still has an inward
      // penumbra. saveLayer+DstIn leaks a filled square on CPU raster.
      const auto pad = paddingBox(node, bounds);
      if (pad.width() <= 0.5f || pad.height() <= 0.5f) {
        continue;
      }
      const auto padNode = innerBorderNode(node);
      auto inner = pad;
      if (2.0f * shadow.spread >= pad.width() ||
          2.0f * shadow.spread >= pad.height()) {
        inner = SkRect::MakeEmpty();
      } else {
        inner.inset(shadow.spread, shadow.spread);
        inner.offset(shadow.offsetX, shadow.offsetY);
      }
      const auto blurExtent = sigma > 0 ? sigma * 3.0f + 1.0f : 0.0f;
      auto outer = pad.makeOutset(blurExtent, blurExtent);
      outer.join(inner);
      canvas.save();
      canvas.clipRRect(nodeRoundRect(padNode, pad), true);
      if (inner.width() > 0.5f && inner.height() > 0.5f) {
        canvas.drawDRRect(
            SkRRect::MakeRect(outer),
            spreadRoundRect(padNode, inner, -shadow.spread),
            fill);
      } else {
        canvas.drawRect(outer, fill);
      }
      canvas.restore();
      continue;
    }
    auto silhouette = bounds.makeOutset(shadow.spread, shadow.spread);
    silhouette.offset(shadow.offsetX, shadow.offsetY);
    if (silhouette.width() <= 0.5f || silhouette.height() <= 0.5f) {
      continue;
    }
    canvas.save();
    canvas.clipRRect(
        nodeRoundRect(node, bounds), SkClipOp::kDifference, true);
    canvas.drawRRect(
        spreadRoundRect(node, silhouette, shadow.spread), fill);
    canvas.restore();
  }
}

void paintDecodedImage(
    SkCanvas& canvas,
    const SkImage& image,
    const SceneNode& node,
    const SkRect& bounds) {
  const auto imageWidth = static_cast<float>(image.width());
  const auto imageHeight = static_cast<float>(image.height());
  if (imageWidth <= 0 || imageHeight <= 0 || bounds.isEmpty()) {
    return;
  }
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setAlphaf(std::clamp(node.opacity, 0.0f, 1.0f));
  if (node.hasImageTint) {
    paint.setColorFilter(SkColorFilters::Blend(
        sceneColor(
            node.imageTintRed,
            node.imageTintGreen,
            node.imageTintBlue,
            node.imageTintAlpha,
            1.0f),
        SkBlendMode::kSrcIn));
  }
  if (node.imageBlurRadius > 0) {
    const auto sigma = std::clamp(node.imageBlurRadius * 0.5f, 0.0f, 32.0f);
    paint.setImageFilter(SkImageFilters::Blur(sigma, sigma, nullptr));
  }
  const auto sampling = SkSamplingOptions(SkFilterMode::kLinear);
  const auto mode = node.imageResizeMode;
  if (mode == "repeat") {
    auto shader = image.makeShader(
        SkTileMode::kRepeat, SkTileMode::kRepeat, sampling);
    paint.setShader(std::move(shader));
    canvas.save();
    canvas.clipRect(bounds);
    canvas.translate(bounds.left(), bounds.top());
    canvas.drawPaint(paint);
    canvas.restore();
    return;
  }
  SkRect src = SkRect::MakeWH(imageWidth, imageHeight);
  SkRect dst = bounds;
  const auto widthScale = bounds.width() / imageWidth;
  const auto heightScale = bounds.height() / imageHeight;
  if (mode == "cover") {
    const auto scale = std::max(widthScale, heightScale);
    const auto scaledW = imageWidth * scale;
    const auto scaledH = imageHeight * scale;
    const auto cropX = (scaledW - bounds.width()) / scale * 0.5f;
    const auto cropY = (scaledH - bounds.height()) / scale * 0.5f;
    src = SkRect::MakeXYWH(
        cropX, cropY, imageWidth - cropX * 2, imageHeight - cropY * 2);
  } else if (mode == "contain") {
    const auto scale = std::min(widthScale, heightScale);
    const auto scaledW = imageWidth * scale;
    const auto scaledH = imageHeight * scale;
    dst = SkRect::MakeXYWH(
        bounds.centerX() - scaledW * 0.5f,
        bounds.centerY() - scaledH * 0.5f,
        scaledW,
        scaledH);
  } else if (mode == "center") {
    if (imageWidth <= bounds.width() && imageHeight <= bounds.height()) {
      dst = SkRect::MakeXYWH(
          bounds.centerX() - imageWidth * 0.5f,
          bounds.centerY() - imageHeight * 0.5f,
          imageWidth,
          imageHeight);
    } else {
      const auto scale = std::min(widthScale, heightScale);
      const auto scaledW = imageWidth * scale;
      const auto scaledH = imageHeight * scale;
      dst = SkRect::MakeXYWH(
          bounds.centerX() - scaledW * 0.5f,
          bounds.centerY() - scaledH * 0.5f,
          scaledW,
          scaledH);
    }
  } else if (mode == "none") {
    dst = SkRect::MakeXYWH(
        bounds.left(), bounds.top(), imageWidth, imageHeight);
    canvas.save();
    canvas.clipRect(bounds);
    canvas.drawImageRect(&image, src, dst, sampling, &paint, SkCanvas::kFast_SrcRectConstraint);
    canvas.restore();
    return;
  }
  canvas.drawImageRect(
      &image, src, dst, sampling, &paint, SkCanvas::kFast_SrcRectConstraint);
}

void paintAndroidSwitch(
    SkCanvas& canvas,
    const SceneNode& node,
    const SkRect& bounds) {
  if (!node.androidSwitch) {
    return;
  }
  const auto enabledAlpha = node.androidSwitchEnabled ? node.opacity : node.opacity * 0.5f;
  const auto trackColor = node.hasSwitchTrackColor
      ? sceneColor(
            node.switchTrackRed,
            node.switchTrackGreen,
            node.switchTrackBlue,
            node.switchTrackAlpha,
            enabledAlpha)
      : sceneColor(
            node.androidSwitchOn ? 0.22745098f : 0.62f,
            node.androidSwitchOn ? 0.5137255f : 0.62f,
            node.androidSwitchOn ? 0.46666667f : 0.62f,
            1.0f,
            enabledAlpha);
  const auto thumbColor = node.hasSwitchThumbColor
      ? sceneColor(
            node.switchThumbRed,
            node.switchThumbGreen,
            node.switchThumbBlue,
            node.switchThumbAlpha,
            enabledAlpha)
      : sceneColor(1, 1, 1, 1, enabledAlpha);
  // SwitchCompat in a 52×32 view: 20dp thumb on a 14×32dp track. Thumb
  // elevation uses the same Ambient+Spot mapping as View elevation.
  constexpr float kRefW = 52.0f;
  constexpr float kRefH = 32.0f;
  constexpr float kThumbD = 20.0f;
  constexpr float kTrackW = 32.0f;
  constexpr float kTrackH = 14.0f;
  constexpr float kThumbElevation = 4.0f;
  const auto scale =
      std::min(bounds.width() / kRefW, bounds.height() / kRefH);
  const auto thumbRadius = std::max(8.0f, 0.5f * kThumbD * scale);
  const auto trackHeight = std::max(8.0f, kTrackH * scale);
  const auto trackWidth = std::max(thumbRadius * 2.0f, kTrackW * scale);
  const auto elevation = kThumbElevation * scale;
  const auto leftPad =
      std::min(elevation, std::max(0.0f, bounds.width() - trackWidth));
  const auto track = SkRect::MakeLTRB(
      bounds.left() + leftPad,
      bounds.centerY() - trackHeight * 0.5f,
      bounds.left() + leftPad + trackWidth,
      bounds.centerY() + trackHeight * 0.5f);
  SkPaint trackPaint;
  trackPaint.setAntiAlias(true);
  trackPaint.setColor(trackColor);
  canvas.drawRoundRect(
      track, trackHeight * 0.5f, trackHeight * 0.5f, trackPaint);
  const auto thumbX = node.androidSwitchOn
      ? track.right() - thumbRadius
      : track.left() + thumbRadius;
  const auto thumbY = bounds.centerY();
  const auto sigma = std::clamp(elevation * 0.5f, 0.0f, 32.0f);
  if (sigma > 0 && enabledAlpha > 0.001f) {
    SkPaint ambient;
    ambient.setAntiAlias(true);
    ambient.setColor(sceneColor(0, 0, 0, 0.12f, enabledAlpha));
    ambient.setMaskFilter(
        SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma));
    canvas.drawCircle(thumbX, thumbY, thumbRadius, ambient);
    SkPaint spot;
    spot.setAntiAlias(true);
    spot.setColor(sceneColor(0, 0, 0, 0.24f, enabledAlpha));
    spot.setMaskFilter(
        SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, sigma));
    canvas.drawCircle(
        thumbX, thumbY + elevation * 0.5f, thumbRadius, spot);
  }
  SkPaint thumbPaint;
  thumbPaint.setAntiAlias(true);
  thumbPaint.setColor(thumbColor);
  canvas.drawCircle(thumbX, thumbY, thumbRadius, thumbPaint);
}

SkBlendMode skiaBlendMode(const std::string& name) {
  if (name == "multiply") {
    return SkBlendMode::kMultiply;
  }
  if (name == "screen") {
    return SkBlendMode::kScreen;
  }
  if (name == "overlay") {
    return SkBlendMode::kOverlay;
  }
  if (name == "darken") {
    return SkBlendMode::kDarken;
  }
  if (name == "lighten") {
    return SkBlendMode::kLighten;
  }
  if (name == "color-dodge") {
    return SkBlendMode::kColorDodge;
  }
  if (name == "color-burn") {
    return SkBlendMode::kColorBurn;
  }
  if (name == "hard-light") {
    return SkBlendMode::kHardLight;
  }
  if (name == "soft-light") {
    return SkBlendMode::kSoftLight;
  }
  if (name == "difference") {
    return SkBlendMode::kDifference;
  }
  if (name == "exclusion") {
    return SkBlendMode::kExclusion;
  }
  if (name == "hue") {
    return SkBlendMode::kHue;
  }
  if (name == "saturation") {
    return SkBlendMode::kSaturation;
  }
  if (name == "color") {
    return SkBlendMode::kColor;
  }
  if (name == "luminosity") {
    return SkBlendMode::kLuminosity;
  }
  if (name == "plus-lighter") {
    return SkBlendMode::kPlus;
  }
  return SkBlendMode::kSrcOver;
}

sk_sp<SkColorFilter> colorFilterForOp(const SceneNode::FilterOp& op) {
  const auto& type = op.type;
  const auto t = op.amount;
  SkColorMatrix matrix;
  if (type == "brightness") {
    matrix.setScale(t, t, t, 1);
  } else if (type == "contrast") {
    matrix.setScale(t, t, t, 1);
    const auto offset = 0.5f * (1.0f - t);
    matrix.postTranslate(offset, offset, offset, 0);
  } else if (type == "grayscale") {
    const auto g = std::clamp(t, 0.0f, 1.0f);
    const float r = 0.2126f * g;
    const float gg = 0.7152f * g;
    const float b = 0.0722f * g;
    const float m[20] = {
        1 - g + r, gg, b, 0, 0, r, 1 - g + gg, b, 0, 0, r, gg, 1 - g + b, 0, 0,
        0, 0, 0, 1, 0};
    matrix.setRowMajor(m);
  } else if (type == "saturate") {
    matrix.setSaturation(t);
  } else if (type == "sepia") {
    const auto s = std::clamp(t, 0.0f, 1.0f);
    matrix = SkColorMatrix(
        1 - 0.607f * s,
        0.769f * s,
        0.189f * s,
        0,
        0,
        0.349f * s,
        1 - 0.314f * s,
        0.168f * s,
        0,
        0,
        0.272f * s,
        0.534f * s,
        1 - 0.869f * s,
        0,
        0,
        0,
        0,
        0,
        1,
        0);
  } else if (type == "invert") {
    const auto s = std::clamp(t, 0.0f, 1.0f);
    matrix = SkColorMatrix(
        1 - 2 * s, 0, 0, 0, s, 0, 1 - 2 * s, 0, 0, s, 0, 0, 1 - 2 * s, 0, s, 0,
        0, 0, 1, 0);
  } else if (type == "hueRotate") {
    const auto radians = t * 3.14159265f / 180.0f;
    const auto c = std::cos(radians);
    const auto s = std::sin(radians);
    matrix = SkColorMatrix(
        0.213f + 0.787f * c - 0.213f * s,
        0.715f - 0.715f * c - 0.715f * s,
        0.072f - 0.072f * c + 0.928f * s,
        0,
        0,
        0.213f - 0.213f * c + 0.143f * s,
        0.715f + 0.285f * c + 0.140f * s,
        0.072f - 0.072f * c - 0.283f * s,
        0,
        0,
        0.213f - 0.213f * c - 0.787f * s,
        0.715f - 0.715f * c + 0.715f * s,
        0.072f + 0.928f * c + 0.072f * s,
        0,
        0,
        0,
        0,
        0,
        1,
        0);
  } else {
    return nullptr;
  }
  return SkColorFilters::Matrix(matrix);
}

sk_sp<SkImageFilter> imageFilterForOp(const SceneNode::FilterOp& op) {
  if (op.type == "blur") {
    if (op.amount <= 0) {
      return nullptr;
    }
    const auto sigma = std::clamp(op.amount * 0.5f, 0.0f, 32.0f);
    return SkImageFilters::Blur(sigma, sigma, nullptr);
  }
  if (op.type == "dropShadow") {
    // CSS stdDeviation is treated as blur radius; sigma is half that, matching
    // the blur() filter mapping above.
    const auto sigma =
        std::clamp(op.dropShadowStdDev * 0.5f, 0.0f, 32.0f);
    return SkImageFilters::DropShadow(
        op.dropShadowOffsetX,
        op.dropShadowOffsetY,
        sigma,
        sigma,
        sceneColor(
            op.dropShadowRed,
            op.dropShadowGreen,
            op.dropShadowBlue,
            op.dropShadowAlpha,
            1.0f),
        nullptr);
  }
  return nullptr;
}

std::vector<SceneNode::FilterOp> viewEffectOps(const SceneNode& node) {
  if (!node.filters.empty()) {
    return node.filters;
  }
  std::vector<SceneNode::FilterOp> ops;
  const auto add = [&](const char* type, float amount, bool active) {
    if (!active) {
      return;
    }
    SceneNode::FilterOp op;
    op.type = type;
    op.amount = amount;
    ops.push_back(std::move(op));
  };
  add("blur", node.filterBlur, node.filterBlur > 0);
  add("brightness", node.filterBrightness, node.filterBrightness != 1.0f);
  add("contrast", node.filterContrast, node.filterContrast != 1.0f);
  add("grayscale", node.filterGrayscale, node.filterGrayscale != 0.0f);
  add("saturate", node.filterSaturate, node.filterSaturate != 1.0f);
  add("sepia", node.filterSepia, node.filterSepia != 0.0f);
  add("invert", node.filterInvert, node.filterInvert != 0.0f);
  add("hueRotate", node.filterHueRotate, node.filterHueRotate != 0.0f);
  return ops;
}

void applyViewEffectsToPaint(SkPaint& paint, const SceneNode& node) {
  if (!node.mixBlendMode.empty()) {
    paint.setBlendMode(skiaBlendMode(node.mixBlendMode));
  }
  const auto ops = viewEffectOps(node);
  sk_sp<SkColorFilter> colorFilter;
  sk_sp<SkImageFilter> imageFilter;
  for (const auto& op : ops) {
    if (auto next = colorFilterForOp(op)) {
      colorFilter = colorFilter
          ? SkColorFilters::Compose(next, colorFilter)
          : std::move(next);
    }
    if (auto next = imageFilterForOp(op)) {
      imageFilter = imageFilter
          ? SkImageFilters::Compose(std::move(next), std::move(imageFilter))
          : std::move(next);
    }
  }
  if (colorFilter) {
    paint.setColorFilter(std::move(colorFilter));
  }
  if (imageFilter) {
    paint.setImageFilter(std::move(imageFilter));
  }
}

sk_sp<SkShader> makeBackgroundLayerShader(
    const SceneNode::BackgroundImageLayer& layer,
    const SkRect& bounds,
    float opacity) {
  if (layer.stops.empty() || bounds.isEmpty()) {
    return nullptr;
  }
  std::vector<SkColor4f> colors;
  std::vector<float> positions;
  colors.reserve(layer.stops.size());
  positions.reserve(layer.stops.size());
  const auto paintOpacity = std::clamp(opacity, 0.0f, 1.0f);
  for (const auto& stop : layer.stops) {
    colors.push_back(SkColor4f{
        stop.red,
        stop.green,
        stop.blue,
        std::clamp(stop.alpha * paintOpacity, 0.0f, 1.0f)});
    positions.push_back(std::clamp(stop.offset, 0.0f, 1.0f));
  }
  if (colors.size() == 1) {
    colors.push_back(colors.front());
    positions.push_back(1.0f);
  }
  for (std::size_t i = 1; i < positions.size(); ++i) {
    if (positions[i] <= positions[i - 1]) {
      positions[i] = std::min(1.0f, positions[i - 1] + 1.0e-4f);
    }
  }
  const SkGradient::Colors spec(
      SkSpan<const SkColor4f>(colors.data(), colors.size()),
      SkSpan<const float>(positions.data(), positions.size()),
      SkTileMode::kClamp);
  const SkGradient grad(spec, SkGradient::Interpolation{});
  if (layer.radial) {
    const auto cx = bounds.left() + bounds.width() * layer.x0;
    const auto cy = bounds.top() + bounds.height() * layer.y0;
    if (layer.ellipse) {
      const auto rx = std::max(bounds.width() * 0.5f, 0.001f);
      const auto ry = std::max(bounds.height() * 0.5f, 0.001f);
      SkMatrix local;
      local.setScale(rx, ry);
      local.postTranslate(cx, cy);
      return SkShaders::RadialGradient(
          SkPoint::Make(0, 0), 1.0f, grad, &local);
    }
    const auto dx = std::max(
        std::abs(cx - bounds.left()), std::abs(bounds.right() - cx));
    const auto dy = std::max(
        std::abs(cy - bounds.top()), std::abs(bounds.bottom() - cy));
    const auto radius = std::max(std::sqrt(dx * dx + dy * dy), 0.001f);
    return SkShaders::RadialGradient(SkPoint::Make(cx, cy), radius, grad);
  }
  SkPoint pts[2] = {
      SkPoint::Make(
          bounds.left() + bounds.width() * layer.x0,
          bounds.top() + bounds.height() * layer.y0),
      SkPoint::Make(
          bounds.left() + bounds.width() * layer.x1,
          bounds.top() + bounds.height() * layer.y1),
  };
  if (pts[0] == pts[1]) {
    pts[1].fY += 1.0f;
  }
  return SkShaders::LinearGradient(pts, grad);
}

void paintBackgroundGradient(
    SkCanvas& canvas,
    const SceneNode& node,
    const SkRect& bounds) {
  std::vector<SceneNode::BackgroundImageLayer> layers = node.backgroundImageLayers;
  if (layers.empty() && node.hasBackgroundGradient) {
    SceneNode::BackgroundImageLayer layer;
    layer.radial = node.backgroundGradientRadial;
    layer.x0 = node.backgroundGradientX0;
    layer.y0 = node.backgroundGradientY0;
    layer.x1 = node.backgroundGradientX1;
    layer.y1 = node.backgroundGradientY1;
    layer.stops.push_back(
        {0,
         node.backgroundGradientR0,
         node.backgroundGradientG0,
         node.backgroundGradientB0,
         node.backgroundGradientA0});
    layer.stops.push_back(
        {1,
         node.backgroundGradientR1,
         node.backgroundGradientG1,
         node.backgroundGradientB1,
         node.backgroundGradientA1});
    layers.push_back(std::move(layer));
  }
  if (layers.empty()) {
    return;
  }
  canvas.save();
  canvas.clipRRect(nodeRoundRect(node, bounds), true);
  for (const auto& layer : layers) {
    auto shader = makeBackgroundLayerShader(layer, bounds, node.opacity);
    if (!shader) {
      continue;
    }
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setShader(std::move(shader));
    applyViewEffectsToPaint(paint, node);
    canvas.drawRect(bounds, paint);
  }
  canvas.restore();
}

SkColor sceneColor(
    float red,
    float green,
    float blue,
    float alpha,
    float opacity) {
  const auto channel = [](float value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255);
  };
  return SkColorSetARGB(
      channel(alpha * std::clamp(opacity, 0.0f, 1.0f)),
      channel(red),
      channel(green),
      channel(blue));
}

struct Point {
  float x{0};
  float y{0};
};

Point presentationOrigin(
    const SceneNode& node,
    const std::unordered_map<int, const SceneNode*>& byTag) {
  Point origin{node.absoluteX, node.absoluteY};
  const SceneNode* current = &node;
  while (current->parentTag) {
    if (current->modalHost) {
      break;
    }
    const auto parent = byTag.find(*current->parentTag);
    if (parent == byTag.end()) {
      break;
    }
    if (parent->second->scrollable) {
      origin.x -= parent->second->scrollOffsetX;
      origin.y -= parent->second->scrollOffsetY;
    }
    current = parent->second;
    if (current->modalHost) {
      break;
    }
  }
  return origin;
}

sk_sp<SkImage> decodeEncodedImage(sk_sp<const SkData> encoded) {
  if (!encoded) {
    return nullptr;
  }
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
  // The current Skia bootstrap encodes PNG but does not register a PNG
  // decoder. macOS ImageIO already covers Metro PNG/JPEG/GIF assets.
  if (auto generator = SkImageGeneratorCG::MakeFromEncodedCG(encoded)) {
    return SkImages::DeferredFromGenerator(std::move(generator));
  }
#endif
  return SkImages::DeferredFromEncodedData(std::move(encoded));
}

void applyAncestorTransformsAndClips(
    SkCanvas& canvas,
    const SceneNode& node,
    const std::unordered_map<int, const SceneNode*>& byTag) {
  std::vector<const SceneNode*> chain;
  const SceneNode* current = &node;
  chain.push_back(current);
  while (current->parentTag) {
    if (current->modalHost) {
      break;
    }
    const auto parent = byTag.find(*current->parentTag);
    if (parent == byTag.end()) {
      break;
    }
    chain.push_back(parent->second);
    current = parent->second;
    if (current->modalHost) {
      break;
    }
  }
  for (int index = static_cast<int>(chain.size()) - 1; index >= 0; --index) {
    const auto* ancestor = chain[static_cast<std::size_t>(index)];
    const auto origin = presentationOrigin(*ancestor, byTag);
    applyNodeTransform(
        canvas, *ancestor, SkPoint::Make(origin.x, origin.y));
    if (index == 0) {
      continue;
    }
    bool underInlineAttachment = false;
    for (int descendant = 0; descendant <= index; ++descendant) {
      if (chain[static_cast<std::size_t>(descendant)]->inlineAttachment) {
        underInlineAttachment = true;
        break;
      }
    }
    // Android Text overflow:hidden clips glyphs, not ReplacementSpan hosts.
    const bool textOverflowClip = ancestor->clipsContentToBounds &&
        underInlineAttachment &&
        (ancestor->preparedText ||
         ancestor->componentName == "Paragraph" ||
         ancestor->componentName == "Text");
    if ((ancestor->scrollable || ancestor->clipsContentToBounds) &&
        !textOverflowClip) {
      const auto clipBounds = SkRect::MakeXYWH(
          origin.x, origin.y, ancestor->width, ancestor->height);
      canvas.clipRRect(
          overflowClipRRect(*ancestor, clipBounds),
          SkClipOp::kIntersect,
          true);
    }
  }
}

SceneSnapshot sceneFromMetrics(const folly::dynamic& metrics) {
  SceneSnapshot scene;
  if (const auto* viewport = field(metrics, "viewport")) {
    scene.viewportWidth = numberValue(*viewport, "width");
    scene.viewportHeight = numberValue(*viewport, "height");
    scene.pointScaleFactor = numberValue(*viewport, "pointScaleFactor", 1.0);
  }
  const auto* mountedTree = field(metrics, "mountedViewTree");
  if (mountedTree == nullptr) {
    return scene;
  }
  scene.surfaceId = numberValue(*mountedTree, "surfaceId");
  scene.revision = numberValue(*mountedTree, "revision");
  scene.rootTag = numberValue(*mountedTree, "rootTag");
  const auto* nodes = field(*mountedTree, "nodes");
  if (nodes == nullptr || !nodes->isArray()) {
    return scene;
  }
  scene.nodes.reserve(nodes->size());
  for (const auto& source : *nodes) {
    SceneNode node;
    node.tag = tagOf(source);
    node.childIndex = static_cast<std::size_t>(numberValue(source, "index"));
    node.componentName = stringValue(source, "componentName");
    if (const auto* parent = field(source, "parentTag");
        parent != nullptr && parent->isNumber()) {
      node.parentTag = parent->asInt();
    }
    node.layoutable = booleanValue(source, "layoutable");
    if (const auto* layout = layoutOf(source)) {
      node.x = numberValue(*layout, "x");
      node.y = numberValue(*layout, "y");
      node.width = numberValue(*layout, "width");
      node.height = numberValue(*layout, "height");
      node.absoluteX = numberValue(*layout, "absoluteX");
      node.absoluteY = numberValue(*layout, "absoluteY");
      node.display = stringValue(*layout, "display", "flex");
      if (const auto* insets = field(*layout, "contentInsets");
          insets != nullptr && insets->isObject()) {
        node.contentInsetTop = numberValue(*insets, "top");
        node.contentInsetRight = numberValue(*insets, "right");
        node.contentInsetBottom = numberValue(*insets, "bottom");
        node.contentInsetLeft = numberValue(*insets, "left");
      }
      if (const auto* border = field(*layout, "borderWidth");
          border != nullptr && border->isObject()) {
        node.borderTop = numberValue(*border, "top");
        node.borderRight = numberValue(*border, "right");
        node.borderBottom = numberValue(*border, "bottom");
        node.borderLeft = numberValue(*border, "left");
      }
    }
    if (const auto* props = field(source, "props")) {
      node.opacity = numberValue(*props, "opacity", 1.0);
      if (const auto* transform = field(*props, "transform");
          transform != nullptr && transform->isObject()) {
        node.hasTransform = booleanValue(*transform, "hasTransform");
        if (const auto* matrix = field(*transform, "matrix");
            matrix != nullptr && matrix->isArray() && matrix->size() == 16) {
          for (int index = 0; index < 16; ++index) {
            const auto& value = (*matrix)[static_cast<std::size_t>(index)];
            node.transformM[index] =
                value.isNumber() ? static_cast<float>(value.asDouble()) : 0.0f;
          }
        } else {
          node.transformM[0] = numberValue(*transform, "a", 1.0);
          node.transformM[1] = numberValue(*transform, "b");
          node.transformM[4] = numberValue(*transform, "c");
          node.transformM[5] = numberValue(*transform, "d", 1.0);
          node.transformM[12] = numberValue(*transform, "tx");
          node.transformM[13] = numberValue(*transform, "ty");
          node.transformM[3] = numberValue(*transform, "p0");
          node.transformM[7] = numberValue(*transform, "p1");
          node.transformM[15] = numberValue(*transform, "p2", 1.0);
        }
      }
      node.borderRadius = numberValue(*props, "borderRadius");
      if (const auto* radii = field(*props, "borderRadii");
          radii != nullptr && radii->isObject()) {
        readCornerRadius(
            *radii,
            "topLeft",
            node.borderRadiusTopLeft,
            node.borderRadiusTopLeftX,
            node.borderRadiusTopLeftY);
        readCornerRadius(
            *radii,
            "topRight",
            node.borderRadiusTopRight,
            node.borderRadiusTopRightX,
            node.borderRadiusTopRightY);
        readCornerRadius(
            *radii,
            "bottomRight",
            node.borderRadiusBottomRight,
            node.borderRadiusBottomRightX,
            node.borderRadiusBottomRightY);
        readCornerRadius(
            *radii,
            "bottomLeft",
            node.borderRadiusBottomLeft,
            node.borderRadiusBottomLeftX,
            node.borderRadiusBottomLeftY);
      }
      if (const auto* zIndex = field(*props, "zIndex");
          zIndex != nullptr && zIndex->isNumber()) {
        node.zIndex = zIndex->asInt();
      }
      node.pointerEvents = stringValue(*props, "pointerEvents", "auto");
      node.clipsContentToBounds =
          booleanValue(*props, "clipsContentToBounds");
      if (const auto* hitSlop = field(*props, "hitSlop");
          hitSlop != nullptr && hitSlop->isObject()) {
        node.hitSlopTop = numberValue(*hitSlop, "top");
        node.hitSlopRight = numberValue(*hitSlop, "right");
        node.hitSlopBottom = numberValue(*hitSlop, "bottom");
        node.hitSlopLeft = numberValue(*hitSlop, "left");
      }
      node.text = stringValue(*props, "text");
      node.fontSize = numberValue(*props, "fontSize");
      node.fontWeight = numberValue(*props, "fontWeight", 400);
      node.fontFamily = stringValue(*props, "fontFamily");
      node.subpixelText = booleanValue(*props, "subpixelText");
      node.includeFontPadding =
          booleanValue(*props, "includeFontPadding", true);
      node.textAlignVertical =
          static_cast<int>(numberValue(*props, "textAlignVertical"));
      if (const auto* color = field(*props, "backgroundColor");
          color != nullptr && color->isObject()) {
        node.hasBackgroundColor = true;
        node.backgroundRed = numberValue(*color, "red");
        node.backgroundGreen = numberValue(*color, "green");
        node.backgroundBlue = numberValue(*color, "blue");
        node.backgroundAlpha = numberValue(*color, "alpha", 1.0);
      }
      if (const auto* color = field(*props, "borderColor");
          color != nullptr && color->isObject()) {
        readSceneColor(
            *color,
            node.hasBorderColor,
            node.borderRed,
            node.borderGreen,
            node.borderBlue,
            node.borderAlpha);
      }
      if (const auto* colors = field(*props, "borderColors");
          colors != nullptr && colors->isObject()) {
        if (const auto* color = field(*colors, "top");
            color != nullptr && color->isObject()) {
          readSceneColor(
              *color,
              node.hasBorderTopColor,
              node.borderTopRed,
              node.borderTopGreen,
              node.borderTopBlue,
              node.borderTopAlpha);
        }
        if (const auto* color = field(*colors, "right");
            color != nullptr && color->isObject()) {
          readSceneColor(
              *color,
              node.hasBorderRightColor,
              node.borderRightRed,
              node.borderRightGreen,
              node.borderRightBlue,
              node.borderRightAlpha);
        }
        if (const auto* color = field(*colors, "bottom");
            color != nullptr && color->isObject()) {
          readSceneColor(
              *color,
              node.hasBorderBottomColor,
              node.borderBottomRed,
              node.borderBottomGreen,
              node.borderBottomBlue,
              node.borderBottomAlpha);
        }
        if (const auto* color = field(*colors, "left");
            color != nullptr && color->isObject()) {
          readSceneColor(
              *color,
              node.hasBorderLeftColor,
              node.borderLeftRed,
              node.borderLeftGreen,
              node.borderLeftBlue,
              node.borderLeftAlpha);
        }
        node.hasBorderColor = node.hasBorderColor || node.hasBorderTopColor ||
            node.hasBorderRightColor || node.hasBorderBottomColor ||
            node.hasBorderLeftColor;
      }
      if (const auto* styles = field(*props, "borderStyles");
          styles != nullptr && styles->isObject()) {
        node.borderStyleTop = stringValue(*styles, "top", "solid");
        node.borderStyleRight = stringValue(*styles, "right", "solid");
        node.borderStyleBottom = stringValue(*styles, "bottom", "solid");
        node.borderStyleLeft = stringValue(*styles, "left", "solid");
      }
      if (const auto* layers = field(*props, "boxShadows");
          layers != nullptr && layers->isArray()) {
        for (const auto& shadow : *layers) {
          if (shadow.isObject()) {
            node.boxShadows.push_back(readBoxShadowLayer(shadow));
          }
        }
      }
      if (node.boxShadows.empty()) {
        if (const auto* shadow = field(*props, "boxShadow");
            shadow != nullptr && shadow->isObject()) {
          node.boxShadows.push_back(readBoxShadowLayer(*shadow));
        }
      }
      syncPrimaryBoxShadow(node);
      node.backfaceHidden = booleanValue(*props, "backfaceHidden");
      node.needsOffscreenAlphaCompositing =
          booleanValue(*props, "needsOffscreenAlphaCompositing");
      if (const auto* ripple = field(*props, "nativeRipple");
          ripple != nullptr && ripple->isObject()) {
        node.nativeRipple = true;
        node.nativeRipplePressed = booleanValue(*ripple, "pressed");
        node.nativeRippleBorderless = booleanValue(*ripple, "borderless");
        if (const auto* color = field(*ripple, "color");
            color != nullptr && color->isObject()) {
          node.nativeRippleRed = numberValue(*color, "red");
          node.nativeRippleGreen = numberValue(*color, "green");
          node.nativeRippleBlue = numberValue(*color, "blue");
          node.nativeRippleAlpha = numberValue(*color, "alpha", 0.2);
        }
      }
      if (const auto* outline = field(*props, "outline");
          outline != nullptr && outline->isObject()) {
        node.outlineWidth = numberValue(*outline, "width");
        node.outlineOffset = numberValue(*outline, "offset");
        node.outlineStyle = stringValue(*outline, "style", "solid");
        if (const auto* color = field(*outline, "color");
            color != nullptr && color->isObject()) {
          readSceneColor(
              *color,
              node.hasOutlineColor,
              node.outlineRed,
              node.outlineGreen,
              node.outlineBlue,
              node.outlineAlpha);
        }
      }
      if (const auto* color = field(*props, "textColor");
          color != nullptr && color->isObject()) {
        node.hasTextColor = true;
        node.textRed = numberValue(*color, "red");
        node.textGreen = numberValue(*color, "green");
        node.textBlue = numberValue(*color, "blue");
        node.textAlpha = numberValue(*color, "alpha", 1.0);
      }
      if (const auto* indicator = field(*props, "activityIndicator");
          indicator != nullptr && indicator->isObject()) {
        node.activityIndicator = true;
        node.activityIndicatorAnimating =
            booleanValue(*indicator, "animating", true);
        node.activityIndicatorHidesWhenStopped =
            booleanValue(*indicator, "hidesWhenStopped", true);
        node.activityIndicatorHorizontal =
            booleanValue(*indicator, "horizontal");
        node.activityIndicatorProgress =
            numberValue(*indicator, "progress");
        if (const auto* color = field(*indicator, "color");
            color != nullptr && color->isObject()) {
          node.hasActivityIndicatorColor = true;
          node.activityIndicatorRed = numberValue(*color, "red");
          node.activityIndicatorGreen = numberValue(*color, "green");
          node.activityIndicatorBlue = numberValue(*color, "blue");
          node.activityIndicatorAlpha = numberValue(*color, "alpha", 1.0);
        }
      }
      if (const auto* scroll = field(*props, "scroll");
          scroll != nullptr && scroll->isObject()) {
        node.scrollable = true;
        node.scrollOffsetX = numberValue(*scroll, "offsetX");
        node.scrollOffsetY = numberValue(*scroll, "offsetY");
      }
      if (const auto* input = field(*props, "textInput");
          input != nullptr && input->isObject()) {
        node.textInput = true;
        node.editable = booleanValue(*input, "editable", true);
        node.multiline = booleanValue(*input, "multiline");
        node.focused = booleanValue(*input, "focused");
        node.placeholder = stringValue(*input, "placeholder");
        node.selectionStart = numberValue(*input, "selectionStart");
        node.selectionEnd = numberValue(*input, "selectionEnd");
      }
      node.imageUri = stringValue(*props, "imageUri");
      node.imagePath = stringValue(*props, "imagePath");
      node.imageResizeMode =
          stringValue(*props, "imageResizeMode", "stretch");
      if (const auto* tint = field(*props, "imageTint");
          tint != nullptr && tint->isObject()) {
        node.hasImageTint = true;
        node.imageTintRed = numberValue(*tint, "red");
        node.imageTintGreen = numberValue(*tint, "green");
        node.imageTintBlue = numberValue(*tint, "blue");
        node.imageTintAlpha = numberValue(*tint, "alpha", 1.0);
      }
      if (const auto* toggle = field(*props, "androidSwitch");
          toggle != nullptr && toggle->isObject()) {
        node.androidSwitch = true;
        node.androidSwitchOn = booleanValue(*toggle, "on");
        node.androidSwitchEnabled = booleanValue(*toggle, "enabled", true);
        if (const auto* thumb = field(*toggle, "thumbColor");
            thumb != nullptr && thumb->isObject()) {
          node.hasSwitchThumbColor = true;
          node.switchThumbRed = numberValue(*thumb, "red");
          node.switchThumbGreen = numberValue(*thumb, "green");
          node.switchThumbBlue = numberValue(*thumb, "blue");
          node.switchThumbAlpha = numberValue(*thumb, "alpha", 1.0);
        }
        if (const auto* track = field(*toggle, "trackColor");
            track != nullptr && track->isObject()) {
          node.hasSwitchTrackColor = true;
          node.switchTrackRed = numberValue(*track, "red");
          node.switchTrackGreen = numberValue(*track, "green");
          node.switchTrackBlue = numberValue(*track, "blue");
          node.switchTrackAlpha = numberValue(*track, "alpha", 1.0);
        }
      }
      if (const auto* modal = field(*props, "modal");
          modal != nullptr && modal->isObject()) {
        node.modalHost = true;
        node.modalTransparent = booleanValue(*modal, "transparent");
      }
    }
    scene.nodes.push_back(std::move(node));
  }
  return scene;
}

SceneSnapshot sceneFromWire(const folly::dynamic& wire) {
  SceneSnapshot scene;
  if (!wire.isObject() || numberValue(wire, "schemaVersion") != 1) {
    return scene;
  }
  scene.surfaceId = numberValue(wire, "surfaceId");
  scene.revision = numberValue(wire, "revision");
  scene.rootTag = numberValue(wire, "rootTag");
  if (const auto* viewport = field(wire, "viewport")) {
    scene.viewportWidth = numberValue(*viewport, "width");
    scene.viewportHeight = numberValue(*viewport, "height");
    scene.pointScaleFactor = numberValue(*viewport, "pointScaleFactor", 1.0);
  }
  if (const auto* errors = field(wire, "mountingErrors");
      errors != nullptr && errors->isArray()) {
    for (const auto& error : *errors) {
      if (error.isString()) {
        scene.mountingErrors.push_back(error.asString());
      }
    }
  }
  const auto* nodes = field(wire, "nodes");
  if (nodes == nullptr || !nodes->isArray()) {
    return scene;
  }
  const auto* viewport = field(wire, "viewport");
  folly::dynamic viewportValue = viewport != nullptr
      ? *viewport
      : folly::dynamic::object();
  folly::dynamic metrics = folly::dynamic::object
      ("viewport", std::move(viewportValue))
      ("mountedViewTree", folly::dynamic::object
          ("surfaceId", scene.surfaceId)
          ("revision", scene.revision)
          ("rootTag", scene.rootTag)
          ("nodes", *nodes));
  auto decoded = sceneFromMetrics(metrics);
  decoded.surfaceId = scene.surfaceId;
  decoded.revision = scene.revision;
  decoded.rootTag = scene.rootTag;
  decoded.viewportWidth = scene.viewportWidth;
  decoded.viewportHeight = scene.viewportHeight;
  decoded.pointScaleFactor = scene.pointScaleFactor;
  decoded.mountingErrors = std::move(scene.mountingErrors);
  return decoded;
}

} // namespace

class SkiaMountedTreeRenderer::Impl {
 public:
  explicit Impl(const std::filesystem::path& fontDirectory)
      : textLayoutEngine_(
            fontDirectory,
            fontDirectory.empty()
                ? TextFontPlatform::Generic
                : TextFontPlatform::Android) {}

  SkiaRenderedFrame render(const folly::dynamic& metrics) {
    return render(sceneFromMetrics(metrics), std::nullopt);
  }

  SkiaRenderedFrame renderSceneWire(const folly::dynamic& wire) {
    if (!wire.isObject()) {
      return {.error = "scene wire payload must be an object"};
    }
    const auto* schemaVersion = field(wire, "schemaVersion");
    if (schemaVersion == nullptr || !schemaVersion->isInt() ||
        schemaVersion->asInt() != 1) {
      return {.error = "unsupported scene wire schemaVersion"};
    }
    const auto* viewport = field(wire, "viewport");
    if (viewport == nullptr || !viewport->isObject()) {
      return {.error = "scene wire payload is missing viewport"};
    }
    const auto* nodes = field(wire, "nodes");
    if (nodes == nullptr || !nodes->isArray()) {
      return {.error = "scene wire payload is missing nodes"};
    }
    return render(sceneFromWire(wire), std::nullopt);
  }

  SkiaRenderedFrame render(
      const SceneSnapshot& scene,
      std::optional<std::int64_t> animationTimeMs) {
    const auto animationMillis = animationTimeMs.value_or(wallClockMs());
    if (!scene.mountingErrors.empty()) {
      return {.error = "scene contains mounting errors: " +
              scene.mountingErrors.front()};
    }
    if (scene.nodes.empty()) {
      return {.error = "mountedViewTree has no nodes"};
    }
    if (!std::isfinite(scene.pointScaleFactor) ||
        scene.pointScaleFactor <= 0) {
      return {.error = "scene pointScaleFactor must be finite and positive"};
    }

    std::unordered_map<int, const SceneNode*> byTag;
    byTag.reserve(scene.nodes.size());
    for (const auto& node : scene.nodes) {
      if (!byTag.emplace(node.tag, &node).second) {
        return {.error = "scene contains duplicate tag " +
                std::to_string(node.tag)};
      }
    }
    const auto rootEntry = byTag.find(scene.rootTag);
    if (rootEntry == byTag.end()) {
      return {.error = "scene rootTag does not identify a node"};
    }
    const auto* root = rootEntry->second;
    if (root->parentTag) {
      return {.error = "scene root node must not have a parent"};
    }
    if (!root->layoutable || !std::isfinite(root->width) ||
        !std::isfinite(root->height) || root->width <= 0 ||
        root->height <= 0) {
      return {.error = "mountedViewTree root has no layout"};
    }
    for (const auto& node : scene.nodes) {
      if (node.tag != scene.rootTag && !node.parentTag) {
        return {.error = "scene contains a second root at tag " +
                std::to_string(node.tag)};
      }
      std::unordered_set<int> ancestors;
      auto* current = &node;
      while (current->parentTag) {
        if (!ancestors.emplace(current->tag).second) {
          return {.error = "scene contains a parent cycle at tag " +
                  std::to_string(current->tag)};
        }
        const auto parent = byTag.find(*current->parentTag);
        if (parent == byTag.end()) {
          return {.error = "scene node " + std::to_string(current->tag) +
                  " references missing parent " +
                  std::to_string(*current->parentTag)};
        }
        current = parent->second;
      }
      if (current->tag != scene.rootTag) {
        return {.error = "scene node " + std::to_string(node.tag) +
                " is disconnected from root"};
      }
    }

    std::unordered_map<int, std::vector<const SceneNode*>> children;
    for (const auto& node : scene.nodes) {
      if (node.parentTag) {
        children[*node.parentTag].push_back(&node);
      }
    }
    for (auto& [_, siblings] : children) {
      std::stable_sort(
          siblings.begin(), siblings.end(),
          [](const SceneNode* left, const SceneNode* right) {
            // childIndex is Fabric's mount index after flattening and
            // orderIndex sort (static first, zIndex ignored on static).
            // Re-sorting by props.zIndex would paint a flattened parent
            // over its static children (ZIndex With Static).
            return left->childIndex < right->childIndex;
          });
    }
    std::vector<const SceneNode*> paintOrder;
    std::vector<const SceneNode*> modalHosts;
    const auto appendPaintOrder = [&](const auto& self,
                                      const SceneNode& node,
                                      bool insideModal) -> void {
      if (node.modalHost && !insideModal) {
        modalHosts.push_back(&node);
        return;
      }
      paintOrder.push_back(&node);
      const auto found = children.find(node.tag);
      if (found != children.end()) {
        for (const auto* child : found->second) {
          self(self, *child, insideModal || node.modalHost);
        }
      }
    };
    appendPaintOrder(appendPaintOrder, *root, false);
    for (const auto* modal : modalHosts) {
      appendPaintOrder(appendPaintOrder, *modal, true);
    }

    const auto density = scene.pointScaleFactor;
    const auto logicalWidth = root->width;
    const auto logicalHeight = root->height;
    const auto pixelWidth = std::max(1, static_cast<int>(std::lround(logicalWidth * density)));
    const auto pixelHeight = std::max(1, static_cast<int>(std::lround(logicalHeight * density)));
    const auto info = SkImageInfo::Make(
        pixelWidth,
        pixelHeight,
        kRGBA_8888_SkColorType,
        kPremul_SkAlphaType);
    if (!rasterSurface_ || rasterWidth_ != pixelWidth ||
        rasterHeight_ != pixelHeight) {
      rasterSurface_ = SkSurfaces::Raster(info);
      rasterWidth_ = pixelWidth;
      rasterHeight_ = pixelHeight;
    }
    auto surface = rasterSurface_;
    if (surface == nullptr) {
      return {.error = "Skia failed to allocate raster surface"};
    }
    auto* canvas = surface->getCanvas();
    // Reused surfaces keep the previous CTM/clip. Scale-without-reset
    // compounded density each Interact revision and emptied the device.
    canvas->restoreToCount(1);
    canvas->save();
    canvas->clear(SK_ColorTRANSPARENT);
    canvas->scale(density, density);
    canvas->clipRect(SkRect::MakeWH(logicalWidth, logicalHeight));

    const auto isDescendantOf = [&](const SceneNode& node, int ancestorTag) {
      const SceneNode* current = &node;
      while (current->parentTag) {
        if (*current->parentTag == ancestorTag) {
          return true;
        }
        const auto parent = byTag.find(*current->parentTag);
        if (parent == byTag.end()) {
          break;
        }
        current = parent->second;
      }
      return false;
    };
    const auto inheritedPaintOpacity = [&](const SceneNode& node) {
      if (node.needsOffscreenAlphaCompositing) {
        return 1.0f;
      }
      float opacity = node.opacity;
      const SceneNode* current = &node;
      while (current->parentTag) {
        if (current->modalHost) {
          break;
        }
        const auto parent = byTag.find(*current->parentTag);
        if (parent == byTag.end()) {
          break;
        }
        if (parent->second->needsOffscreenAlphaCompositing) {
          break;
        }
        opacity *= parent->second->opacity;
        current = parent->second;
        if (current->modalHost) {
          break;
        }
      }
      return std::clamp(opacity, 0.0f, 1.0f);
    };

    std::vector<int> offscreenLayers;
    int paintedNodes = 0;
    int culledNodes = 0;
    int paintedText = 0;
    int preparedOnPaint = 0;
    const auto rasterStarted = std::chrono::steady_clock::now();
    for (const auto* nodePointer : paintOrder) {
      const auto& node = *nodePointer;
      while (!offscreenLayers.empty() &&
             node.tag != offscreenLayers.back() &&
             !isDescendantOf(node, offscreenLayers.back())) {
        canvas->restore();
        offscreenLayers.pop_back();
      }
      if (!node.layoutable || node.display == "none" ||
          node.componentName == "RawText") {
        continue;
      }
      bool hiddenByBackface = nodeFacesBack(node);
      if (!hiddenByBackface) {
        const SceneNode* ancestor = &node;
        while (!hiddenByBackface && ancestor->parentTag) {
          const auto parent = byTag.find(*ancestor->parentTag);
          if (parent == byTag.end()) {
            break;
          }
          ancestor = parent->second;
          hiddenByBackface = nodeFacesBack(*ancestor);
        }
      }
      if (hiddenByBackface) {
        continue;
      }
      const auto width = node.width;
      const auto height = node.height;
      if (width <= 0 || height <= 0) {
        continue;
      }
      const auto paintOpacity = inheritedPaintOpacity(node);
      if (paintOpacity <= 0.001f) {
        continue;
      }
      const auto origin = presentationOrigin(node, byTag);
      const SkRect nodeBounds =
          SkRect::MakeXYWH(origin.x, origin.y, width, height);
      bool underTransform = node.hasTransform;
      {
        const SceneNode* current = &node;
        while (!underTransform && current->parentTag) {
          const auto parent = byTag.find(*current->parentTag);
          if (parent == byTag.end()) {
            break;
          }
          underTransform = parent->second->hasTransform;
          current = parent->second;
        }
      }
      if (!underTransform &&
          !nodeBounds.makeOutset(64, 64).intersects(
              SkRect::MakeWH(logicalWidth, logicalHeight))) {
        ++culledNodes;
        continue;
      }
      ++paintedNodes;
      if (node.needsOffscreenAlphaCompositing || node.isolationIsolate) {
        if (node.needsOffscreenAlphaCompositing) {
          SkPaint layerPaint;
          float layerAlpha = node.opacity;
          const SceneNode* current = &node;
          while (current->parentTag) {
            const auto parent = byTag.find(*current->parentTag);
            if (parent == byTag.end() ||
                parent->second->needsOffscreenAlphaCompositing) {
              break;
            }
            layerAlpha *= parent->second->opacity;
            current = parent->second;
          }
          layerPaint.setAlphaf(std::clamp(layerAlpha, 0.0f, 1.0f));
          canvas->saveLayer(nullptr, &layerPaint);
        } else {
          canvas->saveLayer(nullptr, nullptr);
        }
        offscreenLayers.push_back(node.tag);
      }

      canvas->save();
      applyAncestorTransformsAndClips(*canvas, node, byTag);
      const SkRect bounds =
          SkRect::MakeXYWH(origin.x, origin.y, width, height);
      SceneNode painted = node;
      painted.opacity = paintOpacity;
      if (painted.hasBoxShadow || !painted.boxShadows.empty()) {
        paintBoxShadows(*canvas, painted, bounds, painted.opacity, false);
      }
      auto decodeCached = [&](const std::string& path) -> sk_sp<SkImage> {
        if (path.empty()) {
          return nullptr;
        }
        auto cached = decodedImages_.find(path);
        if (cached == decodedImages_.end()) {
          sk_sp<SkImage> decoded;
          if (auto encoded = SkData::MakeFromFileName(path.c_str())) {
            decoded = decodeEncodedImage(std::move(encoded));
          }
          cached = decodedImages_.emplace(path, std::move(decoded)).first;
        }
        return cached->second;
      };
      sk_sp<SkImage> decodedImage = decodeCached(node.imagePath);
      if (!decodedImage) {
        decodedImage = decodeCached(node.imageDefaultPath);
      }
      if (node.modalHost) {
        const SkRect dialog =
            SkRect::MakeWH(logicalWidth, logicalHeight);
        SkPaint fill;
        fill.setAntiAlias(true);
        if (node.modalTransparent) {
          fill.setColor(SkColorSetARGB(
              static_cast<U8CPU>(
                  std::clamp(painted.opacity, 0.0f, 1.0f) * 128.0f),
              0,
              0,
              0));
        } else {
          const float red =
              node.hasBackgroundColor ? node.backgroundRed : 1.0f;
          const float green =
              node.hasBackgroundColor ? node.backgroundGreen : 1.0f;
          const float blue =
              node.hasBackgroundColor ? node.backgroundBlue : 1.0f;
          const float alpha =
              node.hasBackgroundColor ? node.backgroundAlpha : 1.0f;
          fill.setColor(
              sceneColor(red, green, blue, alpha, painted.opacity));
        }
        canvas->drawRect(dialog, fill);
      } else if (decodedImage) {
        paintDecodedImage(*canvas, *decodedImage, painted, bounds);
      } else if (
          node.hasBackgroundColor || node.hasBackgroundGradient ||
          !node.backgroundImageLayers.empty()) {
        if (node.hasBackgroundColor) {
          SkPaint paint;
          paint.setAntiAlias(true);
          paint.setColor(sceneColor(
              node.backgroundRed,
              node.backgroundGreen,
              node.backgroundBlue,
              node.backgroundAlpha,
              painted.opacity));
          applyViewEffectsToPaint(paint, node);
          canvas->drawRRect(nodeRoundRect(node, bounds), paint);
        }
        paintBackgroundGradient(*canvas, painted, bounds);
      } else if (node.componentName == "Switch") {
        SkPaint fill;
        fill.setAntiAlias(true);
        fill.setColor(SkColorSetARGB(
            static_cast<U8CPU>(std::clamp(node.opacity, 0.0f, 1.0f) * 56.0f),
            118,
            128,
            142));
        canvas->drawRRect(nodeRoundRect(node, bounds), fill);
        SkPaint stroke;
        stroke.setAntiAlias(true);
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(1);
        stroke.setColor(SkColorSetARGB(
            static_cast<U8CPU>(std::clamp(node.opacity, 0.0f, 1.0f) * 180.0f),
            72,
            84,
            98));
        canvas->drawRRect(nodeRoundRect(node, bounds), stroke);
      }
      paintAndroidSwitch(*canvas, node, bounds);
      paintActivityIndicator(*canvas, node, bounds, animationMillis);
      if (node.swipeRefresh && node.swipeRefreshing) {
        SceneNode spinner = node;
        spinner.activityIndicator = true;
        spinner.activityIndicatorAnimating = true;
        spinner.activityIndicatorHidesWhenStopped = false;
        spinner.activityIndicatorHorizontal = false;
        spinner.activityIndicatorRed = node.swipeRefreshRed;
        spinner.activityIndicatorGreen = node.swipeRefreshGreen;
        spinner.activityIndicatorBlue = node.swipeRefreshBlue;
        spinner.activityIndicatorAlpha = node.swipeRefreshAlpha;
        const auto size = node.swipeRefreshOffset > 0 ? 36.0f : 36.0f;
        const auto spinnerBounds = SkRect::MakeXYWH(
            bounds.centerX() - size * 0.5f,
            bounds.top() + node.swipeRefreshOffset + 8.0f,
            size,
            size);
        paintActivityIndicator(*canvas, spinner, spinnerBounds, animationMillis);
      }
      if (node.drawerLayout && node.drawerOffset > 0) {
        SkPaint dim;
        dim.setAntiAlias(true);
        dim.setColor(SkColorSetARGB(
            static_cast<U8CPU>(
                std::clamp(node.drawerOffset, 0.0f, 1.0f) * 0.45f * 255.0f *
                std::clamp(node.opacity, 0.0f, 1.0f)),
            0,
            0,
            0));
        canvas->drawRect(bounds, dim);
      }
      if (node.hasBorderColor || node.borderTop > 0 || node.borderRight > 0 ||
          node.borderBottom > 0 || node.borderLeft > 0) {
        const auto top = node.borderTop;
        const auto right = node.borderRight;
        const auto bottom = node.borderBottom;
        const auto left = node.borderLeft;
        const auto uniformWidth =
            top > 0 && std::fabs(top - right) < 0.01f &&
            std::fabs(right - bottom) < 0.01f &&
            std::fabs(bottom - left) < 0.01f;
        const auto uniformStyle =
            node.borderStyleTop == node.borderStyleRight &&
            node.borderStyleRight == node.borderStyleBottom &&
            node.borderStyleBottom == node.borderStyleLeft;
        const auto uniformColor =
            (!node.hasBorderTopColor && !node.hasBorderRightColor &&
             !node.hasBorderBottomColor && !node.hasBorderLeftColor) ||
            (sameBorderColor(
                 node.hasBorderTopColor,
                 node.borderTopRed,
                 node.borderTopGreen,
                 node.borderTopBlue,
                 node.borderTopAlpha,
                 node.hasBorderRightColor,
                 node.borderRightRed,
                 node.borderRightGreen,
                 node.borderRightBlue,
                 node.borderRightAlpha) &&
             sameBorderColor(
                 node.hasBorderTopColor,
                 node.borderTopRed,
                 node.borderTopGreen,
                 node.borderTopBlue,
                 node.borderTopAlpha,
                 node.hasBorderBottomColor,
                 node.borderBottomRed,
                 node.borderBottomGreen,
                 node.borderBottomBlue,
                 node.borderBottomAlpha) &&
             sameBorderColor(
                 node.hasBorderTopColor,
                 node.borderTopRed,
                 node.borderTopGreen,
                 node.borderTopBlue,
                 node.borderTopAlpha,
                 node.hasBorderLeftColor,
                 node.borderLeftRed,
                 node.borderLeftGreen,
                 node.borderLeftBlue,
                 node.borderLeftAlpha));
        auto strokeFor = [&](bool has,
                             float red,
                             float green,
                             float blue,
                             float alpha,
                             const std::string& style,
                             float width) {
          SkPaint stroke;
          stroke.setAntiAlias(true);
          stroke.setStyle(SkPaint::kStroke_Style);
          if (has) {
            stroke.setColor(
                sceneColor(red, green, blue, alpha, painted.opacity));
          } else if (node.hasBorderColor) {
            stroke.setColor(sceneColor(
                node.borderRed,
                node.borderGreen,
                node.borderBlue,
                node.borderAlpha,
                painted.opacity));
          } else {
            // Android/CSS default border color is opaque black.
            stroke.setColor(sceneColor(0, 0, 0, 1, painted.opacity));
          }
          applyBorderStyle(stroke, style, width);
          return stroke;
        };
        if (uniformWidth && uniformStyle && uniformColor) {
          auto stroke = strokeFor(
              node.hasBorderTopColor,
              node.borderTopRed,
              node.borderTopGreen,
              node.borderTopBlue,
              node.borderTopAlpha,
              node.borderStyleTop,
              top);
          stroke.setStrokeWidth(top);
          const auto inset = top * 0.5f;
          SceneNode insetNode = node;
          insetNode.borderRadius = std::max(0.0f, node.borderRadius - inset);
          insetNode.borderRadiusTopLeft =
              std::max(0.0f, node.borderRadiusTopLeft - inset);
          insetNode.borderRadiusTopRight =
              std::max(0.0f, node.borderRadiusTopRight - inset);
          insetNode.borderRadiusBottomRight =
              std::max(0.0f, node.borderRadiusBottomRight - inset);
          insetNode.borderRadiusBottomLeft =
              std::max(0.0f, node.borderRadiusBottomLeft - inset);
          insetNode.borderRadiusTopLeftX =
              std::max(0.0f, node.borderRadiusTopLeftX - inset);
          insetNode.borderRadiusTopLeftY =
              std::max(0.0f, node.borderRadiusTopLeftY - inset);
          insetNode.borderRadiusTopRightX =
              std::max(0.0f, node.borderRadiusTopRightX - inset);
          insetNode.borderRadiusTopRightY =
              std::max(0.0f, node.borderRadiusTopRightY - inset);
          insetNode.borderRadiusBottomRightX =
              std::max(0.0f, node.borderRadiusBottomRightX - inset);
          insetNode.borderRadiusBottomRightY =
              std::max(0.0f, node.borderRadiusBottomRightY - inset);
          insetNode.borderRadiusBottomLeftX =
              std::max(0.0f, node.borderRadiusBottomLeftX - inset);
          insetNode.borderRadiusBottomLeftY =
              std::max(0.0f, node.borderRadiusBottomLeftY - inset);
          canvas->drawRRect(
              nodeRoundRect(insetNode, bounds.makeInset(inset, inset)),
              stroke);
        } else {
          // BorderDrawable.drawQuadrilateral: mitered trapezoids so
          // borderBlockColor corners meet on the diagonal. Clip to the
          // outer rrect so partial rounded edges follow the arc.
          const bool rounded =
              node.borderRadius > 0 || node.borderRadiusTopLeft > 0 ||
              node.borderRadiusTopRight > 0 ||
              node.borderRadiusBottomRight > 0 ||
              node.borderRadiusBottomLeft > 0 ||
              node.borderRadiusTopLeftX > 0 ||
              node.borderRadiusTopRightX > 0 ||
              node.borderRadiusBottomRightX > 0 ||
              node.borderRadiusBottomLeftX > 0;
          canvas->save();
          if (rounded) {
            // BorderDrawable: clip O' then clipOut I' so two-sided rounded
            // edges are constant-width strokes, not filled pies.
            canvas->clipRRect(
                nodeRoundRect(node, bounds), SkClipOp::kIntersect, true);
            const auto innerBounds = paddingBox(node, bounds);
            if (innerBounds.width() > 0.5f && innerBounds.height() > 0.5f) {
              canvas->clipRRect(
                  nodeRoundRect(innerBorderNode(node), innerBounds),
                  SkClipOp::kDifference,
                  true);
            }
          }
          auto fillFor = [&](bool has,
                             float red,
                             float green,
                             float blue,
                             float alpha) {
            SkPaint paint;
            paint.setAntiAlias(rounded);
            paint.setStyle(SkPaint::kFill_Style);
            if (has) {
              paint.setColor(
                  sceneColor(red, green, blue, alpha, painted.opacity));
            } else {
              // Unspecified edges are Android/CSS default black, not the
              // first resolved side color (borderBlockColor is only block).
              paint.setColor(sceneColor(0, 0, 0, 1, painted.opacity));
            }
            return paint;
          };
          // BorderDrawable: after clip O' / clipOut I', a single-color
          // rounded border is `drawRect(O)`. Trapezoids miss the inner
          // quarter-ellipse, which is the notch on two-sided rounded edges.
          SkColor visibleColor = SK_ColorTRANSPARENT;
          bool haveVisible = false;
          bool sameVisible = true;
          auto considerSide = [&](float width,
                                  bool has,
                                  float red,
                                  float green,
                                  float blue,
                                  float alpha) {
            if (width <= 0) {
              return;
            }
            const auto color = fillFor(has, red, green, blue, alpha).getColor();
            if (!haveVisible) {
              visibleColor = color;
              haveVisible = true;
            } else if (color != visibleColor) {
              sameVisible = false;
            }
          };
          considerSide(
              left,
              node.hasBorderLeftColor,
              node.borderLeftRed,
              node.borderLeftGreen,
              node.borderLeftBlue,
              node.borderLeftAlpha);
          considerSide(
              top,
              node.hasBorderTopColor,
              node.borderTopRed,
              node.borderTopGreen,
              node.borderTopBlue,
              node.borderTopAlpha);
          considerSide(
              right,
              node.hasBorderRightColor,
              node.borderRightRed,
              node.borderRightGreen,
              node.borderRightBlue,
              node.borderRightAlpha);
          considerSide(
              bottom,
              node.hasBorderBottomColor,
              node.borderBottomRed,
              node.borderBottomGreen,
              node.borderBottomBlue,
              node.borderBottomAlpha);
          const bool fullRing =
              left > 0 && right > 0 && top > 0 && bottom > 0;
          if (rounded && sameVisible && haveVisible && fullRing) {
            SkPaint fill;
            fill.setAntiAlias(true);
            fill.setColor(visibleColor);
            canvas->drawPaint(fill);
          } else {
            const auto L = bounds.left();
            const auto T = bounds.top();
            const auto R = bounds.right();
            const auto Btm = bounds.bottom();
            SkPoint innerCorner[4];
            innerBorderCorners(node, bounds, innerCorner);
            const auto gap = rounded ? 0.8f : 0.0f;
            auto drawQuad = [&](const SkPaint& paint,
                                float x1,
                                float y1,
                                float x2,
                                float y2,
                                float x3,
                                float y3,
                                float x4,
                                float y4) {
              SkPathBuilder path;
              path.moveTo(x1, y1);
              path.lineTo(x2, y2);
              path.lineTo(x3, y3);
              path.lineTo(x4, y4);
              path.close();
              canvas->drawPath(path.detach(), paint);
            };
            if (left > 0) {
              drawQuad(
                  fillFor(
                      node.hasBorderLeftColor,
                      node.borderLeftRed,
                      node.borderLeftGreen,
                      node.borderLeftBlue,
                      node.borderLeftAlpha),
                  L,
                  T - gap,
                  innerCorner[0].x(),
                  innerCorner[0].y() - gap,
                  innerCorner[3].x(),
                  innerCorner[3].y() + gap,
                  L,
                  Btm + gap);
            }
            if (top > 0) {
              drawQuad(
                  fillFor(
                      node.hasBorderTopColor,
                      node.borderTopRed,
                      node.borderTopGreen,
                      node.borderTopBlue,
                      node.borderTopAlpha),
                  L - gap,
                  T,
                  innerCorner[0].x() - gap,
                  innerCorner[0].y(),
                  innerCorner[1].x() + gap,
                  innerCorner[1].y(),
                  R + gap,
                  T);
            }
            if (right > 0) {
              drawQuad(
                  fillFor(
                      node.hasBorderRightColor,
                      node.borderRightRed,
                      node.borderRightGreen,
                      node.borderRightBlue,
                      node.borderRightAlpha),
                  R,
                  T - gap,
                  R,
                  Btm + gap,
                  innerCorner[2].x(),
                  innerCorner[2].y() + gap,
                  innerCorner[1].x(),
                  innerCorner[1].y() - gap);
            }
            if (bottom > 0) {
              drawQuad(
                  fillFor(
                      node.hasBorderBottomColor,
                      node.borderBottomRed,
                      node.borderBottomGreen,
                      node.borderBottomBlue,
                      node.borderBottomAlpha),
                  L - gap,
                  Btm,
                  R + gap,
                  Btm,
                  innerCorner[2].x() + gap,
                  innerCorner[2].y(),
                  innerCorner[3].x() - gap,
                  innerCorner[3].y());
            }
          }
          canvas->restore();
        }
      }
      if (node.outlineWidth > 0 && node.hasOutlineColor) {
        SkPaint outline;
        outline.setAntiAlias(true);
        outline.setStyle(SkPaint::kStroke_Style);
        outline.setColor(sceneColor(
            node.outlineRed,
            node.outlineGreen,
            node.outlineBlue,
            node.outlineAlpha,
            painted.opacity));
        applyOutlineStyle(outline, node.outlineStyle, node.outlineWidth);
        outline.setStrokeWidth(node.outlineWidth);
        // OutlineDrawable: stroke centerline is inset/outset by
        // width/2+offset, radii grow by the same amount, and 0.8px closes
        // the hairline against the border.
        const auto grow =
            node.outlineWidth * 0.5f + node.outlineOffset;
        const auto gap = 0.8f / std::max(density, 0.001f);
        const auto outset = grow - gap;
        canvas->drawRRect(
            nodeRoundRect(
                outlineStrokeNode(node, grow),
                bounds.makeOutset(outset, outset)),
            outline);
      }
      // overflow:hidden clips padding-box content (glyphs, children via
      // ancestor clips). Background and border live on the border box;
      // clipping them first ate RN Tester Text red/blue strokes.
      if (node.clipsContentToBounds) {
        canvas->clipRRect(
            overflowClipRRect(node, bounds), SkClipOp::kIntersect, true);
      }

      const auto insetLeft =
          std::max(node.contentInsetLeft, node.borderLeft);
      const auto insetRight =
          std::max(node.contentInsetRight, node.borderRight);
      const auto insetTop =
          std::max(node.contentInsetTop, node.borderTop);
      const auto insetBottom =
          std::max(node.contentInsetBottom, node.borderBottom);
      const auto textX = origin.x + insetLeft;
      const auto textWidth = std::max(0.0f, width - insetLeft - insetRight);
      const auto innerHeight =
          std::max(0.0f, height - insetTop - insetBottom);
      const auto paragraphContentHeight = [&](float fallbackHeight) {
        if (node.preparedText && !node.preparedText->lines().empty()) {
          const auto& last = node.preparedText->lines().back();
          return last.baseline + last.descent;
        }
        return fallbackHeight;
      };
      const auto alignedTextY = [&](float paragraphHeight) {
        const auto contentHeight = paragraphContentHeight(paragraphHeight);
        auto textY = origin.y + insetTop;
        if (contentHeight + 0.5f < innerHeight) {
          if (node.textAlignVertical == 1 ||
              (node.textAlignVertical == 0 && node.textInput &&
               !node.multiline)) {
            textY += (innerHeight - contentHeight) * 0.5f;
          } else if (node.textAlignVertical == 2) {
            textY += innerHeight - contentHeight;
          }
        }
        return textY;
      };
      const auto paintTextWithOpacity = [&](const auto& draw) {
        if (painted.opacity < 0.999f) {
          SkPaint fade;
          fade.setAlphaf(painted.opacity);
          canvas->saveLayer(nullptr, &fade);
          draw();
          canvas->restore();
        } else {
          draw();
        }
      };
      if (node.preparedText) {
        ++paintedText;
        paintTextWithOpacity([&] {
          node.preparedText->paint(
              *canvas, textX, alignedTextY(node.preparedText->height()));
        });
      } else if (!node.text.empty() ||
                 (node.textInput && !node.placeholder.empty())) {
        TextParagraph paragraph;
        paragraph.maximumNumberOfLines = 1;
        paragraph.ellipsizeMode = TextEllipsizeMode::Clip;
        TextRun run;
        run.text = node.text.empty() ? node.placeholder : node.text;
        run.fontFamily = node.fontFamily;
        run.fontSize = std::max(node.fontSize, 1.0f);
        run.fontWeight = std::clamp(node.fontWeight, 1, 1000);
        run.subpixel = node.subpixelText;
        if (node.hasExplicitLineHeight) {
          run.lineHeight = node.lineHeight;
        }
        if (node.hasTextColor) {
          run.foregroundColor = TextColor{
              node.textRed,
              node.textGreen,
              node.textBlue,
              node.textAlpha};
        }
        paragraph.runs.push_back(std::move(run));
        ++preparedOnPaint;
        ++paintedText;
        const auto prepared = textLayoutEngine_.prepare(paragraph, textWidth);
        paintTextWithOpacity([&] {
          prepared->paint(*canvas, textX, alignedTextY(prepared->height()));
        });
      }
      if (painted.hasBoxShadow || !painted.boxShadows.empty()) {
        paintBoxShadows(*canvas, painted, bounds, painted.opacity, true);
      }
      if (painted.nativeRipple && painted.nativeRipplePressed) {
        SkPaint ripple;
        ripple.setAntiAlias(true);
        ripple.setColor(sceneColor(
            painted.nativeRippleRed,
            painted.nativeRippleGreen,
            painted.nativeRippleBlue,
            painted.nativeRippleAlpha,
            painted.opacity));
        if (painted.nativeRippleBorderless) {
          const auto radius =
              std::sqrt(bounds.width() * bounds.width() +
                        bounds.height() * bounds.height()) *
              0.5f;
          canvas->drawCircle(bounds.centerX(), bounds.centerY(), radius, ripple);
        } else {
          canvas->drawRRect(nodeRoundRect(painted, bounds), ripple);
        }
      }
      canvas->restore();
    }
    while (!offscreenLayers.empty()) {
      canvas->restore();
      offscreenLayers.pop_back();
    }

    if (!scene.statusBarHidden && scene.statusBarHeight > 0 &&
        scene.statusBarAlpha > 0) {
      SkPaint bar;
      bar.setAntiAlias(true);
      bar.setColor(sceneColor(
          scene.statusBarRed,
          scene.statusBarGreen,
          scene.statusBarBlue,
          scene.statusBarAlpha));
      canvas->drawRect(
          SkRect::MakeXYWH(0, 0, logicalWidth, scene.statusBarHeight),
          bar);
    }
    const auto nowMs = animationMillis;
    if (!scene.toastMessage.empty() &&
        (scene.toastUntilMs == 0 || nowMs < scene.toastUntilMs)) {
      TextParagraph paragraph;
      paragraph.maximumNumberOfLines = 2;
      paragraph.ellipsizeMode = TextEllipsizeMode::Tail;
      TextRun run;
      run.text = scene.toastMessage;
      run.fontSize = 14;
      run.fontWeight = 500;
      run.foregroundColor = TextColor{1, 1, 1, 1};
      paragraph.runs.push_back(std::move(run));
      const auto maxWidth = std::max(80.0f, logicalWidth - 48.0f);
      const auto prepared = textLayoutEngine_.prepare(paragraph, maxWidth);
      const auto padX = 16.0f;
      const auto padY = 10.0f;
      const auto toastWidth =
          std::min(maxWidth, prepared->width() + padX * 2);
      const auto toastHeight = prepared->height() + padY * 2;
      float toastX =
          (logicalWidth - toastWidth) * 0.5f + scene.toastOffsetX;
      float toastY = logicalHeight - toastHeight - 48.0f + scene.toastOffsetY;
      if (scene.toastGravity == 49) {
        toastY = 24.0f + scene.statusBarHeight + scene.toastOffsetY;
      } else if (scene.toastGravity == 17) {
        toastY = (logicalHeight - toastHeight) * 0.5f + scene.toastOffsetY;
      }
      SkPaint pill;
      pill.setAntiAlias(true);
      pill.setColor(SkColorSetARGB(220, 48, 48, 48));
      canvas->drawRRect(
          SkRRect::MakeRectXY(
              SkRect::MakeXYWH(toastX, toastY, toastWidth, toastHeight),
              24,
              24),
          pill);
      prepared->paint(*canvas, toastX + padX, toastY + padY);
    }

    canvas->restoreToCount(1);
    if (std::getenv("RNSIM_RASTER_STATS") != nullptr) {
      const auto rasterMs =
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - rasterStarted)
              .count();
      std::fprintf(
          stderr,
          "raster %.1fms nodes=%zu painted=%d culled=%d text=%d "
          "prepareOnPaint=%d px=%dx%d density=%.2f\n",
          rasterMs,
          scene.nodes.size(),
          paintedNodes,
          culledNodes,
          paintedText,
          preparedOnPaint,
          pixelWidth,
          pixelHeight,
          density);
    }

    SkPixmap pixels;
    if (!surface->peekPixels(&pixels)) {
      return {.error = "Skia raster surface did not expose pixels"};
    }
    SkiaRenderedFrame frame;
    frame.width = pixelWidth;
    frame.height = pixelHeight;
    frame.rowBytes = static_cast<std::size_t>(pixelWidth) * 4;
    frame.rgba.resize(frame.rowBytes * static_cast<std::size_t>(pixelHeight));
    for (int row = 0; row < pixelHeight; ++row) {
      std::memcpy(
          frame.rgba.data() + static_cast<std::size_t>(row) * frame.rowBytes,
          static_cast<const std::uint8_t*>(pixels.addr()) +
              static_cast<std::size_t>(row) * pixels.rowBytes(),
          frame.rowBytes);
    }
    return frame;
  }

 private:
  SkiaTextLayoutEngine textLayoutEngine_;
  std::unordered_map<std::string, sk_sp<SkImage>> decodedImages_;
  sk_sp<SkSurface> rasterSurface_;
  int rasterWidth_{0};
  int rasterHeight_{0};
};

SkiaMountedTreeRenderer::SkiaMountedTreeRenderer(
    std::filesystem::path androidFontDirectory)
    : impl_(std::make_unique<Impl>(androidFontDirectory)) {}

SkiaMountedTreeRenderer::~SkiaMountedTreeRenderer() = default;
SkiaMountedTreeRenderer::SkiaMountedTreeRenderer(
    SkiaMountedTreeRenderer&&) noexcept = default;
SkiaMountedTreeRenderer& SkiaMountedTreeRenderer::operator=(
    SkiaMountedTreeRenderer&&) noexcept = default;

SkiaRenderedFrame SkiaMountedTreeRenderer::render(
    const folly::dynamic& metrics) {
  return impl_->render(metrics);
}

SkiaRenderedFrame SkiaMountedTreeRenderer::renderSceneWire(
    const folly::dynamic& sceneWire) {
  return impl_->renderSceneWire(sceneWire);
}

SkiaRenderedFrame SkiaMountedTreeRenderer::render(
    const SceneSnapshot& scene,
    std::optional<std::int64_t> animationTimeMs) {
  return impl_->render(scene, animationTimeMs);
}

} // namespace ReactNativeSimulator
