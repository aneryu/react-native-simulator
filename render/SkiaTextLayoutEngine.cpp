#include "SkiaTextLayoutEngine.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "include/core/SkFourByteTag.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_directory.h"
#include "include/ports/SkFontMgr_empty.h"
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
#include "include/ports/SkFontMgr_mac_ct.h"
#endif
#if defined(SK_FONTMGR_FONTCONFIG_AVAILABLE) || defined(__linux__)
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#endif
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/TypefaceFontProvider.h"
#include "modules/skparagraph/include/Paragraph.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skparagraph/include/Metrics.h"
#include "modules/skparagraph/include/TextShadow.h"
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skunicode/include/SkUnicode_icu.h"
#include "unicode/ubidi.h"
#include "unicode/uchar.h"
#include "unicode/utf8.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ReactNativeSimulator {
namespace {

using skia::textlayout::FontCollection;
using skia::textlayout::ParagraphBuilder;
using skia::textlayout::ParagraphStyle;
using skia::textlayout::PlaceholderAlignment;
using skia::textlayout::PlaceholderStyle;
using skia::textlayout::RectHeightStyle;
using skia::textlayout::RectWidthStyle;
using skia::textlayout::TextAlign;
using skia::textlayout::TextBaseline;
using skia::textlayout::TextDirection;
using skia::textlayout::TextStyle;

float wavyHalfExtent(float thickness) {
  const float amplitude = std::max(1.0f, thickness * 1.1f);
  const float stroke = std::max(thickness, 1.0f);
  return amplitude + stroke * 0.5f;
}

void drawWavyLine(
    SkCanvas& canvas,
    float x0,
    float x1,
    float y,
    float thickness,
    SkColor color) {
  if (x1 - x0 < 0.5f) {
    return;
  }
  // Glance-level HWUI sine density, not pixel-identical. Skia's kWavy
  // decoration is a zigzag, so we stroke a cubic-interpolated sine.
  // ~6× thickness matches Pixel's denser rounded wave; 2π×amplitude
  // keeps peaks from going triangular.
  const float twoPi = 6.28318530718f;
  const float amplitude = std::max(1.0f, thickness * 1.1f);
  const float wavelength = std::max(
      std::max(6.0f, thickness * 6.0f), twoPi * amplitude);
  const float stroke = std::max(thickness, 1.0f);
  SkPathBuilder builder;
  builder.moveTo(x0, y);
  float prevX = x0;
  float prevY = y;
  const float eighth = wavelength * 0.125f;
  const int steps =
      std::max(2, static_cast<int>(std::ceil((x1 - x0) / eighth)));
  const float slope = amplitude * twoPi / wavelength;
  for (int i = 1; i <= steps; ++i) {
    const float x = std::min(x1, x0 + static_cast<float>(i) * eighth);
    const float t = (x - x0) / wavelength * twoPi;
    const float yv = y + amplitude * std::sin(t);
    const float dx = x - prevX;
    if (dx > 0.05f) {
      const float t0 = (prevX - x0) / wavelength * twoPi;
      const float m0 = slope * std::cos(t0);
      const float m1 = slope * std::cos(t);
      builder.cubicTo(
          prevX + dx / 3.0f,
          prevY + m0 * dx / 3.0f,
          x - dx / 3.0f,
          yv - m1 * dx / 3.0f,
          x,
          yv);
    } else {
      builder.lineTo(x, yv);
    }
    prevX = x;
    prevY = yv;
  }
  const auto path = builder.detach();
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeCap(SkPaint::kRound_Cap);
  paint.setStrokeJoin(SkPaint::kRound_Join);
  paint.setStrokeWidth(stroke);
  paint.setColor(color);
  canvas.drawPath(path, paint);
}

SkColor skColor(const TextColor& color, float opacity = 1) {
  const auto channel = [](float value) {
    return std::clamp(static_cast<int>(std::lround(value * 255)), 0, 255);
  };
  return SkColorSetARGB(
      channel(color.alpha * std::clamp(opacity, 0.0f, 1.0f)),
      channel(color.red),
      channel(color.green),
      channel(color.blue));
}

TextAlign skAlignment(TextHorizontalAlignment alignment, bool paragraphRtl) {
  // Android RN maps auto/left through ALIGN_NORMAL with a script/paragraph
  // swap, which collapses to: LTR paragraph -> left gravity, RTL paragraph
  // (style.direction) -> right gravity. Bidi still uses first-strong.
  const auto start = paragraphRtl ? TextAlign::kRight : TextAlign::kLeft;
  const auto end = paragraphRtl ? TextAlign::kLeft : TextAlign::kRight;
  switch (alignment) {
    case TextHorizontalAlignment::Center:
      return TextAlign::kCenter;
    case TextHorizontalAlignment::Justified:
      return TextAlign::kJustify;
    case TextHorizontalAlignment::Right:
    case TextHorizontalAlignment::End:
      return end;
    case TextHorizontalAlignment::Left:
    case TextHorizontalAlignment::Start:
    case TextHorizontalAlignment::Natural:
      return start;
  }
  return start;
}

TextDirection skDirection(TextWritingDirection direction) {
  return direction == TextWritingDirection::RightToLeft
      ? TextDirection::kRtl
      : TextDirection::kLtr;
}

TextWritingDirection resolveWritingDirection(
    const TextParagraph& paragraph) {
  if (paragraph.writingDirection != TextWritingDirection::Natural) {
    return paragraph.writingDirection;
  }

  std::string text;
  for (const auto& run : paragraph.runs) {
    if (run.attachment) {
      // U+FFFC OBJECT REPLACEMENT CHARACTER is neutral for bidi purposes.
      text.append("\xEF\xBF\xBC");
    } else {
      text.append(run.text);
    }
  }
  const auto utf16 = SkUnicode::convertUtf8ToUtf16(text.data(), text.size());
  const auto direction = ubidi_getBaseDirection(
      reinterpret_cast<const UChar*>(utf16.data()),
      static_cast<int32_t>(utf16.size()));
  return direction == UBIDI_RTL
      ? TextWritingDirection::RightToLeft
      : TextWritingDirection::LeftToRight;
}

std::vector<SkString> defaultFontFamilies(TextFontPlatform platform) {
  switch (platform) {
    case TextFontPlatform::Android:
    case TextFontPlatform::Generic:
      return {SkString("Roboto"), SkString("sans-serif")};
    case TextFontPlatform::IOS:
      return {SkString("SF Pro Text"), SkString("sans-serif")};
  }
  return {SkString("sans-serif")};
}

struct ScriptFallbacks {
  bool han{false};
  bool arabic{false};
  bool hebrew{false};
  bool devanagari{false};
  bool thai{false};
  bool emoji{false};
};

ScriptFallbacks detectScriptFallbacks(const std::string& text) {
  ScriptFallbacks needs;
  int32_t offset = 0;
  const auto size = static_cast<int32_t>(text.size());
  while (offset < size) {
    UChar32 cp = 0;
    U8_NEXT(text.data(), offset, size, cp);
    if (cp < 0) {
      break;
    }
    // UCHAR_EMOJI is also true for ASCII digits / '#' / '*'. Those must
    // stay on Roboto; loading Noto Color Emoji for every "900" line both
    // stalls Text and clips glyphs against overflow:hidden.
    if (cp > 0x7F &&
        (u_hasBinaryProperty(cp, UCHAR_EMOJI_PRESENTATION) ||
         u_hasBinaryProperty(cp, UCHAR_EXTENDED_PICTOGRAPHIC) ||
         (cp >= 0x1F1E6 && cp <= 0x1F1FF))) {
      needs.emoji = true;
    }
    if ((cp >= 0x3400 && cp <= 0x9FFF) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0x3040 && cp <= 0x30FF) ||
        (cp >= 0xAC00 && cp <= 0xD7AF) ||
        (cp >= 0x20000 && cp <= 0x2FA1F)) {
      needs.han = true;
    } else if (cp >= 0x0590 && cp <= 0x05FF) {
      needs.hebrew = true;
    } else if ((cp >= 0x0600 && cp <= 0x06FF) ||
               (cp >= 0x0750 && cp <= 0x077F) ||
               (cp >= 0x08A0 && cp <= 0x08FF)) {
      needs.arabic = true;
    } else if (cp >= 0x0900 && cp <= 0x097F) {
      needs.devanagari = true;
    } else if (cp >= 0x0E00 && cp <= 0x0E7F) {
      needs.thai = true;
    }
  }
  return needs;
}

bool isEmojiOnlyText(const std::string& text) {
  bool sawNonAscii = false;
  int32_t offset = 0;
  const auto size = static_cast<int32_t>(text.size());
  while (offset < size) {
    UChar32 cp = 0;
    U8_NEXT(text.data(), offset, size, cp);
    if (cp < 0) {
      return false;
    }
    if (u_isUWhiteSpace(cp)) {
      continue;
    }
    // Emoji cluster components are all non-ASCII. Reject visible ASCII so a
    // mixed prose/digit run retains Roboto; other non-ASCII codepoints simply
    // fall through Noto to their normal script family.
    if (cp <= 0x7F) {
      return false;
    }
    sawNonAscii = true;
  }
  return sawNonAscii;
}

void addFamilyUnique(std::vector<SkString>& families, const char* name) {
  for (const auto& existing : families) {
    if (existing.equals(name)) {
      return;
    }
  }
  families.emplace_back(name);
}

void appendScriptFallbacks(
    const TextRun& run,
    std::vector<SkString>& families,
    TextFontPlatform platform) {
  const auto needs = detectScriptFallbacks(run.text);
  if (platform == TextFontPlatform::IOS) {
    addFamilyUnique(families, "SF Pro Text");
    if (needs.han) {
      addFamilyUnique(families, "PingFang SC");
    }
    if (needs.emoji) {
      addFamilyUnique(families, "Apple Color Emoji");
    }
    addFamilyUnique(families, "sans-serif");
    return;
  }
  // SkParagraph's fallback itemizer can split an emoji modifier/ZWJ cluster
  // before HarfBuzz sees it. For an emoji-only run, shape with Noto as the
  // primary family so sequences such as 🙏🏾 and 👩🏽‍🔧 remain one glyph.
  // Mixed prose keeps Roboto first so ordinary digits do not turn into emoji.
  if (needs.emoji && isEmojiOnlyText(run.text)) {
    addFamilyUnique(families, "Noto Color Emoji");
    addFamilyUnique(families, "Noto Color Emoji Flags");
  }
  addFamilyUnique(families, "Roboto");
  if (needs.han) {
    addFamilyUnique(families, "Noto Sans CJK SC");
    addFamilyUnique(families, "Noto Sans CJK JP");
    addFamilyUnique(families, "Noto Sans CJK KR");
    addFamilyUnique(families, "Noto Sans CJK TC");
  }
  if (needs.arabic) {
    addFamilyUnique(families, "Noto Naskh Arabic");
  }
  if (needs.hebrew) {
    addFamilyUnique(families, "Noto Sans Hebrew");
  }
  if (needs.devanagari) {
    addFamilyUnique(families, "Noto Sans Devanagari UI");
    addFamilyUnique(families, "Noto Sans Devanagari");
  }
  if (needs.thai) {
    addFamilyUnique(families, "Noto Sans Thai UI");
    addFamilyUnique(families, "Noto Sans Thai");
  }
  if (needs.emoji) {
    addFamilyUnique(families, "Noto Color Emoji");
    addFamilyUnique(families, "Noto Color Emoji Flags");
  }
  addFamilyUnique(families, "sans-serif");
}

