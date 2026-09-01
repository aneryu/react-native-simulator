#pragma once

#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class SkCanvas;

namespace ReactNativeSimulator {

void validateSkiaFontDirectory(const std::filesystem::path& fontDirectory);

enum class TextHorizontalAlignment {
  Natural,
  Left,
  Center,
  Right,
  Justified,
  Start,
  End,
};

enum class TextWritingDirection {
  Natural,
  LeftToRight,
  RightToLeft,
};

enum class TextFontPlatform {
  Generic,
  Android,
  IOS,
};

enum class TextEllipsizeMode {
  Clip,
  Head,
  Tail,
  Middle,
};

enum class TextDecorationLine {
  None,
  Underline,
  Strikethrough,
  UnderlineStrikethrough,
};

enum class TextDecorationStyle {
  Solid,
  Double,
  Dotted,
  Dashed,
  Wavy,
};

enum class TextTransform {
  None,
  Uppercase,
  Lowercase,
  Capitalize,
};

enum class TextHyphenation {
  None,
  Normal,
  Full,
};

enum class TextBreakStrategy {
  Simple,
  HighQuality,
  Balanced,
};

enum class TextVerticalAlignment {
  Auto,
  Top,
  Center,
  Bottom,
};

enum class TextDataDetector {
  None,
  Phone,
  Link,
  Email,
  All,
};

struct TextColor {
  float red{0};
  float green{0};
  float blue{0};
  float alpha{1};
};

struct TextRun {
  std::string text;
  std::string fontFamily;
  float fontSize{14};
  float fontSizeMultiplier{1};
  int fontWeight{400};
  bool italic{false};
  std::optional<float> letterSpacing;
  std::optional<float> lineHeight;
  std::optional<TextColor> foregroundColor;
  std::optional<TextColor> backgroundColor;
  TextDecorationLine decorationLine{TextDecorationLine::None};
  TextDecorationStyle decorationStyle{TextDecorationStyle::Solid};
  std::optional<TextColor> decorationColor;
  TextTransform textTransform{TextTransform::None};
  uint32_t fontVariant{0};
  float textShadowOffsetX{0};
  float textShadowOffsetY{0};
  float textShadowRadius{0};
  std::optional<TextColor> textShadowColor;
  float opacity{1};
  bool subpixel{false};
  bool attachment{false};
  float attachmentWidth{0};
  float attachmentHeight{0};
};

struct TextParagraph {
  std::vector<TextRun> runs;
  TextHorizontalAlignment alignment{TextHorizontalAlignment::Natural};
  TextWritingDirection writingDirection{TextWritingDirection::Natural};
  // Yoga/layout direction of the paragraph, not first-strong of the script.
  // Android gravity for auto/left follows this, while bidi still uses
  // first-strong.
  bool paragraphRtl{false};
  TextEllipsizeMode ellipsizeMode{TextEllipsizeMode::Clip};
  std::size_t maximumNumberOfLines{0};
  // Android / RN ParagraphAttributes default. iOS ignores the flag.
  bool includeFontPadding{true};
  bool adjustsFontSizeToFit{false};
  float minimumFontSize{std::numeric_limits<float>::quiet_NaN()};
  float maximumFontSize{std::numeric_limits<float>::quiet_NaN()};
  float minimumFontScale{std::numeric_limits<float>::quiet_NaN()};
  float maxHeight{std::numeric_limits<float>::infinity()};
  TextHyphenation hyphenation{TextHyphenation::None};
  TextDataDetector dataDetector{TextDataDetector::None};
  // Android ParagraphAttributes default to HighQuality (Knuth-Plass-style
  // evenness). Simple keeps SkParagraph's greedy fill.
  TextBreakStrategy breakStrategy{TextBreakStrategy::HighQuality};
};

struct PreparedTextLine {
  float left{0};
  float width{0};
  float height{0};
  float baseline{0};
  float ascent{0};
  float descent{0};
  std::size_t utf16Start{0};
  std::size_t utf16End{0};
  std::string text;
};

struct PreparedTextAttachment {
  float x{0};
  float y{0};
  float width{0};
  float height{0};
  bool clipped{false};
};

class SkiaPreparedParagraph final {
 public:
  ~SkiaPreparedParagraph();
  SkiaPreparedParagraph(SkiaPreparedParagraph&&) noexcept;
  SkiaPreparedParagraph& operator=(SkiaPreparedParagraph&&) noexcept;
  SkiaPreparedParagraph(const SkiaPreparedParagraph&) = delete;
  SkiaPreparedParagraph& operator=(const SkiaPreparedParagraph&) = delete;

  float width() const noexcept;
  float height() const noexcept;
  float longestLine() const noexcept;
  float alphabeticBaseline() const noexcept;
  float firstLineAscent() const noexcept;
  float layoutWidth() const noexcept;
  std::size_t lineCount() const noexcept;
  TextWritingDirection writingDirection() const noexcept;
  bool exceededMaximumLines() const noexcept;
  bool canPaintAtWidth(float width) const noexcept;
  void markMeasured() const noexcept;
  bool wasMeasured() const noexcept;
  const std::vector<PreparedTextAttachment>& attachments() const noexcept;
  const std::vector<PreparedTextLine>& lines() const noexcept;
  float widthForUtf16Range(std::size_t start, std::size_t end) const;
  void retainUnconstrainedIntrinsicWidth(float unconstrainedWidth);
  void paint(SkCanvas& canvas, float x, float y) const;

 private:
  class Impl;
  explicit SkiaPreparedParagraph(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;

  friend class SkiaTextLayoutEngine;
};

class SkiaTextLayoutEngine final {
 public:
  explicit SkiaTextLayoutEngine(
      std::filesystem::path fontDirectory = {},
      TextFontPlatform platform = TextFontPlatform::Generic);
  ~SkiaTextLayoutEngine();
  SkiaTextLayoutEngine(SkiaTextLayoutEngine&&) noexcept;
  SkiaTextLayoutEngine& operator=(SkiaTextLayoutEngine&&) noexcept;
  SkiaTextLayoutEngine(const SkiaTextLayoutEngine&) = delete;
  SkiaTextLayoutEngine& operator=(const SkiaTextLayoutEngine&) = delete;

  std::shared_ptr<const SkiaPreparedParagraph> prepare(
      const TextParagraph& paragraph,
      float width,
      float pointScaleFactor = 1.0f) const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ReactNativeSimulator
