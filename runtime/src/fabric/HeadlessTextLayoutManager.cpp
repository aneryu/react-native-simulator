#include "HeadlessTextLayoutManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#if RNS_ENABLE_SKIA
#include <react/renderer/graphics/Color.h>
#include <react/utils/FloatComparison.h>
#endif

namespace facebook::react {
namespace {

#if RNS_ENABLE_SKIA
using ReactNativeSimulator::TextColor;
using ReactNativeSimulator::TextDataDetector;
using ReactNativeSimulator::TextDecorationLine;
using ReactNativeSimulator::TextDecorationStyle;
using ReactNativeSimulator::TextEllipsizeMode;
using ReactNativeSimulator::TextHorizontalAlignment;
using ReactNativeSimulator::TextHyphenation;
using ReactNativeSimulator::TextBreakStrategy;
using ReactNativeSimulator::TextParagraph;
using ReactNativeSimulator::TextRun;
using ReactNativeSimulator::TextTransform;
using ReactNativeSimulator::TextWritingDirection;

TextColor textColor(const SharedColor& color) {
  const auto components = colorComponentsFromColor(color);
  return {
      .red = components.red,
      .green = components.green,
      .blue = components.blue,
      .alpha = components.alpha};
}

TextHorizontalAlignment textAlignment(
    std::optional<TextAlignment> alignment) {
  switch (alignment.value_or(TextAlignment::Natural)) {
    case TextAlignment::Left:
      return TextHorizontalAlignment::Left;
    case TextAlignment::Center:
      return TextHorizontalAlignment::Center;
    case TextAlignment::Right:
      return TextHorizontalAlignment::Right;
    case TextAlignment::Justified:
      return TextHorizontalAlignment::Justified;
    case TextAlignment::Start:
      return TextHorizontalAlignment::Start;
    case TextAlignment::End:
      return TextHorizontalAlignment::End;
    case TextAlignment::Natural:
      return TextHorizontalAlignment::Natural;
  }
  return TextHorizontalAlignment::Natural;
}

TextWritingDirection textDirection(
    std::optional<WritingDirection> direction) {
  switch (direction.value_or(WritingDirection::Natural)) {
    case WritingDirection::LeftToRight:
      return TextWritingDirection::LeftToRight;
    case WritingDirection::RightToLeft:
      return TextWritingDirection::RightToLeft;
    case WritingDirection::Natural:
      return TextWritingDirection::Natural;
  }
  return TextWritingDirection::Natural;
}

TextEllipsizeMode ellipsizeMode(EllipsizeMode mode) {
  switch (mode) {
    case EllipsizeMode::Head:
      return TextEllipsizeMode::Head;
    case EllipsizeMode::Tail:
      return TextEllipsizeMode::Tail;
    case EllipsizeMode::Middle:
      return TextEllipsizeMode::Middle;
    case EllipsizeMode::Clip:
      return TextEllipsizeMode::Clip;
  }
  return TextEllipsizeMode::Clip;
}

TextTransform textTransform(std::optional<facebook::react::TextTransform> value) {
  switch (value.value_or(facebook::react::TextTransform::Unset)) {
    case facebook::react::TextTransform::Uppercase:
      return TextTransform::Uppercase;
    case facebook::react::TextTransform::Lowercase:
      return TextTransform::Lowercase;
    case facebook::react::TextTransform::Capitalize:
      return TextTransform::Capitalize;
    case facebook::react::TextTransform::None:
    case facebook::react::TextTransform::Unset:
      return TextTransform::None;
  }
  return TextTransform::None;
}

TextHyphenation textHyphenation(HyphenationFrequency value) {
  switch (value) {
    case HyphenationFrequency::Normal:
      return TextHyphenation::Normal;
    case HyphenationFrequency::Full:
      return TextHyphenation::Full;
    case HyphenationFrequency::None:
      return TextHyphenation::None;
  }
  return TextHyphenation::None;
}

TextBreakStrategy textBreakStrategy(facebook::react::TextBreakStrategy value) {
  switch (value) {
    case facebook::react::TextBreakStrategy::Simple:
      return TextBreakStrategy::Simple;
    case facebook::react::TextBreakStrategy::Balanced:
      return TextBreakStrategy::Balanced;
    case facebook::react::TextBreakStrategy::HighQuality:
      return TextBreakStrategy::HighQuality;
  }
  return TextBreakStrategy::HighQuality;
}

TextParagraph textParagraph(
    const AttributedString& attributedString,
    const ParagraphAttributes& paragraphAttributes,
    Float maxHeight,
    TextDataDetector dataDetector = TextDataDetector::None,
    LayoutDirection layoutDirection = LayoutDirection::LeftToRight) {
  TextParagraph result;
  result.maximumNumberOfLines =
      std::max(paragraphAttributes.maximumNumberOfLines, 0);
  result.ellipsizeMode = ellipsizeMode(paragraphAttributes.ellipsizeMode);
  result.includeFontPadding = paragraphAttributes.includeFontPadding;
  result.adjustsFontSizeToFit = paragraphAttributes.adjustsFontSizeToFit;
  result.minimumFontSize = paragraphAttributes.minimumFontSize;
  result.maximumFontSize = paragraphAttributes.maximumFontSize;
  result.minimumFontScale = paragraphAttributes.minimumFontScale;
  result.maxHeight = maxHeight;
  result.hyphenation =
      textHyphenation(paragraphAttributes.android_hyphenationFrequency);
  result.breakStrategy =
      textBreakStrategy(paragraphAttributes.textBreakStrategy);
  result.dataDetector = dataDetector;
  result.paragraphRtl = layoutDirection == LayoutDirection::RightToLeft;
  const auto& base = attributedString.getBaseTextAttributes();
  auto alignment = base.alignment;
  if (!alignment) {
    for (const auto& fragment : attributedString.getFragments()) {
      if (!fragment.isAttachment() && fragment.textAttributes.alignment) {
        alignment = fragment.textAttributes.alignment;
        break;
      }
    }
  }
  result.alignment = textAlignment(alignment);
  result.writingDirection = textDirection(base.baseWritingDirection);
  result.runs.reserve(attributedString.getFragments().size());
  for (const auto& fragment : attributedString.getFragments()) {
    const auto& attributes = fragment.textAttributes;
    TextRun run;
    run.text = fragment.string;
    run.fontFamily = attributes.fontFamily;
    run.fontSize = std::isfinite(attributes.fontSize)
        ? attributes.fontSize
        : 14.0f;
    run.fontSizeMultiplier = std::isfinite(attributes.fontSizeMultiplier)
        ? attributes.fontSizeMultiplier
        : 1.0f;
    run.fontWeight = attributes.fontWeight
        ? static_cast<int>(*attributes.fontWeight)
        : 400;
    run.italic = attributes.fontStyle &&
        *attributes.fontStyle != FontStyle::Normal;
    if (std::isfinite(attributes.letterSpacing)) {
      // RN letterSpacing is extra dp (points), not Android Paint em and
      // not device px. Skia layout is in the same dp space as fontSize;
      // do not multiply by pointScaleFactor / density here.
      run.letterSpacing = attributes.letterSpacing;
    }
    if (std::isfinite(attributes.lineHeight)) {
      run.lineHeight = attributes.lineHeight;
    }
    if (attributes.foregroundColor) {
      run.foregroundColor = textColor(attributes.foregroundColor);
    }
    if (attributes.backgroundColor) {
      run.backgroundColor = textColor(attributes.backgroundColor);
    }
    if (attributes.textDecorationLineType) {
      switch (*attributes.textDecorationLineType) {
        case TextDecorationLineType::Underline:
          run.decorationLine = TextDecorationLine::Underline;
          break;
        case TextDecorationLineType::Strikethrough:
          run.decorationLine = TextDecorationLine::Strikethrough;
          break;
        case TextDecorationLineType::UnderlineStrikethrough:
          run.decorationLine = TextDecorationLine::UnderlineStrikethrough;
          break;
        case TextDecorationLineType::None:
          run.decorationLine = TextDecorationLine::None;
          break;
      }
    }
    if (attributes.textDecorationStyle) {
      switch (*attributes.textDecorationStyle) {
        case facebook::react::TextDecorationStyle::Solid:
          run.decorationStyle = TextDecorationStyle::Solid;
          break;
        case facebook::react::TextDecorationStyle::Double:
          run.decorationStyle = TextDecorationStyle::Double;
          break;
        case facebook::react::TextDecorationStyle::Dotted:
          run.decorationStyle = TextDecorationStyle::Dotted;
          break;
        case facebook::react::TextDecorationStyle::Dashed:
          run.decorationStyle = TextDecorationStyle::Dashed;
          break;
        case facebook::react::TextDecorationStyle::Wavy:
          run.decorationStyle = TextDecorationStyle::Wavy;
          break;
      }
    }
    if (attributes.textDecorationColor) {
      run.decorationColor = textColor(attributes.textDecorationColor);
    }
    run.textTransform = textTransform(attributes.textTransform);
    if (attributes.fontVariant) {
      run.fontVariant = static_cast<uint32_t>(*attributes.fontVariant);
    }
    if (attributes.textShadowOffset) {
      run.textShadowOffsetX = attributes.textShadowOffset->width;
      run.textShadowOffsetY = attributes.textShadowOffset->height;
    }
    if (std::isfinite(attributes.textShadowRadius)) {
      run.textShadowRadius = attributes.textShadowRadius;
    }
    if (attributes.textShadowColor) {
      run.textShadowColor = textColor(attributes.textShadowColor);
    }
    run.opacity = std::isfinite(attributes.opacity)
        ? attributes.opacity
        : 1.0f;
    run.subpixel = !attributes.fontFamily.empty() ||
        attributes.fontWeight.has_value() ||
        attributes.fontStyle.has_value();
    run.attachment = fragment.isAttachment();
    if (run.attachment) {
      // Android TextInlineViewPlaceholderSpan uses the host view's measured
      // width/height. Fabric writes that into parentShadowView.layoutMetrics
      // via ParagraphShadowNode::getContentWithMeasuredAttachments. Empty
      // metrics are -1x-1; treat those and other non-finite values as 0 so
      // SkParagraph does not receive NaN placeholder sizes.
      const auto size = fragment.parentShadowView.layoutMetrics.frame.size;
      run.attachmentWidth =
          std::isfinite(size.width) && size.width > 0 ? size.width : 0;
      run.attachmentHeight =
          std::isfinite(size.height) && size.height > 0 ? size.height : 0;
      run.text.clear();
    }
    result.runs.push_back(std::move(run));
  }
  return result;
}

Float roundedUp(Float value, Float pointScaleFactor) {
  if (!std::isfinite(pointScaleFactor) || pointScaleFactor <= 0) {
    return value;
  }
  return std::ceil(value * pointScaleFactor) / pointScaleFactor;
}
#endif

} // namespace

HeadlessTextLayoutManager::HeadlessTextLayoutManager(
    const std::shared_ptr<const ContextContainer>& contextContainer,
    const std::filesystem::path& fontDirectory,
    const std::string& platform)
    : TextLayoutManager(contextContainer)
#if RNS_ENABLE_SKIA
      , skiaTextLayoutEngine_(
            fontDirectory,
            platform == "ios"
                ? ReactNativeSimulator::TextFontPlatform::IOS
                : platform == "android"
                ? ReactNativeSimulator::TextFontPlatform::Android
                : ReactNativeSimulator::TextFontPlatform::Generic)
#endif
{
#if !RNS_ENABLE_SKIA
  (void)fontDirectory;
  (void)platform;
#endif
}

#if RNS_ENABLE_SKIA
std::shared_ptr<const ReactNativeSimulator::SkiaPreparedParagraph>
HeadlessTextLayoutManager::prepare(
    const AttributedString& attributedString,
    const ParagraphAttributes& paragraphAttributes,
    const TextLayoutContext& layoutContext,
    const LayoutConstraints& layoutConstraints) const {
  auto normalizedConstraints = layoutConstraints;
  normalizedConstraints.minimumSize = {0, 0};
  const auto maxHeight = layoutConstraints.maximumSize.height;
  if (!paragraphAttributes.adjustsFontSizeToFit) {
    normalizedConstraints.maximumSize.height =
        std::numeric_limits<Float>::infinity();
  }
  PreparedTextCacheKey key{
      .attributedString = attributedString,
      .paragraphAttributes = paragraphAttributes,
      .layoutConstraints = normalizedConstraints,
      .pointScaleFactor = layoutContext.pointScaleFactor};
  return preparedTextCache_.get(key, [&] {
    return skiaTextLayoutEngine_.prepare(
        textParagraph(
            attributedString,
            paragraphAttributes,
            maxHeight,
            TextDataDetector::None,
            layoutConstraints.layoutDirection),
        normalizedConstraints.maximumSize.width,
        layoutContext.pointScaleFactor);
  });
}

std::shared_ptr<const ReactNativeSimulator::SkiaPreparedParagraph>
HeadlessTextLayoutManager::prepareForPaint(
    const AttributedString& attributedString,
    const ParagraphAttributes& paragraphAttributes,
    const TextLayoutContext& layoutContext,
    const LayoutConstraints& layoutConstraints,
    TextDataDetector dataDetector) const {
  if (dataDetector != TextDataDetector::None) {
    const auto contentWidth = layoutConstraints.maximumSize.width;
    return skiaTextLayoutEngine_.prepare(
        textParagraph(
            attributedString,
            paragraphAttributes,
            layoutConstraints.maximumSize.height,
            dataDetector,
            layoutConstraints.layoutDirection),
        std::isfinite(contentWidth) && contentWidth > 0 ? contentWidth
                                                        : 0,
        layoutContext.pointScaleFactor);
  }
  const auto exact = prepare(
      attributedString,
      paragraphAttributes,
      layoutContext,
      layoutConstraints);
  if (exact->wasMeasured()) {
    return exact;
  }
  std::lock_guard lock(measuredParagraphsMutex_);
  for (auto entry = measuredParagraphs_.rbegin();
       entry != measuredParagraphs_.rend(); ++entry) {
    if (floatEquality(entry->pointScaleFactor, layoutContext.pointScaleFactor) &&
        entry->paragraphAttributes == paragraphAttributes &&
        areAttributedStringsEquivalentDisplayWise(
            entry->attributedString, attributedString) &&
        entry->prepared->canPaintAtWidth(
            layoutConstraints.maximumSize.width)) {
      return entry->prepared;
    }
  }
  return exact;
}
#endif

TextMeasurement HeadlessTextLayoutManager::measure(
    const AttributedStringBox& attributedStringBox,
    const ParagraphAttributes& paragraphAttributes,
    const TextLayoutContext& layoutContext,
    const LayoutConstraints& layoutConstraints) const {
#if RNS_ENABLE_SKIA
  const auto& attributedString = attributedStringBox.getValue();
  const auto prepared = prepare(
      attributedString,
      paragraphAttributes,
      layoutContext,
      layoutConstraints);
  prepared->markMeasured();
  {
    std::lock_guard lock(measuredParagraphsMutex_);
    if (std::isfinite(layoutContext.pointScaleFactor) &&
        layoutContext.pointScaleFactor > 0) {
      lastPointScaleFactor_ = layoutContext.pointScaleFactor;
    }
    const auto alreadyRecorded = std::find_if(
        measuredParagraphs_.begin(),
        measuredParagraphs_.end(),
        [&](const auto& entry) {
          return entry.prepared.get() == prepared.get();
        });
    if (alreadyRecorded == measuredParagraphs_.end()) {
      measuredParagraphs_.push_back({
          .attributedString = attributedString,
          .paragraphAttributes = paragraphAttributes,
          .pointScaleFactor = layoutContext.pointScaleFactor,
          .prepared = prepared});
      if (measuredParagraphs_.size() > kSimpleThreadSafeCacheSizeCap) {
        measuredParagraphs_.erase(measuredParagraphs_.begin());
      }
    }
  }
  TextMeasurement::Attachments attachments;
  const auto& preparedAttachments = prepared->attachments();
  std::size_t preparedIndex = 0;
  // SkParagraph line metrics can omit placeholder ascent, so the paragraph
  // box must be the union of text bounds and every unclipped attachment.
  // Otherwise Yoga gets a one-line height, Text's default overflow:hidden
  // clips the inline Image, and trailing glyphs on the image line vanish.
  Float contentWidth = prepared->width();
  Float contentHeight = prepared->height();
  for (const auto& fragment : attributedString.getFragments()) {
    if (!fragment.isAttachment()) {
      continue;
    }
    if (preparedIndex < preparedAttachments.size()) {
      const auto& attachment = preparedAttachments[preparedIndex++];
      const auto frame = Rect{
          .origin = {.x = attachment.x, .y = attachment.y},
          .size = {
              .width = attachment.width,
              .height = attachment.height}};
      if (!attachment.clipped) {
        contentWidth = std::max(contentWidth, frame.getMaxX());
        contentHeight = std::max(contentHeight, frame.getMaxY());
      }
      attachments.push_back(TextMeasurement::Attachment{
          .frame = frame,
          .isClipped = attachment.clipped});
    } else {
      attachments.push_back(TextMeasurement::Attachment{
          .frame = {},
          .isClipped = true});
    }
  }
  return TextMeasurement{
      .size = layoutConstraints.clamp({
          .width = roundedUp(contentWidth, layoutContext.pointScaleFactor),
          .height = roundedUp(contentHeight, layoutContext.pointScaleFactor)}),
      .attachments = std::move(attachments)};
#else
  (void)attributedStringBox;
  (void)paragraphAttributes;
  (void)layoutContext;
  (void)layoutConstraints;
  throw std::runtime_error(
      "React Native text measurement requires RNS_ENABLE_SKIA=ON");
#endif
}

LinesMeasurements HeadlessTextLayoutManager::measureLines(
    const AttributedStringBox& attributedStringBox,
    const ParagraphAttributes& paragraphAttributes,
    const Size& size) const {
#if RNS_ENABLE_SKIA
  auto lineAttributes = paragraphAttributes;
  // Android onTextLayout with ellipsizeMode="clip" reports every wrapped
  // line of the untruncated layout, not the single clipped paint line.
  if (lineAttributes.ellipsizeMode == EllipsizeMode::Clip) {
    lineAttributes.maximumNumberOfLines = 0;
  }
  const auto& attributedString = attributedStringBox.getValue();
  std::shared_ptr<const ReactNativeSimulator::SkiaPreparedParagraph> prepared;
  Float pointScaleFactor = 1;
  {
    std::lock_guard lock(measuredParagraphsMutex_);
    pointScaleFactor = lastPointScaleFactor_;
    for (auto entry = measuredParagraphs_.rbegin();
         entry != measuredParagraphs_.rend();
         ++entry) {
      if (!areAttributedStringsEquivalentDisplayWise(
              entry->attributedString, attributedString)) {
        continue;
      }
      if (std::isfinite(entry->pointScaleFactor) &&
          entry->pointScaleFactor > 0) {
        pointScaleFactor = entry->pointScaleFactor;
      }
      const bool sameUntruncated =
          entry->paragraphAttributes == lineAttributes;
      const bool sameYogaAttrs =
          entry->paragraphAttributes == paragraphAttributes &&
          (paragraphAttributes.maximumNumberOfLines == 0 ||
           paragraphAttributes.ellipsizeMode != EllipsizeMode::Clip);
      if ((sameUntruncated || sameYogaAttrs) &&
          entry->prepared->canPaintAtWidth(size.width)) {
        prepared = entry->prepared;
        break;
      }
    }
  }
  if (!prepared) {
    // RN measureLines has no TextLayoutContext. Reuse Yoga's last density so
    // onTextLayout does not relayout Hebrew/CJK at scale 1 while Yoga used
    // 2.75, which jittered LineMeasurement and retriggered setState.
    prepared = prepare(
        attributedString,
        lineAttributes,
        TextLayoutContext{.pointScaleFactor = pointScaleFactor},
        LayoutConstraints{
            .minimumSize = {0, 0},
            .maximumSize = size});
  }
  LinesMeasurements lines;
  const auto snap = [pointScaleFactor](Float value) {
    if (!(pointScaleFactor > 0) || !std::isfinite(value)) {
      return value;
    }
    return std::round(value * pointScaleFactor) / pointScaleFactor;
  };
  const auto& preparedLines = prepared->lines();
  for (std::size_t index = 0; index < preparedLines.size(); ++index) {
    const auto& line = preparedLines[index];
    const auto ascent = snap(std::abs(line.ascent));
    // Android StaticLayout folds includeFontPadding's top extra into
    // getLineAscent(0). ParagraphShadowNode uses this `ascender` directly as
    // Yoga's baseline. Reporting only the typographic ascent leaves sibling
    // Views several pixels above the Text baseline in alignItems:'baseline'.
    const auto reportedAscent = index == 0
        ? snap(prepared->firstLineAscent())
        : ascent;
    const auto descent = snap(std::abs(line.descent));
    const auto height = snap(std::max(
        line.height,
        reportedAscent + descent));
    lines.push_back(LineMeasurement(
        line.text,
        Rect{
            .origin =
                {.x = snap(line.left),
                 .y = snap(line.baseline - reportedAscent)},
            .size = {.width = snap(line.width), .height = height}},
        descent,
        snap(ascent * 0.7f),
        reportedAscent,
        snap(ascent * 0.5f)));
  }
  return lines;
#else
  (void)attributedStringBox;
  (void)paragraphAttributes;
  (void)size;
  return {};
#endif
}

} // namespace facebook::react