std::string lowerCopy(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

struct ResolvedTypeface {
  std::vector<SkString> families;
  int weight{400};
  SkFontStyle::Width width{SkFontStyle::kNormal_Width};
  SkFontStyle::Slant slant{SkFontStyle::kUpright_Slant};
};

// Android fonts.xml generic families / aliases, plus RN Tester asset names
// that do not match the OpenType family string inside the file.
ResolvedTypeface resolveTypeface(
    const TextRun& run,
    TextFontPlatform platform) {
  ResolvedTypeface resolved;
  resolved.weight = std::clamp(run.fontWeight, 1, 1000);
  resolved.slant = run.italic ? SkFontStyle::kItalic_Slant
                              : SkFontStyle::kUpright_Slant;
  const auto requested = run.fontFamily;
  const auto key = lowerCopy(requested);
  const bool defaultWeight = run.fontWeight == 400;

  const auto appendDefaults = [&]() {
    appendScriptFallbacks(run, resolved.families, platform);
  };
  const auto useFamily = [&](const char* family) {
    resolved.families.emplace_back(family);
    appendDefaults();
  };

  if (platform == TextFontPlatform::Android ||
      platform == TextFontPlatform::Generic) {
    if (key == "sans-serif" || key == "arial" || key == "helvetica" ||
        key == "tahoma" || key == "verdana") {
      useFamily("Roboto");
      return resolved;
    }
    if (key == "sans-serif-thin") {
      if (defaultWeight) {
        resolved.weight = 100;
      }
      useFamily("Roboto");
      return resolved;
    }
    if (key == "sans-serif-light") {
      if (defaultWeight) {
        resolved.weight = 300;
      }
      useFamily("Roboto");
      return resolved;
    }
    if (key == "sans-serif-medium") {
      if (defaultWeight) {
        resolved.weight = 500;
      }
      useFamily("Roboto");
      return resolved;
    }
    if (key == "sans-serif-black") {
      if (defaultWeight) {
        resolved.weight = 900;
      }
      useFamily("Roboto");
      return resolved;
    }
    if (key == "sans-serif-condensed" ||
        key == "sans-serif-condensed-light" ||
        key == "sans-serif-condensed-medium") {
      resolved.width = SkFontStyle::kCondensed_Width;
      if (defaultWeight && key == "sans-serif-condensed-light") {
        resolved.weight = 300;
      } else if (defaultWeight && key == "sans-serif-condensed-medium") {
        resolved.weight = 500;
      }
      useFamily("Roboto");
      return resolved;
    }
    if (key == "serif" || key == "times" || key == "times new roman" ||
        key == "georgia" || key == "palatino") {
      useFamily("Noto Serif");
      return resolved;
    }
    if (key == "monospace" || key == "sans-serif-monospace" ||
        key == "monaco") {
      resolved.families.emplace_back("Droid Sans Mono");
      resolved.families.emplace_back("Cutive Mono");
      appendDefaults();
      return resolved;
    }
  }

  if (key == "firacode") {
    useFamily("Fira Code");
    return resolved;
  }
  if (key == "notoserif") {
    useFamily("Noto Serif");
    return resolved;
  }
  if (key == "rubik") {
    if (resolved.weight <= 350) {
      resolved.families.emplace_back("Rubik Light");
    } else if (resolved.weight >= 450 && resolved.weight < 650) {
      resolved.families.emplace_back("Rubik Medium");
    }
    useFamily("Rubik");
    return resolved;
  }

  if (!requested.empty()) {
    resolved.families.emplace_back(requested.c_str());
  }
  appendDefaults();
  return resolved;
}

SkFontStyle skFontStyle(const ResolvedTypeface& resolved) {
  return SkFontStyle(resolved.weight, resolved.width, resolved.slant);
}

TextStyle skTextStyle(
    const TextRun& run,
    TextFontPlatform platform,
    bool paintBackground = true) {
  TextStyle style;
  const auto fontSize = std::max(
      run.fontSize * std::max(run.fontSizeMultiplier, 0.01f), 1.0f);
  const auto resolved = resolveTypeface(run, platform);
  style.setFontFamilies(resolved.families);
  style.setFontSize(fontSize);
  style.setFontStyle(skFontStyle(resolved));
  style.setLocale(SkString("und"));
  // Android StaticLayout draws every span on the line's alphabetic
  // baseline. SkParagraph defaults here too; set it so mixed-size nested
  // Text cannot pick up an ideographic/center style from the paragraph.
  style.setTextBaseline(TextBaseline::kAlphabetic);
  style.setSubpixel(run.subpixel);
  style.setFontEdging(SkFont::Edging::kAntiAlias);
  if (platform == TextFontPlatform::Android &&
      (!run.subpixel || fontSize < 9.0f)) {
    // Android hints at the final physical text size. SkParagraph shapes in
    // logical dp before the renderer applies its density transform, so normal
    // hinting here would quantize advances at (for example) 8px instead of
    // the Pixel's 22px for 8sp @ 2.75x. Slight hinting keeps scalable
    // advances and avoids the visible `i`/`z` overlap in RN Tester's 8sp
    // sample while retaining hinted raster edges after the canvas transform.
    style.setFontHinting(SkFontHinting::kSlight);
  } else if (platform == TextFontPlatform::Android) {
    style.setFontHinting(SkFontHinting::kNormal);
  }
  if (run.letterSpacing) {
    // RN letterSpacing is extra dp between glyphs — the same units as
    // fontSize. Android Paint uses em (`toPixel(letterSpacing) /
    // toPixel(fontSize)`); SkParagraph setLetterSpacing is extra px, so
    // pass the dp value through. Layout is already in dp: do not apply
    // density again. Scale with fontSizeMultiplier so the em ratio stays
    // letterSpacing/fontSize when the run is autosized.
    style.setLetterSpacing(
        *run.letterSpacing * std::max(run.fontSizeMultiplier, 0.01f));
  }
  if (run.lineHeight) {
    style.setHeight(std::max(*run.lineHeight / fontSize, 0.01f));
    style.setHeightOverride(true);
    style.setHalfLeading(true);
  }
  if (run.foregroundColor) {
    style.setColor(skColor(*run.foregroundColor, run.opacity));
  }
  if (paintBackground && run.backgroundColor) {
    SkPaint paint;
    paint.setColor(skColor(*run.backgroundColor, run.opacity));
    style.setBackgroundPaint(std::move(paint));
  }
  if (run.decorationLine != TextDecorationLine::None) {
    using skia::textlayout::TextDecoration;
    using SkDecoStyle = skia::textlayout::TextDecorationStyle;
    auto decoration = TextDecoration::kNoDecoration;
    switch (run.decorationLine) {
      case TextDecorationLine::Underline:
        decoration = TextDecoration::kUnderline;
        break;
      case TextDecorationLine::Strikethrough:
        decoration = TextDecoration::kLineThrough;
        break;
      case TextDecorationLine::UnderlineStrikethrough:
        decoration = static_cast<TextDecoration>(
            TextDecoration::kUnderline | TextDecoration::kLineThrough);
        break;
      case TextDecorationLine::None:
        break;
    }
    style.setDecoration(decoration);
    switch (run.decorationStyle) {
      case TextDecorationStyle::Solid:
        style.setDecorationStyle(SkDecoStyle::kSolid);
        break;
      case TextDecorationStyle::Double:
        style.setDecorationStyle(SkDecoStyle::kDouble);
        break;
      case TextDecorationStyle::Dotted:
        style.setDecorationStyle(SkDecoStyle::kDotted);
        break;
      case TextDecorationStyle::Dashed:
        style.setDecorationStyle(SkDecoStyle::kDashed);
        break;
      case TextDecorationStyle::Wavy:
        // HWUI draws a sine; Skia's kWavy is a zigzag. Custom-paint later.
        style.setDecoration(TextDecoration::kNoDecoration);
        break;
    }
    if (run.decorationColor) {
      style.setDecorationColor(skColor(*run.decorationColor, run.opacity));
    } else if (run.foregroundColor) {
      style.setDecorationColor(skColor(*run.foregroundColor, run.opacity));
    }
  }
  const auto addFeature = [&](const char* tag) {
    style.addFontFeature(SkString(tag), 1);
  };
  // FontVariant::SmallCaps. Roboto lists GSUB smcp, but HWUI/Paint does not
  // apply it, so Pixel keeps mixed case. Skia/HarfBuzz still substitutes
  // (SMALL CAPS). Do not request smcp on the Android path or synthesize.
  if ((run.fontVariant & (1u << 1)) &&
      platform != TextFontPlatform::Android) {
    addFeature("smcp");
  }
  if (run.fontVariant & (1u << 2)) {
    addFeature("onum");
  }
  if (run.fontVariant & (1u << 3)) {
    addFeature("lnum");
  }
  if (run.fontVariant & (1u << 4)) {
    addFeature("tnum");
  }
  if (run.fontVariant & (1u << 5)) {
    addFeature("pnum");
  }
  for (int index = 0; index < 20; ++index) {
    if (run.fontVariant & (1u << (6 + index))) {
      char tag[] = {'s', 's', static_cast<char>('0' + (index + 1) / 10),
                    static_cast<char>('0' + (index + 1) % 10), 0};
      addFeature(tag);
    }
  }
  if (run.textShadowRadius > 0 || run.textShadowOffsetX != 0 ||
      run.textShadowOffsetY != 0) {
    const auto color = run.textShadowColor
        ? skColor(*run.textShadowColor, run.opacity)
        : SkColorSetARGB(
              static_cast<U8CPU>(std::clamp(run.opacity, 0.0f, 1.0f) * 255),
              0,
              0,
              0);
    style.addShadow(skia::textlayout::TextShadow(
        color,
        SkPoint::Make(run.textShadowOffsetX, run.textShadowOffsetY),
        std::max(run.textShadowRadius, 0.0f) * 0.5));
  }
  return style;
}

sk_sp<SkTypeface> typefaceForRun(
    SkFontMgr& fontMgr,
    const TextRun& run,
    TextFontPlatform platform) {
  const auto resolved = resolveTypeface(run, platform);
  const auto style = skFontStyle(resolved);
  for (const auto& family : resolved.families) {
    if (auto face = fontMgr.matchFamilyStyle(family.c_str(), style)) {
      return face;
    }
  }
  return fontMgr.legacyMakeTypeface(nullptr, style);
}

struct AndroidFontPadding {
  float top{0};
  float bottom{0};
};

uint16_t sfntU16(const uint8_t* data) {
  return static_cast<uint16_t>((data[0] << 8) | data[1]);
}

int16_t sfntI16(const uint8_t* data) {
  return static_cast<int16_t>(sfntU16(data));
}

bool readTypefaceTable(
    SkTypeface& face,
    SkFontTableTag tag,
    size_t offset,
    size_t length,
    void* dst) {
  return face.getTableData(tag, offset, length, dst) == length;
}

// Android Paint FontMetricsInt: ascent/descent from hhea (or OS/2 typo when
// USE_TYPO_METRICS is set); top/bottom from usWinAscent/usWinDescent.
// Do not clamp win to the historical 1946/512 Roboto grid. Pixel 4a RN Tester
// screenshots of this pulled Roboto (win 2146/555) match the file's OS/2
// table: magenta 2-line boxes 56.00dp on device vs 55.27dp without the cap
// and 53.45dp with it. The cap shortened every includeFontPadding line and
// shifted TextExample's first screen.

struct AndroidFontMetricsInt {
  float ascent{0};
  float descent{0};
  float winAscent{0};
  float winDescent{0};
};

struct AndroidFontMetricsPx {
  int ascent{0};
  int descent{0};
  int top{0};
  int bottom{0};
};

// Android TextAttributes.effectiveFontSize: ceil(sp * density) to integer px.
float snapAndroidDp(float dp, float pointScaleFactor) {
  if (!(pointScaleFactor > 0) || !std::isfinite(dp)) {
    return dp;
  }
  return static_cast<float>(
      std::ceil(static_cast<double>(dp) * static_cast<double>(pointScaleFactor)) /
      static_cast<double>(pointScaleFactor));
}

void snapAndroidParagraph(TextParagraph& paragraph, float pointScaleFactor) {
  if (!(pointScaleFactor > 0) || pointScaleFactor == 1.0f) {
    return;
  }
  for (auto& run : paragraph.runs) {
    if (run.attachment) {
      continue;
    }
    // Android's default TextPaint quantizes glyph positions in physical
    // pixels. SkParagraph lays out in dp and `subpixel=false` therefore
    // quantizes in whole dp instead, which is much coarser on a 2.75x
    // device (the 8sp "Size 8" sample even overlaps `i` and `z`). Keep
    // fractional dp positions so the renderer's density transform performs
    // the equivalent physical-pixel placement.
    if (run.fontSize * std::max(run.fontSizeMultiplier, 0.01f) < 9.0f) {
      run.subpixel = true;
    }
    // StaticLayout treats CRLF as one paragraph break (and a lone CR as a
    // newline). SkParagraph otherwise counts the two code units separately,
    // adding a blank row to every transformed RN Tester fixture.
    std::string normalized;
    normalized.reserve(run.text.size());
    for (std::size_t index = 0; index < run.text.size(); ++index) {
      if (run.text[index] != '\r') {
        normalized.push_back(run.text[index]);
        continue;
      }
      normalized.push_back('\n');
      if (index + 1 < run.text.size() && run.text[index + 1] == '\n') {
        ++index;
      }
    }
    run.text = std::move(normalized);
    const auto unsnappedFontSize = run.fontSize;
    const auto snappedFontSize =
        snapAndroidDp(run.fontSize, pointScaleFactor);
    // Noto CJK has a one-em advance. Android effectiveFontSize rounds 14sp at
    // 2.75x from 38.5px to 39px, while SkParagraph fallback shaping retains
    // the unsnapped 14dp advance. Preserve that physical-pixel delta as
    // tracking when the caller did not specify letterSpacing.
    if (!run.letterSpacing && detectScriptFallbacks(run.text).han) {
      const auto delta = snappedFontSize - unsnappedFontSize;
      if (delta > 0) {
        run.letterSpacing = delta;
      }
    }
    run.fontSize = snappedFontSize;
    if (run.lineHeight) {
      run.lineHeight = snapAndroidDp(*run.lineHeight, pointScaleFactor);
    }
    if (run.letterSpacing) {
      run.letterSpacing = snapAndroidDp(*run.letterSpacing, pointScaleFactor);
    }
  }
}

// Paint.getFontMetricsInt: round ascent/descent, floor top (more extra),
// ceil bottom. Pixel 4a 14sp @ 2.75 → textSize 39px, 1-line includePad 53px,
// 2-line 99px = 36dp, + padding 20 = 56dp magenta box.
AndroidFontMetricsPx androidFontMetricsPx(
    sk_sp<SkTypeface> face, float fontSizePx) {
  AndroidFontMetricsPx metrics;
  if (!face || !(fontSizePx > 0)) {
    return metrics;
  }
  SkFont font(std::move(face), fontSizePx);
  SkFontMetrics skMetrics;
  font.getMetrics(&skMetrics);
  metrics.ascent = std::max(
      0, static_cast<int>(std::lround(-static_cast<double>(skMetrics.fAscent))));
  metrics.descent = std::max(
      0, static_cast<int>(std::lround(static_cast<double>(skMetrics.fDescent))));
  metrics.top = std::max(
      metrics.ascent,
      -static_cast<int>(std::floor(static_cast<double>(skMetrics.fTop))));
  metrics.bottom = std::max(
      metrics.descent,
      static_cast<int>(std::ceil(static_cast<double>(skMetrics.fBottom))));
  return metrics;
}

AndroidFontMetricsInt androidFontMetricsInt(SkTypeface& face, float fontSize) {
  AndroidFontMetricsInt metrics;
  const auto upem = face.getUnitsPerEm();
  if (upem <= 0 || fontSize <= 0) {
    return metrics;
  }
  const float scale = fontSize / static_cast<float>(upem);
  uint8_t hhea[10] = {};
  uint8_t os2[78] = {};
  const bool haveHhea = readTypefaceTable(
      face, SkSetFourByteTag('h', 'h', 'e', 'a'), 0, sizeof(hhea), hhea);
  const bool haveOs2 = readTypefaceTable(
      face, SkSetFourByteTag('O', 'S', '/', '2'), 0, sizeof(os2), os2);
  float ascent = 0;
  float descent = 0;
  if (haveHhea) {
    ascent = static_cast<float>(sfntI16(hhea + 4)) * scale;
    descent = -static_cast<float>(sfntI16(hhea + 6)) * scale;
  }
  float winAscent = ascent;
  float winDescent = descent;
  if (haveOs2) {
    const auto fsSelection = sfntU16(os2 + 62);
    const auto typoAscender = static_cast<float>(sfntI16(os2 + 68)) * scale;
    const auto typoDescender = -static_cast<float>(sfntI16(os2 + 70)) * scale;
    const bool useTypo = (fsSelection & 0x80) != 0;
    if (useTypo && typoAscender > 0) {
      ascent = typoAscender;
      descent = std::max(0.0f, typoDescender);
    }
    winAscent = static_cast<float>(sfntU16(os2 + 74)) * scale;
    winDescent = static_cast<float>(sfntU16(os2 + 76)) * scale;
  }
  if (ascent <= 0 && descent <= 0) {
    return metrics;
  }
  metrics.ascent = std::max(0.0f, ascent);
  metrics.descent = std::max(0.0f, descent);
  metrics.winAscent = std::max(metrics.ascent, std::max(0.0f, winAscent));
  metrics.winDescent = std::max(metrics.descent, std::max(0.0f, winDescent));
  return metrics;
}

struct AndroidLayoutPx {
  int line{0};
  int primaryLine{0};
  int fallbackLine{0};
  int ascent{0};
  int descent{0};
  int extraTop{0};
  int extraBottom{0};
  bool valid{false};
};

AndroidLayoutPx androidLayoutPx(
    SkFontMgr& fontMgr,
    const TextParagraph& paragraph,
    TextFontPlatform platform,
    float pointScaleFactor) {
  AndroidLayoutPx layout;
  int primaryLine = 0;
  int fallbackLine = 0;
  if (platform != TextFontPlatform::Android || !(pointScaleFactor > 0)) {
    return layout;
  }
  for (const auto& run : paragraph.runs) {
    if (run.lineHeight) {
      return {};
    }
  }
  for (const auto& run : paragraph.runs) {
    if (run.attachment) {
      continue;
    }
    auto face = typefaceForRun(fontMgr, run, platform);
    if (!face) {
      continue;
    }
    const auto fontSizeDp = std::max(
        run.fontSize * std::max(run.fontSizeMultiplier, 0.01f), 1.0f);
    const auto fontSizePx = std::max(
        1.0f,
        static_cast<float>(std::ceil(
            static_cast<double>(fontSizeDp) *
            static_cast<double>(pointScaleFactor))));
    const auto consider = [&](sk_sp<SkTypeface> used) {
      if (!used) {
        return;
      }
      const auto metrics = androidFontMetricsPx(std::move(used), fontSizePx);
      primaryLine = std::max(
          primaryLine, metrics.ascent + metrics.descent);
      layout.line = std::max(layout.line, primaryLine);
      layout.ascent = std::max(layout.ascent, metrics.ascent);
      layout.descent = std::max(layout.descent, metrics.descent);
      layout.extraTop = std::max(
          layout.extraTop, std::max(0, metrics.top - metrics.ascent));
      layout.extraBottom = std::max(
          layout.extraBottom, std::max(0, metrics.bottom - metrics.descent));
      layout.valid = layout.line > 0;
    };
    consider(face);
    // Default family is Roboto. CJK glyphs paint taller; take the fallback
    // line box so lines do not overlap, but keep Roboto includePad extras
    // (Noto OS/2 top/bottom made unicode boxes much taller than Pixel).
    const auto scripts = detectScriptFallbacks(run.text);
    if (scripts.han) {
      const auto style = skFontStyle(resolveTypeface(run, platform));
      if (auto cjk = fontMgr.matchFamilyStyle("Noto Sans CJK SC", style)) {
        const auto metrics = androidFontMetricsPx(std::move(cjk), fontSizePx);
        fallbackLine = std::max(
            fallbackLine, metrics.ascent + metrics.descent);
        layout.line = std::max(layout.line, fallbackLine);
      }
    }
  }
  layout.primaryLine = primaryLine;
  layout.fallbackLine = fallbackLine;
  return layout;
}

bool boundaryLineHasHan(const TextParagraph& paragraph, bool first) {
  std::string text;
  for (const auto& run : paragraph.runs) {
    if (!run.attachment) {
      text += run.text;
    }
  }
  const auto newline = first ? text.find('\n') : text.rfind('\n');
  const auto line = first
      ? text.substr(0, newline)
      : text.substr(newline == std::string::npos ? 0 : newline + 1);
  return detectScriptFallbacks(line).han;
}

AndroidFontPadding androidFontPadding(
    SkFontMgr& fontMgr,
    const TextParagraph& paragraph,
    TextFontPlatform platform,
    float pointScaleFactor = 1.0f) {
  if (platform != TextFontPlatform::Android ||
      !paragraph.includeFontPadding) {
    return {};
  }
  const auto layoutPx = androidLayoutPx(
      fontMgr, paragraph, platform, pointScaleFactor);
  if (layoutPx.valid && pointScaleFactor > 0) {
    // StaticLayout applies top/bottom padding from the first/last line's
    // envelope. A CJK fallback line already spans Roboto's complete
    // top..bottom box, but a later CJK line must not remove padding from an
    // earlier Latin line (TextExample/textTransform mixes exactly that).
    const bool fallbackCoversPrimary =
        layoutPx.fallbackLine >=
        layoutPx.primaryLine + layoutPx.extraTop + layoutPx.extraBottom;
    return {
        .top = static_cast<float>(
            fallbackCoversPrimary && boundaryLineHasHan(paragraph, true)
                ? 0
                : layoutPx.extraTop) /
            pointScaleFactor,
        .bottom = static_cast<float>(
            fallbackCoversPrimary && boundaryLineHasHan(paragraph, false)
                ? 0
                : layoutPx.extraBottom) /
            pointScaleFactor,
    };
  }
  // CustomLineHeightSpan matches first/last top/bottom to ascent/descent.
  for (const auto& run : paragraph.runs) {
    if (run.lineHeight) {
      return {};
    }
  }
  // Android StaticLayout includePad extras come from the line envelope
  // (max span metrics), not the first fragment. Mixed 8/23 must pad from
  // size 23 or the line box is too tight around the large run.
  AndroidFontPadding padding;
  for (const auto& run : paragraph.runs) {
    if (run.attachment) {
      continue;
    }
    auto face = typefaceForRun(fontMgr, run, platform);
    if (!face) {
      continue;
    }
    const auto fontSize = std::max(
        run.fontSize * std::max(run.fontSizeMultiplier, 0.01f), 1.0f);
    const auto metrics = androidFontMetricsInt(*face, fontSize);
    float top = 0;
    float bottom = 0;
    if (metrics.ascent > 0 || metrics.descent > 0) {
      top = std::max(0.0f, metrics.winAscent - metrics.ascent);
      bottom = std::max(0.0f, metrics.winDescent - metrics.descent);
    } else {
      SkFont font(std::move(face), fontSize);
      SkFontMetrics skMetrics;
      font.getMetrics(&skMetrics);
      top = std::max(0.0f, -skMetrics.fTop + skMetrics.fAscent);
      bottom = std::max(0.0f, skMetrics.fBottom - skMetrics.fDescent);
    }
    padding.top = std::max(padding.top, top);
    padding.bottom = std::max(padding.bottom, bottom);
  }
  return padding;
}

std::size_t paragraphUtf16Length(const TextParagraph& paragraph) {
  std::string text;
  for (const auto& run : paragraph.runs) {
    if (!run.attachment) {
      text.append(run.text);
    }
  }
  if (text.empty()) {
    return 0;
  }
  return SkUnicode::convertUtf8ToUtf16(text.data(), text.size()).size();
}

float largestRunFontSize(const TextParagraph& paragraph) {
  float largest = 0;
  for (const auto& run : paragraph.runs) {
    if (!run.attachment) {
      largest = std::max(largest, run.fontSize);
    }
  }
  return largest;
}

TextParagraph scaleParagraphFontSizes(
    const TextParagraph& paragraph,
    float factor,
    float minimumFontSize) {
  auto scaled = paragraph;
  for (auto& run : scaled.runs) {
    if (run.attachment) {
      continue;
    }
    run.fontSize = std::max(run.fontSize * factor, minimumFontSize);
    if (run.lineHeight) {
      run.lineHeight = std::max(*run.lineHeight * factor, 0.01f);
    }
  }
  return scaled;
}

bool isHighSurrogate(char16_t unit) {
  return (unit & 0xFC00) == 0xD800;
}

bool isLowSurrogate(char16_t unit) {
  return (unit & 0xFC00) == 0xDC00;
}

size_t alignUtf16Start(const std::u16string& text, size_t index) {
  if (index > 0 && index < text.size() && isLowSurrogate(text[index])) {
    return index - 1;
  }
  return index;
}

size_t alignUtf16End(const std::u16string& text, size_t index) {
  if (index > 0 && index < text.size() && isHighSurrogate(text[index - 1]) &&
      isLowSurrogate(text[index])) {
    return index + 1;
  }
  return index;
}

struct FlattenedParagraph {
  std::u16string utf16;
  std::vector<int> runIndex;
};

FlattenedParagraph flattenParagraph(const TextParagraph& paragraph) {
  FlattenedParagraph flat;
  for (int index = 0; index < static_cast<int>(paragraph.runs.size()); ++index) {
    const auto& run = paragraph.runs[static_cast<size_t>(index)];
    if (run.attachment) {
      flat.utf16.push_back(u'\uFFFC');
      flat.runIndex.push_back(index);
      continue;
    }
    if (run.text.empty()) {
      continue;
    }
    const auto units = SkUnicode::convertUtf8ToUtf16(
        run.text.data(), static_cast<int>(run.text.size()));
    for (const char16_t unit : units) {
      flat.utf16.push_back(unit);
      flat.runIndex.push_back(index);
    }
  }
  return flat;
}

const TextRun* ellipsisStyleRun(const TextParagraph& source, int runIndex) {
  if (runIndex >= 0 &&
      runIndex < static_cast<int>(source.runs.size()) &&
      !source.runs[static_cast<size_t>(runIndex)].attachment) {
    return &source.runs[static_cast<size_t>(runIndex)];
  }
  for (const auto& run : source.runs) {
    if (!run.attachment) {
      return &run;
    }
  }
  return nullptr;
}

TextRun makeEllipsisRun(const TextRun* style) {
  TextRun run;
  if (style != nullptr) {
    run = *style;
  }
  run.text = "…";
  run.attachment = false;
  return run;
}

void appendUtf16Range(
    TextParagraph& out,
    const TextParagraph& source,
    const FlattenedParagraph& flat,
    size_t start,
    size_t end) {
  if (flat.utf16.empty()) {
    return;
  }
  start = std::min(start, flat.utf16.size());
  end = std::min(end, flat.utf16.size());
  if (start >= end) {
    return;
  }
  start = alignUtf16Start(flat.utf16, start);
  end = alignUtf16End(flat.utf16, end);
  int currentRun = -2;
  std::u16string buffer;
  const auto flush = [&]() {
    if (currentRun < 0 ||
        currentRun >= static_cast<int>(source.runs.size())) {
      buffer.clear();
      return;
    }
    TextRun run = source.runs[static_cast<size_t>(currentRun)];
    if (!run.attachment) {
      const auto utf8 = SkUnicode::convertUtf16ToUtf8(buffer);
      run.text.assign(utf8.c_str(), utf8.size());
    }
    out.runs.push_back(std::move(run));
    buffer.clear();
  };
  for (size_t index = start; index < end; ++index) {
    const int runIndex = flat.runIndex[index];
    if (runIndex != currentRun) {
      flush();
      currentRun = runIndex;
      if (currentRun >= 0 &&
          currentRun < static_cast<int>(source.runs.size()) &&
          source.runs[static_cast<size_t>(currentRun)].attachment) {
        out.runs.push_back(source.runs[static_cast<size_t>(currentRun)]);
        currentRun = -2;
        continue;
      }
    }
    buffer.push_back(flat.utf16[index]);
  }
  flush();
}

bool isWordSeparator(char16_t unit) {
  return unit == u' ' || unit == u'\t';
}

// Android TruncateAt.END keeps the wrap space before U+2026.
TextParagraph withTailEllipsis(const TextParagraph& source, size_t end) {
  const auto flat = flattenParagraph(source);
  const auto length = flat.utf16.size();
  end = std::min(end, length);
  if (end >= length) {
    return source;
  }
  if (end > 0 && !isWordSeparator(flat.utf16[end - 1])) {
    while (end < length && isWordSeparator(flat.utf16[end])) {
      ++end;
    }
  }
  if (end >= length) {
    return source;
  }
  TextParagraph out = source;
  out.runs.clear();
  out.ellipsizeMode = TextEllipsizeMode::Clip;
  appendUtf16Range(out, source, flat, 0, end);
  const int styleIndex = end > 0
      ? flat.runIndex[end - 1]
      : (flat.runIndex.empty() ? -1 : flat.runIndex.front());
  out.runs.push_back(makeEllipsisRun(ellipsisStyleRun(source, styleIndex)));
  return out;
}

TextParagraph withHeadEllipsis(const TextParagraph& source, size_t keep) {
  const auto flat = flattenParagraph(source);
  const auto length = flat.utf16.size();
  keep = std::min(keep, length);
  if (keep >= length) {
    return source;
  }
  TextParagraph out = source;
  out.runs.clear();
  out.ellipsizeMode = TextEllipsizeMode::Clip;
  const int styleIndex = keep == 0
      ? (flat.runIndex.empty() ? -1 : flat.runIndex.back())
      : flat.runIndex[length - keep];
  out.runs.push_back(makeEllipsisRun(ellipsisStyleRun(source, styleIndex)));
  appendUtf16Range(out, source, flat, length - keep, length);
  return out;
}

TextParagraph withMiddleEllipsis(const TextParagraph& source, size_t keep) {
  const auto flat = flattenParagraph(source);
  const auto length = flat.utf16.size();
  keep = std::min(keep, length);
  if (keep >= length) {
    return source;
  }
  const size_t left = (keep + 1) / 2;
  const size_t right = keep - left;
  TextParagraph out = source;
  out.runs.clear();
  out.ellipsizeMode = TextEllipsizeMode::Clip;
  appendUtf16Range(out, source, flat, 0, left);
  const int styleIndex = left > 0
      ? flat.runIndex[left - 1]
      : (right > 0 ? flat.runIndex[length - right] : -1);
  out.runs.push_back(makeEllipsisRun(ellipsisStyleRun(source, styleIndex)));
  appendUtf16Range(out, source, flat, length - right, length);
  return out;
}

bool ellipsisLayoutFits(
    const SkiaPreparedParagraph& laid,
    std::size_t maxLines,
    float width) {
  if (maxLines > 0 && laid.lineCount() > maxLines) {
    return false;
  }
  return laid.longestLine() <= width;
}

bool containsFontFile(const std::filesystem::path& directory) {
  std::error_code error;
  if (!std::filesystem::is_directory(directory, error) || error) {
    return false;
  }
  std::filesystem::recursive_directory_iterator iterator(
      directory,
      std::filesystem::directory_options::skip_permission_denied,
      error);
  const std::filesystem::recursive_directory_iterator end;
  while (!error && iterator != end) {
    if (iterator->is_regular_file(error) && !error) {
      auto extension = iterator->path().extension().string();
      std::transform(
          extension.begin(),
          extension.end(),
          extension.begin(),
          [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
          });
      if (extension == ".ttf" || extension == ".ttc" ||
          extension == ".otf") {
        return true;
      }
    }
    iterator.increment(error);
  }
  return false;
}

std::recursive_mutex& skiaLayoutMutex() {
  static std::recursive_mutex mutex;
  return mutex;
}

enum class ScriptFontBit : unsigned {
  Core = 1u << 0,
  Han = 1u << 1,
  Emoji = 1u << 2,
  Arabic = 1u << 3,
  Hebrew = 1u << 4,
  Devanagari = 1u << 5,
  Thai = 1u << 6,
};

struct SharedAndroidFonts {
  std::mutex mutex;
  std::filesystem::path directory;
  sk_sp<SkFontMgr> loader;
  sk_sp<skia::textlayout::TypefaceFontProvider> provider;
  unsigned loaded{0};

  void registerFile(
      const char* filename,
      std::initializer_list<const char*> aliases,
      int ttcIndex = 0) {
    const auto path = directory / filename;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
      return;
    }
    auto face = loader->makeFromFile(path.c_str(), ttcIndex);
    if (!face) {
      return;
    }
    if (aliases.size() == 0) {
      provider->registerTypeface(face);
      return;
    }
    for (const char* alias : aliases) {
      provider->registerTypeface(face, SkString(alias));
    }
  }

  void registerTtc(const char* filename) {
    for (int index = 0; index < 8; ++index) {
      const auto path = directory / filename;
      std::error_code error;
      if (!std::filesystem::is_regular_file(path, error) || error) {
        return;
      }
      auto face = loader->makeFromFile(path.c_str(), index);
      if (!face) {
        break;
      }
      provider->registerTypeface(face);
      SkString name;
      face->getFamilyName(&name);
      if (!name.isEmpty()) {
        provider->registerTypeface(face, name);
      }
    }
  }

  bool registerRobotoVariable() {
    const auto path = directory / "Roboto-Regular.ttf";
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
      return false;
    }
    auto base = loader->makeFromFile(path.c_str(), 0);
    if (!base) {
      return false;
    }
    bool registered = false;
    for (const int weight : {100, 200, 300, 400, 500, 600, 700, 800, 900}) {
      const SkFontArguments::VariationPosition::Coordinate coordinate{
          .axis = SkFontArguments::VariationPosition::Coordinate::wght,
          .value = static_cast<float>(weight),
      };
      SkFontArguments arguments;
      arguments.setVariationDesignPosition({&coordinate, 1});
      auto face = base->makeClone(arguments);
      if (!face) {
        continue;
      }
      provider->registerTypeface(face, SkString("Roboto"));
      provider->registerTypeface(face, SkString("sans-serif"));
      registered = true;
    }
    return registered;
  }

  void ensure(unsigned bits) {
    std::lock_guard lock(mutex);
    if ((loaded & bits) == bits) {
      return;
    }
    if ((bits & static_cast<unsigned>(ScriptFontBit::Core)) &&
        !(loaded & static_cast<unsigned>(ScriptFontBit::Core))) {
      if (!registerRobotoVariable()) {
        registerFile("RobotoStatic-Regular.ttf", {"Roboto", "sans-serif"});
      }
      registerFile("NotoSerif-Regular.ttf", {"Noto Serif", "serif"});
      registerFile("NotoSerif-Bold.ttf", {"Noto Serif"});
      registerFile("NotoSerif-Italic.ttf", {"Noto Serif"});
      registerFile("NotoSerif-BoldItalic.ttf", {"Noto Serif"});
      registerFile("notoserif.ttf", {"Noto Serif", "notoserif"});
      registerFile("notoserif_bold_italic.ttf", {"Noto Serif", "notoserif"});
      registerFile("DroidSansMono.ttf", {"Droid Sans Mono", "monospace"});
      registerFile("CutiveMono.ttf", {"Cutive Mono", "monospace"});
      registerFile("Rubik-Regular.ttf", {"Rubik"});
      registerFile("Rubik-Bold.ttf", {"Rubik"});
      registerFile("Rubik-Light.ttf", {"Rubik Light", "Rubik"});
      registerFile("Rubik-Medium.ttf", {"Rubik Medium", "Rubik"});
      registerFile("Rubik-MediumItalic.ttf", {"Rubik Medium", "Rubik"});
      registerFile("FiraCode-Regular.ttf", {"Fira Code", "firacode"});
      loaded |= static_cast<unsigned>(ScriptFontBit::Core);
    }
    if ((bits & static_cast<unsigned>(ScriptFontBit::Arabic)) &&
        !(loaded & static_cast<unsigned>(ScriptFontBit::Arabic))) {
      registerFile(
          "NotoNaskhArabic-Regular.ttf", {"Noto Naskh Arabic"});
      registerFile("NotoNaskhArabic-Bold.ttf", {"Noto Naskh Arabic"});
      loaded |= static_cast<unsigned>(ScriptFontBit::Arabic);
    }
    if ((bits & static_cast<unsigned>(ScriptFontBit::Hebrew)) &&
        !(loaded & static_cast<unsigned>(ScriptFontBit::Hebrew))) {
      registerFile("NotoSansHebrew-Regular.ttf", {"Noto Sans Hebrew"});
      registerFile("NotoSansHebrew-Bold.ttf", {"Noto Sans Hebrew"});
      loaded |= static_cast<unsigned>(ScriptFontBit::Hebrew);
    }
    if ((bits & static_cast<unsigned>(ScriptFontBit::Devanagari)) &&
        !(loaded & static_cast<unsigned>(ScriptFontBit::Devanagari))) {
      registerFile(
          "NotoSansDevanagariUI-VF.ttf", {"Noto Sans Devanagari UI"});
      registerFile(
          "NotoSansDevanagari-VF.ttf", {"Noto Sans Devanagari"});
      loaded |= static_cast<unsigned>(ScriptFontBit::Devanagari);
    }
    if ((bits & static_cast<unsigned>(ScriptFontBit::Thai)) &&
        !(loaded & static_cast<unsigned>(ScriptFontBit::Thai))) {
      registerFile("NotoSansThaiUI-Regular.ttf", {"Noto Sans Thai UI"});
      registerFile("NotoSansThaiUI-Bold.ttf", {"Noto Sans Thai UI"});
      registerFile("NotoSansThai-Regular.ttf", {"Noto Sans Thai"});
      registerFile("NotoSansThai-Bold.ttf", {"Noto Sans Thai"});
      loaded |= static_cast<unsigned>(ScriptFontBit::Thai);
    }
    if ((bits & static_cast<unsigned>(ScriptFontBit::Han)) &&
        !(loaded & static_cast<unsigned>(ScriptFontBit::Han))) {
      registerTtc("NotoSansCJK-Regular.ttc");
      registerFile(
          "NotoSansCJK-Regular.ttc",
          {"Noto Sans CJK SC",
           "Noto Sans CJK JP",
           "Noto Sans CJK KR",
           "Noto Sans CJK TC"},
          0);
      loaded |= static_cast<unsigned>(ScriptFontBit::Han);
    }
    if ((bits & static_cast<unsigned>(ScriptFontBit::Emoji)) &&
        !(loaded & static_cast<unsigned>(ScriptFontBit::Emoji))) {
      registerFile("NotoColorEmoji.ttf", {"Noto Color Emoji"});
      registerFile("NotoColorEmojiFlags.ttf", {"Noto Color Emoji Flags"});
      loaded |= static_cast<unsigned>(ScriptFontBit::Emoji);
    }
  }
};

std::shared_ptr<SharedAndroidFonts> sharedAndroidFonts(
    const std::filesystem::path& directory) {
  static std::mutex mutex;
  static std::weak_ptr<SharedAndroidFonts> cached;
  static std::filesystem::path cachedDirectory;
  std::lock_guard lock(mutex);
  auto held = cached.lock();
  if (held && cachedDirectory == directory) {
    return held;
  }
  held = std::make_shared<SharedAndroidFonts>();
  held->directory = directory;
  held->loader = SkFontMgr_New_Custom_Empty();
  held->provider = sk_make_sp<skia::textlayout::TypefaceFontProvider>();
  if (held->loader == nullptr || held->provider == nullptr) {
    throw std::runtime_error("Skia could not create the Android font provider");
  }
  held->ensure(static_cast<unsigned>(ScriptFontBit::Core));
  if (held->provider->countFamilies() == 0) {
    throw std::runtime_error(
        "Skia could not load fonts from " + directory.string());
  }
  cached = held;
  cachedDirectory = directory;
  return held;
}

} // namespace

void validateSkiaFontDirectory(
    const std::filesystem::path& fontDirectory) {
  if (fontDirectory.empty()) {
    return;
  }
  if (!containsFontFile(fontDirectory)) {
    throw std::runtime_error(
        "Skia font directory contains no .ttf, .ttc, or .otf files: " +
        fontDirectory.string());
  }
}

float snapPlaceholderExtent(float value) {
  // SkParagraph placeholder rects can land 1 ulp below the requested size on
  // FreeType/FontConfig. Yoga and retained-scene consumers need the size that
  // was passed to PlaceholderStyle.
  return std::round(value * 64.0f) / 64.0f;
}

struct PreparedWavySpan {
  std::size_t utf16Start{0};
  std::size_t utf16End{0};
  bool underline{false};
  bool strikethrough{false};
  SkColor color{SK_ColorBLACK};
  float thickness{1};
  std::vector<skia::textlayout::TextBox> boxes;
};

struct PreparedBackgroundSpan {
  std::size_t utf16Start{0};
  std::size_t utf16End{0};
  SkColor color{SK_ColorTRANSPARENT};
  float extraTop{0};
  float extraBottom{0};
  std::vector<skia::textlayout::TextBox> boxes;
};

class SkiaPreparedParagraph::Impl {
 public:
  Impl(
      std::unique_ptr<skia::textlayout::Paragraph> paragraph,
      float boxWidth,
      float wrapWidth,
      TextWritingDirection writingDirection,
      AndroidFontPadding fontPadding,
      bool hasExplicitLineHeight,
      std::u16string sourceUtf16,
      std::vector<PreparedWavySpan> wavy,
      bool paintTailEllipsis = false)
      : paragraph(std::move(paragraph)),
        layoutWidth(boxWidth),
        resolvedWritingDirection(writingDirection),
        fontPaddingTop(fontPadding.top),
        wavySpans(std::move(wavy)) {
    this->paragraph->layout(wrapWidth);
    const auto placeholderBoxes = this->paragraph->getRectsForPlaceholders();
    const auto rawFirstBaseline = this->paragraph->getAlphabeticBaseline();
    // ReplacementSpan.getSize sets top=ascent=-height. When a tall inline
    // host owns the first line ascent, StaticLayout's includePad top is
    // already represented by that span and is not added again. Keeping the
    // Roboto top extra shifted only the glyphs 5-6 physical pixels below the
    // attachment baseline and added the same blank tail to Yoga height.
    const bool attachmentOwnsFirstLineTop = std::any_of(
        placeholderBoxes.begin(),
        placeholderBoxes.end(),
        [&](const auto& box) {
          return box.rect.top() <= fontPadding.top + 0.5f &&
              std::fabs(box.rect.bottom() - rawFirstBaseline) <= 0.75f;
        });
    if (attachmentOwnsFirstLineTop) {
      fontPadding.top = 0;
      fontPaddingTop = 0;
    }
    longestLine = this->paragraph->getLongestLine();
    lineCount = this->paragraph->lineNumber();
    maximumIntrinsicWidth = this->paragraph->getMaxIntrinsicWidth();
    // Yoga measure must use StaticLayout-style line width, not
    // getRectsForRange. RTL (Hebrew) rect queries on every measure both
    // stall the JS thread and can report the full box width, so wrap_content
    // and paint disagree.
    measuredWidth = std::min(
        std::max(longestLine, maximumIntrinsicWidth), boxWidth);
    baseline = rawFirstBaseline + fontPadding.top;
    firstLineAscent = baseline;
    exceededMaximumLines = this->paragraph->didExceedMaxLines();
    for (auto& span : wavySpans) {
      if (span.utf16End <= span.utf16Start) {
        continue;
      }
      span.boxes = this->paragraph->getRectsForRange(
          span.utf16Start,
          span.utf16End,
          RectHeightStyle::kTight,
          RectWidthStyle::kTight);
    }
    for (const auto& box : placeholderBoxes) {
      attachments.push_back({
          .x = box.rect.x(),
          .y = box.rect.y(),
          .width = snapPlaceholderExtent(box.rect.width()),
          .height = snapPlaceholderExtent(box.rect.height()),
          .clipped = false});
    }
    // SkParagraph keeps U+FFFC placeholders in logical order inside an LTR
    // island. Android's RTL-dominant bidi reverses those inline views, so
    // swap same-line attachment x positions when the paragraph is RTL.
    if (resolvedWritingDirection == TextWritingDirection::RightToLeft &&
        attachments.size() >= 2) {
      std::vector<std::size_t> order(attachments.size());
      std::iota(order.begin(), order.end(), 0);
      std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (std::fabs(attachments[a].y - attachments[b].y) > 2.0f) {
          return attachments[a].y < attachments[b].y;
        }
        return a < b;
      });
      std::size_t lineStart = 0;
      while (lineStart < order.size()) {
        std::size_t lineEnd = lineStart + 1;
        while (lineEnd < order.size() &&
               std::fabs(
                   attachments[order[lineEnd]].y -
                   attachments[order[lineStart]].y) <= 2.0f) {
          ++lineEnd;
        }
        if (lineEnd - lineStart >= 2) {
          std::vector<float> xs;
          xs.reserve(lineEnd - lineStart);
          for (std::size_t i = lineStart; i < lineEnd; ++i) {
            xs.push_back(attachments[order[i]].x);
          }
          std::sort(xs.begin(), xs.end());
          for (std::size_t i = lineStart; i < lineEnd; ++i) {
            attachments[order[i]].x = xs[lineEnd - 1 - i];
          }
        }
        lineStart = lineEnd;
      }
    }
    std::vector<skia::textlayout::LineMetrics> metrics;
    this->paragraph->getLineMetrics(metrics);
    for (const auto& metric : metrics) {
      const auto ascent = std::abs(static_cast<float>(metric.fAscent));
      const auto descent = std::abs(static_cast<float>(metric.fDescent));
      std::string lineText;
      if (metric.fStartIndex < sourceUtf16.size() &&
          metric.fEndIndex > metric.fStartIndex) {
        const auto end = std::min(metric.fEndIndex, sourceUtf16.size());
        const std::u16string slice(
            sourceUtf16.begin() +
                static_cast<std::ptrdiff_t>(metric.fStartIndex),
            sourceUtf16.begin() + static_cast<std::ptrdiff_t>(end));
        const auto utf8 = SkUnicode::convertUtf16ToUtf8(slice);
        lineText.assign(utf8.c_str(), utf8.size());
      }
      // SkParagraph paints U+2026 from setEllipsis but LineMetrics stay on
      // the source UTF-16 range. Keep glance-level line text in sync.
      if (paintTailEllipsis && exceededMaximumLines &&
          lines.size() + 1 == metrics.size() &&
          lineText.find("…") == std::string::npos) {
        lineText += "…";
      }
      // SkParagraph LineMetrics.fHeight is round(ascent+descent) in dp.
      // Android StaticLayout uses unrounded FontMetricsInt ascent/descent per
      // interior line; includePad extras are first/last only.
      float lineHeight = ascent + descent;
      if (hasExplicitLineHeight && metric.fHeight > 0) {
        // Android LineHeightStyleSpan can intentionally make a line shorter
        // than the glyph box (RN Tester's 16sp/lineHeight:8 fixture). Using
        // ascent+descent as a lower bound made five 8dp lines measure 56dp
        // instead of 40dp and pushed every following case down.
        lineHeight = static_cast<float>(metric.fHeight);
      } else if (static_cast<float>(metric.fHeight) > lineHeight + 0.51f) {
        lineHeight = static_cast<float>(metric.fHeight);
      }
      lines.push_back({
          .left = static_cast<float>(metric.fLeft),
          .width = static_cast<float>(metric.fWidth),
          .height = lineHeight,
          .baseline = static_cast<float>(metric.fBaseline) + fontPadding.top,
          .ascent = ascent,
          .descent = descent,
          .utf16Start = metric.fStartIndex,
          .utf16End = metric.fEndIndex,
          .text = std::move(lineText)});
    }
    float typographicHeight = 0;
    for (const auto& line : lines) {
      typographicHeight += line.height;
    }
    measuredHeight = (typographicHeight > 0
                          ? typographicHeight
                          : this->paragraph->getHeight()) +
        fontPadding.top + fontPadding.bottom;
  }

  mutable std::mutex mutex;
  std::unique_ptr<skia::textlayout::Paragraph> paragraph;
  float layoutWidth{0};
  float measuredWidth{0};
  float measuredHeight{0};
  float longestLine{0};
  float maximumIntrinsicWidth{0};
  float baseline{0};
  float firstLineAscent{0};
  std::size_t lineCount{0};
  TextWritingDirection resolvedWritingDirection{
      TextWritingDirection::LeftToRight};
  bool exceededMaximumLines{false};
  bool widthIndependentOrigin{false};
  bool alignmentDependsOnWidth{false};
  mutable std::atomic<bool> measured{false};
  float fontPaddingTop{0};
  std::vector<PreparedTextAttachment> attachments;
  std::vector<PreparedTextLine> lines;
  std::vector<PreparedBackgroundSpan> backgroundSpans;
  std::vector<PreparedWavySpan> wavySpans;
};

SkiaPreparedParagraph::SkiaPreparedParagraph(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
SkiaPreparedParagraph::~SkiaPreparedParagraph() = default;
SkiaPreparedParagraph::SkiaPreparedParagraph(
    SkiaPreparedParagraph&&) noexcept = default;
SkiaPreparedParagraph& SkiaPreparedParagraph::operator=(
    SkiaPreparedParagraph&&) noexcept = default;
float SkiaPreparedParagraph::width() const noexcept {
  return impl_->measuredWidth;
}
float SkiaPreparedParagraph::height() const noexcept {
  return impl_->measuredHeight;
}
float SkiaPreparedParagraph::longestLine() const noexcept {
  return impl_->longestLine;
}
float SkiaPreparedParagraph::alphabeticBaseline() const noexcept {
  return impl_->baseline;
}
float SkiaPreparedParagraph::firstLineAscent() const noexcept {
  return impl_->firstLineAscent;
}
float SkiaPreparedParagraph::layoutWidth() const noexcept {
  return impl_->layoutWidth;
}
std::size_t SkiaPreparedParagraph::lineCount() const noexcept {
  return impl_->lineCount;
}
TextWritingDirection SkiaPreparedParagraph::writingDirection() const noexcept {
  return impl_->resolvedWritingDirection;
}
bool SkiaPreparedParagraph::exceededMaximumLines() const noexcept {
  return impl_->exceededMaximumLines;
}
bool SkiaPreparedParagraph::canPaintAtWidth(float width) const noexcept {
  if (!std::isfinite(width) || width <= 0) {
    return false;
  }
  if (std::fabs(width - impl_->layoutWidth) < 0.01f) {
    return true;
  }
  // Center/right/end were laid out in the Yoga AT_MOST box. Reusing that
  // paragraph at wrap_content width paints the centered glyphs into a
  // clip the size of the ink (RN Tester title "Text" -> "Tex").
  if (impl_->alignmentDependsOnWidth) {
    return false;
  }
  return impl_->widthIndependentOrigin &&
      impl_->maximumIntrinsicWidth <= width + 0.01f;
}
void SkiaPreparedParagraph::markMeasured() const noexcept {
  impl_->measured.store(true, std::memory_order_relaxed);
}
bool SkiaPreparedParagraph::wasMeasured() const noexcept {
  return impl_->measured.load(std::memory_order_relaxed);
}
const std::vector<PreparedTextAttachment>&
SkiaPreparedParagraph::attachments() const noexcept {
  return impl_->attachments;
}
const std::vector<PreparedTextLine>&
SkiaPreparedParagraph::lines() const noexcept {
  return impl_->lines;
}
void SkiaPreparedParagraph::retainUnconstrainedIntrinsicWidth(
    float unconstrainedWidth) {
  if (!std::isfinite(unconstrainedWidth) || unconstrainedWidth <= 0) {
    return;
  }
  impl_->maximumIntrinsicWidth =
      std::max(impl_->maximumIntrinsicWidth, unconstrainedWidth);
  impl_->measuredWidth = std::min(
      std::max(impl_->longestLine, impl_->maximumIntrinsicWidth),
      impl_->layoutWidth);
}

float SkiaPreparedParagraph::widthForUtf16Range(
    std::size_t start,
    std::size_t end) const {
  std::lock_guard engineLock(skiaLayoutMutex());
  std::lock_guard lock(impl_->mutex);
  if (end <= start) {
    return 0;
  }
  const auto boxes = impl_->paragraph->getRectsForRange(
      start,
      end,
      RectHeightStyle::kTight,
      RectWidthStyle::kTight);
  float width = 0;
  for (const auto& box : boxes) {
    width += box.rect.width();
  }
  return width;
}
void SkiaPreparedParagraph::paint(SkCanvas& canvas, float x, float y) const {
  // Do not take skiaLayoutMutex: Yoga measure on the JS thread shares that
  // lock. Painting an already-laid-out paragraph only needs the instance
  // mutex. Holding the global lock here froze Hebrew/RTL and scroll.
  std::lock_guard lock(impl_->mutex);
  const auto originY = y + impl_->fontPaddingTop;
  // SkParagraph's Flutter half-letter-spacing shifts the run right by
  // spacing/2. Android/CSS omit that leading edge, so pull LTR
  // left-aligned lines back and keep the last glyph inside the box.
  float originX = x;
  if (impl_->resolvedWritingDirection ==
          TextWritingDirection::LeftToRight &&
      !impl_->lines.empty() && impl_->lines.front().left > 0) {
    const float inset = impl_->lines.front().left;
    const float extra =
        std::max(0.0f, impl_->layoutWidth - impl_->longestLine);
    // Center in a wide box: extra ≈ 2*inset. Right: extra ≈ inset.
    // Wrap_content: extra ≈ 0 and line.left is Flutter half-letter-spacing.
    // Using extra > inset + 1 treated right-align as wrap_content and
    // pulled "right right…" back to the left edge.
    if (!(impl_->alignmentDependsOnWidth && extra > 1.0f)) {
      originX -= inset;
    }
  }
  // SkParagraph's native run background uses the typographic ascent/descent.
  // Android BackgroundColorSpan uses FontMetricsInt top/bottom for a padded
  // first/last line, which is visibly taller for nested large Text runs.
  for (const auto& span : impl_->backgroundSpans) {
    SkPaint paint;
    paint.setColor(span.color);
    for (const auto& box : span.boxes) {
      auto rect = box.rect;
      rect.fTop -= span.extraTop;
      rect.fBottom += span.extraBottom;
      canvas.drawRect(rect.makeOffset(originX, originY), paint);
    }
  }
  impl_->paragraph->paint(&canvas, originX, originY);
  for (const auto& span : impl_->wavySpans) {
    for (const auto& box : span.boxes) {
      const auto left = originX + box.rect.left();
      const auto right = originX + box.rect.right();
      if (span.underline) {
        float underlineY = box.rect.bottom() - span.thickness * 0.2f;
        // A tight glyph box can end at the Paragraph's clip edge (notably the
        // combined underline+line-through fixture). Keep the complete wave,
        // including stroke radius, inside its StaticLayout line box instead
        // of letting overflow:hidden flatten its lower half.
        for (const auto& line : impl_->lines) {
          const auto baseline = line.baseline - impl_->fontPaddingTop;
          const auto lineTop = baseline - line.ascent;
          const auto lineBottom = baseline + line.descent;
          if (box.rect.centerY() >= lineTop - 0.5f &&
              box.rect.centerY() <= lineBottom + 0.5f) {
            underlineY = std::min(
                underlineY,
                lineBottom - wavyHalfExtent(span.thickness));
            break;
          }
        }
        drawWavyLine(
            canvas,
            left,
            right,
            originY + underlineY,
            span.thickness,
            span.color);
      }
      if (span.strikethrough) {
        drawWavyLine(
            canvas,
            left,
            right,
            originY + box.rect.centerY(),
            span.thickness,
            span.color);
      }
    }
  }
}

class SkiaTextLayoutEngine::Impl {
 public:
  explicit Impl(
      const std::filesystem::path& fontDirectory,
      TextFontPlatform platform)
      : platform(platform) {
    fonts = sk_make_sp<FontCollection>();
    unicode = SkUnicodes::ICU::Make();
    if (unicode == nullptr) {
      throw std::runtime_error("Skia ICU unicode backend is unavailable");
    }
    if (!fontDirectory.empty()) {
      androidFonts = sharedAndroidFonts(fontDirectory);
      fontMgr = androidFonts->provider;
      fonts->setAssetFontManager(fontMgr);
      fonts->setDefaultFontManager(fontMgr, defaultFontFamilies(platform));
    } else {
#if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
      fontMgr = SkFontMgr_New_CoreText(nullptr);
#elif defined(__linux__)
      fontMgr = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
#else
      fontMgr = SkFontMgr_New_Custom_Empty();
#endif
      fonts->setDefaultFontManager(fontMgr, defaultFontFamilies(platform));
    }
    fonts->enableFontFallback();
  }

  void ensureScriptFonts(const ScriptFallbacks& needs) const {
    if (!androidFonts) {
      return;
    }
    unsigned bits = static_cast<unsigned>(ScriptFontBit::Core);
    if (needs.han) {
      bits |= static_cast<unsigned>(ScriptFontBit::Han);
    }
    if (needs.emoji) {
      bits |= static_cast<unsigned>(ScriptFontBit::Emoji);
    }
    if (needs.arabic) {
      bits |= static_cast<unsigned>(ScriptFontBit::Arabic);
    }
    if (needs.hebrew) {
      bits |= static_cast<unsigned>(ScriptFontBit::Hebrew);
    }
    if (needs.devanagari) {
      bits |= static_cast<unsigned>(ScriptFontBit::Devanagari);
    }
    if (needs.thai) {
      bits |= static_cast<unsigned>(ScriptFontBit::Thai);
    }
    androidFonts->ensure(bits);
  }

  sk_sp<FontCollection> fonts;
  sk_sp<SkFontMgr> fontMgr;
  sk_sp<SkUnicode> unicode;
  TextFontPlatform platform{TextFontPlatform::Generic};
  std::shared_ptr<SharedAndroidFonts> androidFonts;
};

SkiaTextLayoutEngine::SkiaTextLayoutEngine(
    std::filesystem::path fontDirectory,
    TextFontPlatform platform)
    : impl_(std::make_unique<Impl>(fontDirectory, platform)) {}
SkiaTextLayoutEngine::~SkiaTextLayoutEngine() = default;
SkiaTextLayoutEngine::SkiaTextLayoutEngine(
    SkiaTextLayoutEngine&&) noexcept = default;
SkiaTextLayoutEngine& SkiaTextLayoutEngine::operator=(
    SkiaTextLayoutEngine&&) noexcept = default;

void appendCodepoint(std::string& out, UChar32 cp) {
  char bytes[4];
  int32_t length = 0;
  U8_APPEND_UNSAFE(bytes, length, cp);
  out.append(bytes, static_cast<std::size_t>(length));
}

std::string applyTextTransformUtf8(
    const std::string& text,
    TextTransform transform) {
  if (transform == TextTransform::None || text.empty()) {
    return text;
  }
  std::string out;
  out.reserve(text.size() + 8);
  int32_t offset = 0;
  const auto size = static_cast<int32_t>(text.size());
  bool capitalizeNext = true;
  while (offset < size) {
    UChar32 codepoint = 0;
    U8_NEXT(text.data(), offset, size, codepoint);
    if (transform == TextTransform::Uppercase) {
      codepoint = u_toupper(codepoint);
    } else if (transform == TextTransform::Lowercase) {
      codepoint = u_tolower(codepoint);
    } else {
      if (u_isalpha(codepoint)) {
        if (capitalizeNext) {
          codepoint = u_toupper(codepoint);
        }
        capitalizeNext = false;
      } else if (u_isdigit(codepoint)) {
        capitalizeNext = false;
      } else {
        capitalizeNext = true;
      }
    }
    appendCodepoint(out, codepoint);
  }
  return out;
}

bool isCjkIdeograph(UChar32 cp) {
  return (cp >= 0x3400 && cp <= 0x9FFF) ||
      (cp >= 0xF900 && cp <= 0xFAFF) ||
      (cp >= 0x3040 && cp <= 0x30FF) ||
      (cp >= 0xAC00 && cp <= 0xD7AF) ||
      (cp >= 0x20000 && cp <= 0x2FA1F);
}

bool isCjkWrapChar(UChar32 cp) {
  if (isCjkIdeograph(cp)) {
    return true;
  }
  // Ideographic full stop / comma so "游戏。星际" can wrap like Android.
  return cp == 0x3002 || cp == 0x3001 || cp == 0xFF0C || cp == 0xFF0E;
}

std::string insertCjkLineBreaks(const std::string& text) {
  if (text.empty()) {
    return text;
  }
  std::string out;
  out.reserve(text.size() * 2);
  int32_t offset = 0;
  const auto size = static_cast<int32_t>(text.size());
  UChar32 previous = 0;
  while (offset < size) {
    UChar32 codepoint = 0;
    U8_NEXT(text.data(), offset, size, codepoint);
    if (previous != 0 && isCjkWrapChar(previous) && isCjkWrapChar(codepoint)) {
      appendCodepoint(out, 0x200B);
    }
    appendCodepoint(out, codepoint);
    previous = codepoint;
  }
  return out;
}

std::string insertZwspBetweenCodepoints(const std::string& token) {
  std::string out;
  out.reserve(token.size() * 4);
  int32_t offset = 0;
  const auto size = static_cast<int32_t>(token.size());
  bool first = true;
  while (offset < size) {
    UChar32 codepoint = 0;
    U8_NEXT(token.data(), offset, size, codepoint);
    if (!first) {
      appendCodepoint(out, 0x200B);
    }
    first = false;
    appendCodepoint(out, codepoint);
  }
  return out;
}

std::string insertCamelCaseHyphens(const std::string& token, int minLowerRun) {
  if (token.empty()) {
    return token;
  }
  std::string out;
  out.reserve(token.size() + 16);
  int32_t offset = 0;
  const auto size = static_cast<int32_t>(token.size());
  UChar32 previous = 0;
  int lowerRun = 0;
  while (offset < size) {
    UChar32 codepoint = 0;
    U8_NEXT(token.data(), offset, size, codepoint);
    if (previous != 0 && u_islower(previous) && u_isupper(codepoint) &&
        lowerRun >= minLowerRun) {
      appendCodepoint(out, 0x00AD);
    }
    appendCodepoint(out, codepoint);
    if (u_islower(codepoint)) {
      ++lowerRun;
    } else {
      lowerRun = 0;
    }
    previous = codepoint;
  }
  return out;
}

std::string applyHyphenation(const std::string& text, TextHyphenation mode) {
  std::string out;
  out.reserve(text.size() + 16);
  std::string token;
  const auto flush = [&]() {
    if (token.empty()) {
      return;
    }
    if (mode == TextHyphenation::None) {
      // Android none still character-wraps an overflowing word, no hyphen.
      out += token.size() >= 24 ? insertZwspBetweenCodepoints(token) : token;
    } else {
      // Normal uses fewer camelCase points so "Breaking-For" wins over
      // "For-NewLine". Full keeps every lower-to-upper boundary.
      const int minLower = mode == TextHyphenation::Normal ? 5 : 1;
      out += insertCamelCaseHyphens(token, minLower);
    }
    token.clear();
  };
  int32_t offset = 0;
  const auto size = static_cast<int32_t>(text.size());
  while (offset < size) {
    UChar32 codepoint = 0;
    U8_NEXT(text.data(), offset, size, codepoint);
    if (u_isspace(codepoint)) {
      flush();
      appendCodepoint(out, codepoint);
    } else {
      appendCodepoint(token, codepoint);
    }
  }
  flush();
  return out;
}

bool startsUrl(const std::string& text, std::size_t index) {
  return text.compare(index, 8, "https://") == 0 ||
      text.compare(index, 7, "http://") == 0;
}

std::size_t consumeToken(const std::string& text, std::size_t index) {
  while (index < text.size()) {
    const unsigned char ch = static_cast<unsigned char>(text[index]);
    if (ch <= 32 || ch == ',' || ch == ';' || ch == ')' || ch == ']') {
      break;
    }
    ++index;
  }
  return index;
}

void applyLinkStyle(TextRun& run) {
  run.decorationLine = TextDecorationLine::Underline;
  run.foregroundColor = TextColor{0.0f, 0.478f, 0.8f, 1.0f};
  run.decorationColor = run.foregroundColor;
}

bool isEmailLocalChar(unsigned char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
      (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '%' ||
      ch == '+' || ch == '-';
}

bool isPhoneDigitRun(const std::string& text, std::size_t index) {
  auto isDigit = [](char ch) { return ch >= '0' && ch <= '9'; };
  return index + 12 <= text.size() && isDigit(text[index]) &&
      isDigit(text[index + 1]) && isDigit(text[index + 2]) &&
      text[index + 3] == '-' && isDigit(text[index + 4]) &&
      isDigit(text[index + 5]) && isDigit(text[index + 6]) &&
      text[index + 7] == '-' && isDigit(text[index + 8]) &&
      isDigit(text[index + 9]) && isDigit(text[index + 10]) &&
      isDigit(text[index + 11]);
}

bool emailBounds(
    const std::string& text,
    std::size_t at,
    std::size_t leftBound,
    std::size_t& start,
    std::size_t& end) {
  if (at == 0 || at >= text.size() || text[at] != '@') {
    return false;
  }
  start = at;
  while (start > leftBound) {
    const unsigned char ch = static_cast<unsigned char>(text[start - 1]);
    if (!isEmailLocalChar(ch)) {
      break;
    }
    --start;
  }
  if (start >= at) {
    return false;
  }
  end = consumeToken(text, at + 1);
  return end > at + 1 && text.find('.', at) < end;
}

std::vector<TextRun> splitRunForDetectors(
    const TextRun& run,
    TextDataDetector detector) {
  if (run.attachment || run.text.empty() ||
      detector == TextDataDetector::None) {
    return {run};
  }
  const bool wantPhone = detector == TextDataDetector::Phone ||
      detector == TextDataDetector::All;
  const bool wantLink = detector == TextDataDetector::Link ||
      detector == TextDataDetector::All;
  const bool wantEmail = detector == TextDataDetector::Email ||
      detector == TextDataDetector::All;
  std::vector<TextRun> parts;
  std::size_t cursor = 0;
  const auto flush = [&](std::size_t from, std::size_t to, bool link) {
    if (from >= to) {
      return;
    }
    TextRun part = run;
    part.text = run.text.substr(from, to - from);
    if (link) {
      applyLinkStyle(part);
    }
    parts.push_back(std::move(part));
  };
  while (cursor < run.text.size()) {
    std::size_t matchStart = run.text.size();
    std::size_t matchEnd = matchStart;
    bool found = false;
    if (wantLink) {
      for (std::size_t look = cursor; look < run.text.size(); ++look) {
        if (startsUrl(run.text, look)) {
          matchStart = look;
          matchEnd = consumeToken(run.text, look);
          if (matchEnd <= matchStart) {
            matchEnd = matchStart + 1;
          }
          found = true;
          break;
        }
      }
    }
    if (wantEmail) {
      std::size_t search = cursor;
      while (search < run.text.size()) {
        const auto at = run.text.find('@', search);
        if (at == std::string::npos) {
          break;
        }
        std::size_t start = 0;
        std::size_t end = 0;
        if (emailBounds(run.text, at, cursor, start, end) &&
            start < matchStart) {
          matchStart = start;
          matchEnd = end;
          found = true;
          break;
        }
        search = at + 1;
      }
    }
    if (wantPhone) {
      for (std::size_t look = cursor; look + 12 <= run.text.size(); ++look) {
        if (isPhoneDigitRun(run.text, look) && look < matchStart) {
          matchStart = look;
          matchEnd = look + 12;
          found = true;
          break;
        }
      }
    }
    if (!found) {
      flush(cursor, run.text.size(), false);
      break;
    }
    flush(cursor, matchStart, false);
    flush(matchStart, matchEnd, true);
    cursor = std::max(matchEnd, matchStart + 1);
  }
  return parts.empty() ? std::vector<TextRun>{run} : parts;
}

void dropTrailingLetterSpacing(TextParagraph& paragraph) {
  // SkParagraph addLetterSpacesEvenly adds tracking after every glyph,
  // including the last (N * spacing). Android Minikin / CSS apply
  // letter-spacing between clusters and trim the visually last edge of
  // the line, so extra advance is (N-1) * spacing. A one-glyph-too-wide
  // measure wraps the last word at parent max width and Yoga then
  // stretch-fills alignSelf:flex-start.
  for (int index = static_cast<int>(paragraph.runs.size()) - 1; index >= 0;
       --index) {
    auto& run = paragraph.runs[static_cast<size_t>(index)];
    if (run.attachment || run.text.empty()) {
      continue;
    }
    if (!run.letterSpacing || *run.letterSpacing == 0) {
      return;
    }
    int32_t offset = 0;
    const auto size = static_cast<int32_t>(run.text.size());
    int32_t lastStart = 0;
    while (offset < size) {
      lastStart = offset;
      UChar32 codepoint = 0;
      U8_NEXT(run.text.data(), offset, size, codepoint);
    }
    if (lastStart <= 0) {
      run.letterSpacing.reset();
      return;
    }
    TextRun tail = run;
    tail.text = run.text.substr(static_cast<size_t>(lastStart));
    tail.letterSpacing.reset();
    run.text.resize(static_cast<size_t>(lastStart));
    paragraph.runs.insert(
        paragraph.runs.begin() + index + 1, std::move(tail));
    return;
  }
}

TextParagraph rewriteStyledParagraph(const TextParagraph& paragraph) {
  TextParagraph out = paragraph;
  out.runs.clear();
  out.runs.reserve(paragraph.runs.size());
  for (const auto& source : paragraph.runs) {
    TextRun run = source;
    if (!run.attachment) {
      run.text = applyTextTransformUtf8(run.text, run.textTransform);
    }
    // Detect URLs/phones/emails on the original token stream. Hyphenation
    // None inserts ZWSP into long tokens (including 24-char https URLs),
    // which would otherwise hide "https://" from the matcher.
    auto pieces = splitRunForDetectors(run, paragraph.dataDetector);
    for (auto& piece : pieces) {
      if (!piece.attachment) {
        piece.text = applyHyphenation(piece.text, paragraph.hyphenation);
        piece.text = insertCjkLineBreaks(piece.text);
      }
      out.runs.push_back(std::move(piece));
    }
  }
  dropTrailingLetterSpacing(out);
  return out;
}

namespace {

bool isWrapSpace(char16_t unit) {
  return unit == u' ' || unit == u'\t';
}

TextParagraph insertBreaksAtUtf16(
    const TextParagraph& source,
    const std::vector<std::size_t>& breakAt) {
  if (breakAt.empty()) {
    return source;
  }
  std::vector<std::size_t> ordered = breakAt;
  std::sort(ordered.begin(), ordered.end());
  ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
  const auto flat = flattenParagraph(source);
  TextParagraph out = source;
  out.runs.clear();
  std::size_t cursor = 0;
  const auto emitBreak = [&]() {
    TextRun newline;
    if (!out.runs.empty()) {
      newline = out.runs.back();
      newline.attachment = false;
      newline.backgroundColor.reset();
    }
    newline.text = "\n";
    out.runs.push_back(std::move(newline));
  };
  for (const auto index : ordered) {
    if (index == 0 || index >= flat.utf16.size() || index < cursor) {
      continue;
    }
    appendUtf16Range(out, source, flat, cursor, index);
    emitBreak();
    cursor = index;
  }
  appendUtf16Range(out, source, flat, cursor, flat.utf16.size());
  return out.runs.empty() ? source : out;
}

TextParagraph applyBalancedLineBreaks(
    const TextParagraph& source,
    const SkiaPreparedParagraph& wrapped,
    float width,
    TextWritingDirection /*resolvedDirection*/) {
  if (width <= 0 || !std::isfinite(width) || wrapped.lineCount() < 2) {
    return source;
  }
  const auto& lines = wrapped.lines();
  std::vector<std::size_t> breaks;
  for (std::size_t i = 0; i + 1 < lines.size(); ++i) {
    const auto& line = lines[i];
    const auto& next = lines[i + 1];
    if (line.text.empty() || line.utf16End <= line.utf16Start) {
      continue;
    }
    const auto lineUtf16 = line.text.empty()
        ? std::u16string{}
        : SkUnicode::convertUtf8ToUtf16(
              line.text.data(), static_cast<int>(line.text.size()));
    if (lineUtf16.empty()) {
      continue;
    }
    std::size_t end = lineUtf16.size();
    while (end > 0 && isWrapSpace(lineUtf16[end - 1])) {
      --end;
    }
    if (end == 0) {
      continue;
    }
    std::size_t space = end;
    while (space > 0) {
      --space;
      if (isWrapSpace(lineUtf16[space])) {
        break;
      }
    }
    if (space == 0 && !isWrapSpace(lineUtf16[0])) {
      continue;
    }
    auto wordStart = line.utf16Start + space + 1;
    const auto wordEnd = line.utf16Start + end;
    while (wordStart < wordEnd &&
           (wordStart - line.utf16Start < lineUtf16.size()) &&
           isWrapSpace(lineUtf16[wordStart - line.utf16Start])) {
      ++wordStart;
    }
    if (wordStart >= wordEnd) {
      continue;
    }
    float wordWidth =
        wrapped.widthForUtf16Range(wordStart, wordEnd);
    const float spaceWidth = std::max(
        0.0f,
        wrapped.widthForUtf16Range(line.utf16Start + space, wordStart));
    if (wordWidth < 1.0f) {
      const float units = std::max(
          1.0f, static_cast<float>(wordEnd - line.utf16Start));
      wordWidth = line.width *
          static_cast<float>(wordEnd - wordStart) / units;
    }
    if (wordWidth < 1.0f || wordWidth > line.width * 0.4f) {
      continue;
    }
    if (next.width + spaceWidth + wordWidth > width + 0.5f) {
      continue;
    }
    const float beforeImbalance = std::fabs(line.width - next.width);
    const float afterCurrent =
        std::max(0.0f, line.width - wordWidth - spaceWidth);
    const float afterNext = next.width + spaceWidth + wordWidth;
    const float afterImbalance = std::fabs(afterCurrent - afterNext);
    // Android BREAK_STRATEGY_BALANCED evens a greedy-full first line against
    // a short last line. HIGH_QUALITY is Knuth-Plass: the last line may stay
    // short. Only Balanced uses this evenness pass.
    if (beforeImbalance - afterImbalance <= std::max(wordWidth * 0.5f, 24.0f)) {
      continue;
    }
    breaks.push_back(wordStart);
    break;
  }
  return insertBreaksAtUtf16(source, breaks);
}

TextParagraph applyHighQualityLineBreaks(
    const TextParagraph& source,
    const SkiaPreparedParagraph& unwrapped,
    float width) {
  if (width <= 0 || !std::isfinite(width)) {
    return source;
  }
  const auto flat = flattenParagraph(source);
  if (flat.utf16.empty() ||
      std::find(flat.utf16.begin(), flat.utf16.end(), u'\n') !=
          flat.utf16.end()) {
    return source;
  }

  struct WordRange {
    std::size_t start;
    std::size_t end;
  };
  std::vector<WordRange> words;
  for (std::size_t cursor = 0; cursor < flat.utf16.size();) {
    while (cursor < flat.utf16.size() && isWrapSpace(flat.utf16[cursor])) {
      ++cursor;
    }
    const auto start = cursor;
    while (cursor < flat.utf16.size() &&
           !isWrapSpace(flat.utf16[cursor])) {
      ++cursor;
    }
    if (cursor > start) {
      words.push_back({start, cursor});
    }
  }
  if (words.size() < 3) {
    return source;
  }

  // Android HIGH_QUALITY uses a paragraph-wide line breaker. Minimize the
  // accumulated ragged edge for every line except the last, whose shortness
  // is intentionally unpenalized. This permits both directions of movement
  // across greedy boundaries (for example "sit" forward near the top and
  // "magna" backward near the bottom of RN Tester's baseline sample).
  const auto count = words.size();
  const auto infinity = std::numeric_limits<double>::infinity();
  std::vector<double> cost(count + 1, infinity);
  std::vector<std::size_t> next(count + 1, count);
  cost[count] = 0;
  for (std::size_t reverse = count; reverse-- > 0;) {
    for (std::size_t end = reverse; end < count; ++end) {
      const auto lineWidth = unwrapped.widthForUtf16Range(
          words[reverse].start, words[end].end);
      if (lineWidth > width + 0.5f) {
        break;
      }
      const auto lastLine = end + 1 == count;
      const double slack = std::max(0.0, static_cast<double>(width - lineWidth));
      const double candidate =
          (lastLine ? 0.0 : slack * slack) + cost[end + 1];
      if (candidate < cost[reverse]) {
        cost[reverse] = candidate;
        next[reverse] = end + 1;
      }
    }
  }
  if (!std::isfinite(cost[0])) {
    return source;
  }
  std::vector<std::size_t> breaks;
  for (std::size_t cursor = 0; next[cursor] < count; cursor = next[cursor]) {
    if (next[cursor] <= cursor) {
      return source;
    }
    breaks.push_back(words[next[cursor]].start);
  }
  return insertBreaksAtUtf16(source, breaks);
}

} // namespace

std::shared_ptr<const SkiaPreparedParagraph> SkiaTextLayoutEngine::prepare(
    const TextParagraph& paragraph,
    float width,
    float pointScaleFactor) const {
  const auto prepareStarted = std::chrono::steady_clock::now();
  if (!std::isfinite(width) || width <= 0) {
    width = std::numeric_limits<float>::max() / 16;
  }
  const float scale = std::isfinite(pointScaleFactor) && pointScaleFactor > 0
      ? pointScaleFactor
      : 1.0f;

  auto rewritten = rewriteStyledParagraph(paragraph);
  if (impl_->platform == TextFontPlatform::Android) {
    snapAndroidParagraph(rewritten, scale);
  }
  ScriptFallbacks scriptNeeds;
  for (const auto& run : rewritten.runs) {
    const auto detected = detectScriptFallbacks(run.text);
    scriptNeeds.han = scriptNeeds.han || detected.han;
    scriptNeeds.arabic = scriptNeeds.arabic || detected.arabic;
    scriptNeeds.hebrew = scriptNeeds.hebrew || detected.hebrew;
    scriptNeeds.devanagari = scriptNeeds.devanagari || detected.devanagari;
    scriptNeeds.thai = scriptNeeds.thai || detected.thai;
    scriptNeeds.emoji = scriptNeeds.emoji || detected.emoji;
  }
  // File I/O before skiaLayoutMutex: Yoga measure (JS) and Skia paint (UI)
  // share that lock. Loading Noto CJK/emoji/VF while holding it freezes
  // scrolling. The catalog is shared so the subsequent measure and paint
  // see the same typefaces.
  impl_->ensureScriptFonts(scriptNeeds);
  std::lock_guard engineLock(skiaLayoutMutex());
  const auto resolvedDirection = resolveWritingDirection(rewritten);
  const auto resolvedAlign =
      skAlignment(rewritten.alignment, rewritten.paragraphRtl);
  const bool centeredOrEnd = resolvedAlign == TextAlign::kCenter ||
      resolvedAlign == TextAlign::kRight ||
      resolvedAlign == TextAlign::kEnd;
  const auto widthIndependentOrigin =
      resolvedAlign == TextAlign::kLeft || centeredOrEnd;

  const auto build = [&](const TextParagraph& source,
                         bool enforceMaxLines,
                         float layoutWidth,
                         TextAlign align,
                         float wrapWidth = -1.0f) {
    if (!(layoutWidth > 0)) {
      layoutWidth = width;
    }
    const float paragraphWrap = wrapWidth > 0 ? wrapWidth : layoutWidth;
    ParagraphStyle paragraphStyle;
    paragraphStyle.setTextAlign(align);
    paragraphStyle.setTextDirection(skDirection(resolvedDirection));
    if (source.hyphenation != TextHyphenation::None) {
      paragraphStyle.setRenderSoftHyphens(true);
    }
    if (enforceMaxLines && source.maximumNumberOfLines > 0) {
      paragraphStyle.setMaxLines(source.maximumNumberOfLines);
      if (source.ellipsizeMode == TextEllipsizeMode::Tail) {
        paragraphStyle.setEllipsis(SkString("…"));
      }
    }
    // Android TextView Paint is the outer Text style (first fragment), not
    // SkParagraph's 14pt default. Nested AbsoluteSizeSpan keeps that
    // alphabetic baseline.
    TextRun defaultRun;
    for (const auto& run : source.runs) {
      if (!run.attachment) {
        defaultRun = run;
        defaultRun.text.clear();
        break;
      }
    }
    paragraphStyle.setTextStyle(skTextStyle(defaultRun, impl_->platform));
    auto builder = ParagraphBuilder::make(
        paragraphStyle, impl_->fonts, impl_->unicode);
    const bool customAndroidBackgrounds =
        impl_->platform == TextFontPlatform::Android &&
        source.includeFontPadding;
    for (const auto& run : source.runs) {
      builder->pushStyle(skTextStyle(
          run,
          impl_->platform,
          !(customAndroidBackgrounds && run.backgroundColor)));
      if (run.attachment) {
        builder->addPlaceholder(PlaceholderStyle(
            std::max(run.attachmentWidth, 0.0f),
            std::max(run.attachmentHeight, 0.0f),
            PlaceholderAlignment::kBaseline,
            TextBaseline::kAlphabetic,
            std::max(run.attachmentHeight, 0.0f)));
      } else if (!run.text.empty()) {
        builder->addText(run.text.data(), run.text.size());
      }
      builder->pop();
    }
    const bool paintTailEllipsis =
        enforceMaxLines && source.maximumNumberOfLines > 0 &&
        source.ellipsizeMode == TextEllipsizeMode::Tail;
    const auto fontPadding = impl_->fontMgr
        ? androidFontPadding(
              *impl_->fontMgr, source, impl_->platform, scale)
        : AndroidFontPadding{};
    std::string sourceUtf8;
    for (const auto& run : source.runs) {
      if (!run.attachment) {
        sourceUtf8.append(run.text);
      } else {
        sourceUtf8.append("\xEF\xBF\xBC");
      }
    }
    const auto sourceUtf16 = sourceUtf8.empty()
        ? std::u16string{}
        : SkUnicode::convertUtf8ToUtf16(
              sourceUtf8.data(), static_cast<int>(sourceUtf8.size()));
    std::vector<PreparedWavySpan> wavy;
    std::vector<PreparedBackgroundSpan> backgrounds;
    std::size_t utf16 = 0;
    for (const auto& run : source.runs) {
      const std::size_t runUnits = run.attachment
          ? 1
          : (run.text.empty()
                 ? 0
                 : SkUnicode::convertUtf8ToUtf16(
                       run.text.data(), static_cast<int>(run.text.size()))
                       .size());
      if (!run.attachment &&
          run.decorationStyle == TextDecorationStyle::Wavy &&
          run.decorationLine != TextDecorationLine::None) {
        const auto color = run.decorationColor
            ? skColor(*run.decorationColor, run.opacity)
            : run.foregroundColor
            ? skColor(*run.foregroundColor, run.opacity)
            : SK_ColorBLACK;
        wavy.push_back({
            .utf16Start = utf16,
            .utf16End = utf16 + runUnits,
            .underline = run.decorationLine == TextDecorationLine::Underline ||
                run.decorationLine ==
                    TextDecorationLine::UnderlineStrikethrough,
            .strikethrough =
                run.decorationLine == TextDecorationLine::Strikethrough ||
                run.decorationLine ==
                    TextDecorationLine::UnderlineStrikethrough,
            .color = color,
            .thickness = std::max(run.fontSize / 14.0f, 1.0f),
        });
      }
      if (customAndroidBackgrounds && !run.attachment &&
          run.backgroundColor && runUnits > 0 && impl_->fontMgr) {
        const auto fontSizeDp = std::max(
            run.fontSize * std::max(run.fontSizeMultiplier, 0.01f), 1.0f);
        const auto fontSizePx = std::max(
            1.0f,
            static_cast<float>(std::ceil(
                static_cast<double>(fontSizeDp) *
                static_cast<double>(scale))));
        const auto metrics = androidFontMetricsPx(
            typefaceForRun(*impl_->fontMgr, run, impl_->platform), fontSizePx);
        backgrounds.push_back({
            .utf16Start = utf16,
            .utf16End = utf16 + runUnits,
            .color = skColor(*run.backgroundColor, run.opacity),
            .extraTop = static_cast<float>(
                std::max(0, metrics.top - metrics.ascent)) / scale,
            .extraBottom = static_cast<float>(
                std::max(0, metrics.bottom - metrics.descent)) / scale,
        });
      }
      utf16 += runUnits;
    }
    auto prepared = std::shared_ptr<SkiaPreparedParagraph>(
        new SkiaPreparedParagraph(std::make_unique<SkiaPreparedParagraph::Impl>(
            builder->Build(),
            layoutWidth,
            paragraphWrap,
            resolvedDirection,
            fontPadding,
            std::any_of(
                source.runs.begin(),
                source.runs.end(),
                [](const TextRun& run) { return run.lineHeight.has_value(); }),
            sourceUtf16,
            std::move(wavy),
            paintTailEllipsis)));
    prepared->impl_->widthIndependentOrigin = widthIndependentOrigin;
    prepared->impl_->alignmentDependsOnWidth = centeredOrEnd;
    for (auto& span : backgrounds) {
      span.boxes = prepared->impl_->paragraph->getRectsForRange(
          span.utf16Start,
          span.utf16End,
          RectHeightStyle::kTight,
          RectWidthStyle::kTight);
    }
    prepared->impl_->backgroundSpans = std::move(backgrounds);
    if (impl_->platform == TextFontPlatform::Android && impl_->fontMgr &&
        scale > 0) {
      const auto layoutPx = androidLayoutPx(
          *impl_->fontMgr, source, impl_->platform, scale);
      if (layoutPx.valid) {
        int heightPx = 0;
        if (prepared->impl_->lines.empty()) {
          heightPx = layoutPx.line;
        } else {
          for (const auto& line : prepared->impl_->lines) {
            const bool hasHan = detectScriptFallbacks(line.text).han;
            heightPx += hasHan && layoutPx.fallbackLine > 0
                ? std::max(layoutPx.primaryLine, layoutPx.fallbackLine)
                : layoutPx.primaryLine;
          }
        }
        if (source.includeFontPadding) {
          const bool fallbackCoversPrimary =
              layoutPx.fallbackLine >=
              layoutPx.primaryLine + layoutPx.extraTop + layoutPx.extraBottom;
          const bool firstHasHan = !prepared->impl_->lines.empty() &&
              detectScriptFallbacks(prepared->impl_->lines.front().text).han;
          const bool lastHasHan = !prepared->impl_->lines.empty() &&
              detectScriptFallbacks(prepared->impl_->lines.back().text).han;
          if (!(fallbackCoversPrimary && firstHasHan)) {
            heightPx += layoutPx.extraTop;
          }
          if (!(fallbackCoversPrimary && lastHasHan)) {
            heightPx += layoutPx.extraBottom;
          }
        }
        const auto androidTextHeight = static_cast<float>(heightPx) / scale;
        // StaticLayout's normal-font line grid is authoritative for pure
        // text, but an inline placeholder can expand an earlier line. Keep
        // SkParagraph's attachment-aware height in that case; replacing it
        // with `line * count` clips every line after a tall inline View.
        prepared->impl_->measuredHeight = prepared->impl_->attachments.empty()
            ? androidTextHeight
            : std::max(prepared->impl_->measuredHeight, androidTextHeight);
        // FontMetricsUtil reports -StaticLayout.getLineAscent(0) to Fabric.
        // That value is Paint.FontMetricsInt.top on a padded first line, or
        // the inline ReplacementSpan height when the attachment is taller.
        // SkParagraph's baseline is close but not the same integer-pixel
        // contract, which leaves Yoga baseline markers visibly displaced.
        int firstAscentPx = layoutPx.ascent;
        if (source.includeFontPadding) {
          firstAscentPx += layoutPx.extraTop;
        }
        const auto firstLineHeight = prepared->impl_->lines.empty()
            ? 0.0f
            : prepared->impl_->lines.front().height;
        for (const auto& attachment : prepared->impl_->attachments) {
          if (attachment.y <= firstLineHeight + 0.5f) {
            firstAscentPx = std::max(
                firstAscentPx,
                static_cast<int>(std::lround(attachment.height * scale)));
          }
        }
        prepared->impl_->firstLineAscent =
            static_cast<float>(firstAscentPx) / scale;
      }
    }
    return prepared;
  };

  TextParagraph content = rewritten;
  if (rewritten.adjustsFontSizeToFit) {
    // Match RN Android TextLayoutManager.adjustSpannableFontToFit: binary-search
    // the largest size that still satisfies height, maxLines, and the
    // single-character width check. Multi-character width overflow wraps.
    const auto largest = largestRunFontSize(rewritten);
    if (largest > 0) {
      float minimumFontSize = 4.0f;
      if (std::isfinite(rewritten.minimumFontSize) &&
          rewritten.minimumFontSize > 0) {
        minimumFontSize = rewritten.minimumFontSize;
      }
      if (std::isfinite(rewritten.minimumFontScale)) {
        minimumFontSize = std::max(
            minimumFontSize,
            largest * std::clamp(rewritten.minimumFontScale, 0.01f, 1.0f));
      }
      minimumFontSize = std::min(minimumFontSize, largest);
      const auto utf16Length = paragraphUtf16Length(rewritten);
      const auto exceeds = [&](const SkiaPreparedParagraph& laid) {
        if (rewritten.maximumNumberOfLines > 0 &&
            laid.lineCount() > rewritten.maximumNumberOfLines) {
          return true;
        }
        if (std::isfinite(rewritten.maxHeight) &&
            laid.height() > rewritten.maxHeight) {
          return true;
        }
        return utf16Length == 1 && laid.longestLine() > width;
      };

      int intervalStart =
          std::max(1, static_cast<int>(std::lround(minimumFontSize * 100.0f)));
      int intervalEnd = std::max(
          intervalStart, static_cast<int>(std::lround(largest * 100.0f)));
      while (true) {
        const int current = (intervalStart + intervalEnd + 1) / 2;
        const float factor =
            (static_cast<float>(current) / 100.0f) / largest;
        const auto candidate = scaleParagraphFontSizes(
            rewritten, factor, minimumFontSize);
        // Probe without maxLines/ellipsis so lineCount is the true wrapped
        // count, matching Android's UNSET max-lines search layout.
        const auto fitted = build(candidate, false, -1.0f, resolvedAlign);
        if (intervalStart == intervalEnd) {
          break;
        }
        if (current > intervalStart && exceeds(*fitted)) {
          intervalEnd =
              (intervalEnd - intervalStart == 1) ? intervalStart : current;
        } else {
          intervalStart = current;
        }
      }
      content = scaleParagraphFontSizes(
          rewritten,
          (static_cast<float>(intervalStart) / 100.0f) / largest,
          minimumFontSize);
    }
  }

  float qualityIntrinsicWidth = 0;
  if (content.breakStrategy != TextBreakStrategy::Simple) {
    const bool hasAttachment = std::any_of(
        content.runs.begin(),
        content.runs.end(),
        [](const TextRun& run) { return run.attachment; });
    if (!hasAttachment) {
      const auto wrapped = build(content, false, -1.0f, resolvedAlign);
      if (wrapped->lineCount() >= 2) {
        const auto infinite = build(
            content,
            false,
            std::numeric_limits<float>::max() / 16,
            resolvedAlign);
        qualityIntrinsicWidth = infinite->width();
        // Android HIGH_QUALITY applies paragraph-wide Knuth-Plass decisions
        // to longer paragraphs, but two-line boxes remain greedy (RN Tester
        // magenta keeps "uniform" / "borderRadii"). BALANCED also evens
        // short paragraphs.
        const bool qualityParagraph =
            content.breakStrategy == TextBreakStrategy::Balanced ||
            (content.breakStrategy == TextBreakStrategy::HighQuality &&
             wrapped->lineCount() >= 3);
        if (qualityParagraph && content.maximumNumberOfLines == 0) {
          content = content.breakStrategy == TextBreakStrategy::Balanced
              ? applyBalancedLineBreaks(
                    content, *wrapped, width, resolvedDirection)
              : applyHighQualityLineBreaks(content, *infinite, width);
        }
      }
    }
  }

  const bool nativeTightEllipsis =
      content.maximumNumberOfLines > 0 &&
      !content.adjustsFontSizeToFit &&
      (content.ellipsizeMode == TextEllipsizeMode::Tail ||
       content.ellipsizeMode == TextEllipsizeMode::Clip);
  const bool searchedEllipsis =
      content.maximumNumberOfLines > 0 &&
      !content.adjustsFontSizeToFit &&
      (content.ellipsizeMode == TextEllipsizeMode::Head ||
       content.ellipsizeMode == TextEllipsizeMode::Middle);
  bool ellipsisOverflows = false;
  std::shared_ptr<const SkiaPreparedParagraph> ellipsisProbe;
  if (nativeTightEllipsis || searchedEllipsis) {
    ellipsisProbe = build(content, false, -1.0f, resolvedAlign);
    ellipsisOverflows =
        ellipsisProbe->lineCount() > content.maximumNumberOfLines ||
        ellipsisProbe->longestLine() > width + 0.01f;
  }

  // SkParagraph only natively tail-ellipsizes. Head/middle match Android
  // TruncateAt.START/MIDDLE by binary-searching a truncated UTF-16 slice.
  if (searchedEllipsis) {
    const auto flat = flattenParagraph(content);
    const auto length = static_cast<int>(flat.utf16.size());
    if (length > 0 &&
        !ellipsisLayoutFits(
            *ellipsisProbe, content.maximumNumberOfLines, width)) {
      int low = 0;
      int high = length;
      int best = 0;
      while (low <= high) {
        const int mid = low + (high - low) / 2;
        const auto candidate =
            content.ellipsizeMode == TextEllipsizeMode::Head
            ? withHeadEllipsis(content, static_cast<size_t>(mid))
            : withMiddleEllipsis(content, static_cast<size_t>(mid));
        const auto laid = build(candidate, false, -1.0f, resolvedAlign);
        if (ellipsisLayoutFits(
                *laid, content.maximumNumberOfLines, width)) {
          best = mid;
          low = mid + 1;
        } else {
          high = mid - 1;
        }
      }
      content = content.ellipsizeMode == TextEllipsizeMode::Head
          ? withHeadEllipsis(content, static_cast<size_t>(best))
          : withMiddleEllipsis(content, static_cast<size_t>(best));
    }
  }

  // wrap_content + textAlign center/right: Android centers the view, not
  // the glyphs inside a box the same width as the ink. Layout as start
  // when the constraint is no wider than the text, otherwise "Text" is
  // clipped to "Tex" by default overflow:hidden.
  auto prepared = centeredOrEnd
      ? build(content, true, -1.0f, TextAlign::kLeft)
      : build(content, true, -1.0f, resolvedAlign);
  if (centeredOrEnd) {
    const float intrinsic = prepared->longestLine();
    // Shrink-wrap single-line titles stay start so overflow:hidden does
    // not clip "Text" to "Tex". Use longestLine, not measuredWidth:
    // maxIntrinsic can equal the constraint and skip this pass, leaving
    // wrapping textAlign center/right stuck on the left.
    if (prepared->lineCount() >= 2 || width > intrinsic + 1.0f) {
      prepared = build(content, true, -1.0f, resolvedAlign);
    }
  }
  // SkParagraph.layout(maxIntrinsicWidth) still wraps: cluster widths
  // exceed the constraint by a fraction of a dp. Yoga wrap_content then
  // clips the last glyph (RN Tester title "Text" -> "Tex").
  if (prepared->lineCount() >= 2 &&
      prepared->impl_->maximumIntrinsicWidth <= width + 1.0f) {
    const float wrapWidth =
        std::max(width, prepared->impl_->maximumIntrinsicWidth) + 1.0f;
    const bool shrinkWrapCenter =
        centeredOrEnd && !(width > prepared->width() + 1.0f);
    prepared = build(
        content,
        true,
        width,
        shrinkWrapCenter ? TextAlign::kLeft : resolvedAlign,
        wrapWidth);
  }
  // Android END ellipsis consumes as much of the following word as fits. Using
  // the wrapped line end here stopped at the preceding word boundary (for
  // example, "Truncated text is …" instead of "... is baaa…"). Search the
  // UTF-16 prefix against an unlimited-line probe, then rebuild with maxLines.
  if (content.ellipsizeMode == TextEllipsizeMode::Tail &&
      ellipsisOverflows && ellipsisProbe &&
      content.maximumNumberOfLines > 0 &&
      ellipsisProbe->lineCount() > content.maximumNumberOfLines &&
      !ellipsisProbe->lines().empty()) {
    const auto flat = flattenParagraph(content);
    int low = 0;
    int high = std::max(0, static_cast<int>(flat.utf16.size()) - 1);
    int best = 0;
    while (low <= high) {
      const int pivot = low + (high - low) / 2;
      int mid = pivot;
      if (mid > 0 && mid < static_cast<int>(flat.utf16.size()) &&
          flat.utf16[static_cast<size_t>(mid)] >= 0xDC00 &&
          flat.utf16[static_cast<size_t>(mid)] <= 0xDFFF &&
          flat.utf16[static_cast<size_t>(mid - 1)] >= 0xD800 &&
          flat.utf16[static_cast<size_t>(mid - 1)] <= 0xDBFF) {
        --mid;
      }
      auto candidate = withTailEllipsis(content, static_cast<size_t>(mid));
      candidate.maximumNumberOfLines = 0;
      const auto laid = build(candidate, false, -1.0f, resolvedAlign);
      if (ellipsisLayoutFits(
              *laid, content.maximumNumberOfLines, width)) {
        best = mid;
        low = pivot + 1;
      } else {
        high = pivot - 1;
      }
    }
    const auto truncated =
        withTailEllipsis(content, static_cast<size_t>(best));
    if (truncated.ellipsizeMode == TextEllipsizeMode::Clip) {
      auto relaid = build(truncated, false, -1.0f, resolvedAlign);
      if (ellipsisLayoutFits(
              *relaid, content.maximumNumberOfLines, width) &&
          !relaid->lines().empty() &&
          relaid->lines().back().text.find("…") != std::string::npos) {
        relaid->impl_->exceededMaximumLines = true;
        prepared = std::move(relaid);
      }
    }
  }
  if (qualityIntrinsicWidth > 0) {
    prepared->retainUnconstrainedIntrinsicWidth(qualityIntrinsicWidth);
  }
  if (std::getenv("RNSIM_RASTER_STATS") != nullptr &&
      paragraph.runs.size() == 1 && paragraph.runs[0].text == "Text") {
    std::fprintf(
        stderr,
        "title Text align=%d w=%.2f longest=%.2f lines=%zu left=%.2f "
        "layoutW=%.2f center=%d\n",
        static_cast<int>(paragraph.alignment),
        prepared->width(),
        prepared->longestLine(),
        prepared->lineCount(),
        prepared->lines().empty() ? 0.0f : prepared->lines().front().left,
        prepared->layoutWidth(),
        centeredOrEnd ? 1 : 0);
  }
  if (std::getenv("RNSIM_RASTER_STATS") != nullptr &&
      !prepared->attachments().empty()) {
    std::fprintf(
        stderr,
        "attachment paragraph h=%.2f lines=%zu paddingTop=%.2f",
        prepared->height(),
        prepared->lineCount(),
        prepared->impl_->fontPaddingTop);
    for (const auto& line : prepared->lines()) {
      std::fprintf(
          stderr,
          " [h=%.2f base=%.2f asc=%.2f desc=%.2f]",
          line.height,
          line.baseline,
          line.ascent,
          line.descent);
    }
    for (const auto& attachment : prepared->attachments()) {
      std::fprintf(
          stderr,
          " {y=%.2f h=%.2f}",
          attachment.y,
          attachment.height);
    }
    std::fputc('\n', stderr);
  }
  if (std::getenv("RNSIM_RASTER_STATS") != nullptr) {
    static std::atomic<int> count{0};
    static std::atomic<long long> totalUs{0};
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - prepareStarted)
                        .count();
    const auto n = ++count;
    const auto total = totalUs += us;
    if (n <= 8 || n % 50 == 0) {
      std::size_t chars = 0;
      for (const auto& run : paragraph.runs) {
        chars += run.text.size();
      }
      std::fprintf(
          stderr,
          "prepare #%d %.2fms avg=%.2fms runs=%zu chars=%zu width=%.1f "
          "scale=%.2f\n",
          n,
          static_cast<double>(us) / 1000.0,
          static_cast<double>(total) / 1000.0 / static_cast<double>(n),
          paragraph.runs.size(),
          chars,
          width,
          pointScaleFactor);
    }
  }
  return prepared;
}

} // namespace ReactNativeSimulator
