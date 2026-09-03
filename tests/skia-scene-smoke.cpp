#include "SkiaMountedTreeRenderer.h"
#include "SkiaTextLayoutEngine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include <folly/dynamic.h>

namespace {

std::size_t countGreenSpinnerPixels(
    const ReactNativeSimulator::SkiaRenderedFrame& frame) {
  std::size_t count = 0;
  for (std::size_t offset = 0; offset + 3 < frame.rgba.size(); offset += 4) {
    if (frame.rgba[offset] < 40 && frame.rgba[offset + 1] > 180 &&
        frame.rgba[offset + 2] < 40 && frame.rgba[offset + 3] > 80) {
      ++count;
    }
  }
  return count;
}

bool robotoSmallCapsMatchesPlain(
    ReactNativeSimulator::SkiaTextLayoutEngine& engine,
    ReactNativeSimulator::SkiaMountedTreeRenderer& renderer) {
  auto make = [&](std::uint32_t fontVariant) {
    ReactNativeSimulator::TextParagraph paragraph;
    ReactNativeSimulator::TextRun run;
    run.text = "Small Caps";
    run.fontSize = 16;
    run.fontFamily = "sans-serif";
    run.fontVariant = fontVariant;
    run.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
    paragraph.runs.push_back(std::move(run));
    return engine.prepare(paragraph, 200);
  };
  const auto plain = make(0);
  const auto smallCaps = make(1u << 1);
  auto render = [&](const std::shared_ptr<
                    const ReactNativeSimulator::SkiaPreparedParagraph>&
                        prepared) {
    ReactNativeSimulator::SceneSnapshot scene;
    scene.surfaceId = 1;
    scene.revision = 1;
    scene.rootTag = 1;
    scene.viewportWidth = 160;
    scene.viewportHeight = 32;
    scene.pointScaleFactor = 1;
    ReactNativeSimulator::SceneNode root;
    root.tag = 1;
    root.layoutable = true;
    root.width = 160;
    root.height = 32;
    root.hasBackgroundColor = true;
    root.backgroundRed = 1;
    root.backgroundGreen = 1;
    root.backgroundBlue = 1;
    root.backgroundAlpha = 1;
    scene.nodes.push_back(root);
    ReactNativeSimulator::SceneNode text;
    text.tag = 2;
    text.parentTag = 1;
    text.layoutable = true;
    text.width = 160;
    text.height = 32;
    text.preparedText = prepared;
    scene.nodes.push_back(text);
    return renderer.render(scene);
  };
  const auto plainFrame = render(plain);
  const auto smallCapsFrame = render(smallCaps);
  if (!plainFrame || !smallCapsFrame) {
    std::cerr << "Roboto small-caps smoke render failed: "
              << (!plainFrame ? plainFrame.error : smallCapsFrame.error)
              << '\n';
    return false;
  }
  if (plainFrame.rgba.size() != smallCapsFrame.rgba.size()) {
    std::cerr << "Roboto small-caps smoke size mismatch\n";
    return false;
  }
  std::size_t differ = 0;
  for (std::size_t i = 0; i < plainFrame.rgba.size(); ++i) {
    const int delta = std::abs(
        static_cast<int>(plainFrame.rgba[i]) -
        static_cast<int>(smallCapsFrame.rgba[i]));
    if (delta > 8) {
      ++differ;
    }
  }
  // Pixel/Roboto ignores fontVariant small-caps. Prepared line.text is the
  // source string, so compare rasters instead of glyphs.
  if (differ > plainFrame.rgba.size() / 50) {
    std::cerr << "Roboto small-caps synthesized, differing bytes=" << differ
              << '\n';
    return false;
  }
  return true;
}

struct InkBox {
  int left{0};
  int top{0};
  int right{0};
  int bottom{0};
  std::size_t pixels{0};
};

InkBox inkBox(
    const ReactNativeSimulator::SkiaRenderedFrame& frame,
    int x0,
    int x1,
    bool (*match)(int, int, int, int)) {
  InkBox box;
  box.left = frame.width;
  box.top = frame.height;
  box.right = -1;
  box.bottom = -1;
  for (int y = 0; y < frame.height; ++y) {
    for (int x = std::max(0, x0); x < std::min(frame.width, x1); ++x) {
      const auto offset =
          (static_cast<std::size_t>(y) *
               static_cast<std::size_t>(frame.width) +
           static_cast<std::size_t>(x)) *
          4;
      const int r = frame.rgba[offset];
      const int g = frame.rgba[offset + 1];
      const int b = frame.rgba[offset + 2];
      const int a = frame.rgba[offset + 3];
      if (!match(r, g, b, a)) {
        continue;
      }
      box.left = std::min(box.left, x);
      box.top = std::min(box.top, y);
      box.right = std::max(box.right, x);
      box.bottom = std::max(box.bottom, y);
      ++box.pixels;
    }
  }
  return box;
}

ReactNativeSimulator::SkiaRenderedFrame renderPrepared(
    ReactNativeSimulator::SkiaMountedTreeRenderer& renderer,
    const std::shared_ptr<const ReactNativeSimulator::SkiaPreparedParagraph>&
        prepared,
    int width,
    int height,
    float pointScaleFactor = 1) {
  ReactNativeSimulator::SceneSnapshot scene;
  scene.surfaceId = 1;
  scene.revision = 1;
  scene.rootTag = 1;
  scene.viewportWidth = static_cast<float>(width);
  scene.viewportHeight = static_cast<float>(height);
  scene.pointScaleFactor = pointScaleFactor;
  ReactNativeSimulator::SceneNode root;
  root.tag = 1;
  root.layoutable = true;
  root.width = static_cast<float>(width);
  root.height = static_cast<float>(height);
  root.hasBackgroundColor = true;
  root.backgroundRed = 1;
  root.backgroundGreen = 1;
  root.backgroundBlue = 1;
  root.backgroundAlpha = 1;
  scene.nodes.push_back(root);
  ReactNativeSimulator::SceneNode text;
  text.tag = 2;
  text.parentTag = 1;
  text.layoutable = true;
  text.width = static_cast<float>(width);
  text.height = static_cast<float>(height);
  text.preparedText = prepared;
  scene.nodes.push_back(text);
  return renderer.render(scene);
}

bool mixedSizeSharesAlphabeticBaseline(
    ReactNativeSimulator::SkiaTextLayoutEngine& engine,
    ReactNativeSimulator::SkiaMountedTreeRenderer& renderer) {
  ReactNativeSimulator::TextParagraph paragraph;
  paragraph.includeFontPadding = true;
  ReactNativeSimulator::TextRun smallLeft;
  smallLeft.text = "ace";
  smallLeft.fontSize = 8;
  smallLeft.fontFamily = "sans-serif";
  smallLeft.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 1, 1};
  paragraph.runs.push_back(smallLeft);
  ReactNativeSimulator::TextRun large;
  large.text = "H";
  large.fontSize = 23;
  large.fontFamily = "sans-serif";
  large.foregroundColor = ReactNativeSimulator::TextColor{1, 0, 0, 1};
  large.backgroundColor = ReactNativeSimulator::TextColor{1, 1, 0, 1};
  paragraph.runs.push_back(large);
  ReactNativeSimulator::TextRun smallRight;
  smallRight.text = "ace";
  smallRight.fontSize = 8;
  smallRight.fontFamily = "sans-serif";
  smallRight.foregroundColor = ReactNativeSimulator::TextColor{0, 0.6f, 0, 1};
  paragraph.runs.push_back(smallRight);
  const auto prepared = engine.prepare(paragraph, 280);
  const float density = 3.0f;
  const auto frame = renderPrepared(renderer, prepared, 280, 48, density);
  if (!frame) {
    std::cerr << "mixed-size baseline render failed: " << frame.error << '\n';
    return false;
  }
  auto isBlue = [](int r, int g, int b, int a) {
    return a > 20 && b > r + 25 && b > g + 25;
  };
  auto isRed = [](int r, int g, int b, int a) {
    return a > 20 && r > g + 25 && r > b + 25;
  };
  auto isGreen = [](int r, int g, int b, int a) {
    return a > 20 && g > r + 25 && g > b + 25;
  };
  const auto blue = inkBox(frame, 0, frame.width, isBlue);
  const auto red = inkBox(frame, 0, frame.width, isRed);
  const auto green = inkBox(frame, 0, frame.width, isGreen);
  const auto yellow = inkBox(
      frame,
      0,
      frame.width,
      [](int r, int g, int b, int a) {
        return a > 20 && r > 220 && g > 220 && b < 40;
      });
  if (blue.pixels < 8 || red.pixels < 8 || green.pixels < 8) {
    std::cerr << "mixed-size baseline missing run ink, blue=" << blue.pixels
              << " red=" << red.pixels << " green=" << green.pixels << '\n';
    return false;
  }
  const auto expectedBackgroundHeight = static_cast<int>(
      std::floor(prepared->height() * density));
  if (yellow.pixels < 8 ||
      yellow.bottom - yellow.top + 1 < expectedBackgroundHeight - 2) {
    std::cerr << "Android nested Text background omitted font padding, box=["
              << yellow.top << ',' << yellow.bottom << "] expectedHeight>="
              << expectedBackgroundHeight - 2 << '\n';
    return false;
  }
  const int leftDelta = blue.bottom - red.bottom;
  const int rightDelta = green.bottom - red.bottom;
  // Android alphabetic baseline: bottoms of size-8 "ace" and size-23 "H"
  // line up. Center/top alignment puts the small run ~6-10dp too high.
  const int baselineSlop = static_cast<int>(std::lround(2.5f * density));
  if (std::abs(leftDelta) > baselineSlop ||
      std::abs(rightDelta) > baselineSlop) {
    std::cerr << "mixed-size nested runs did not share alphabetic baseline,"
              << " blue=[" << blue.top << "," << blue.bottom << "]"
              << " red=[" << red.top << "," << red.bottom << "]"
              << " green=[" << green.top << "," << green.bottom << "]"
              << " leftDelta=" << leftDelta << " rightDelta=" << rightDelta
              << " lineH=" << prepared->height()
              << " baseline=" << prepared->alphabeticBaseline() << '\n';
    return false;
  }
  const int smallMid = (blue.top + blue.bottom) / 2;
  const int largeMid = (red.top + red.bottom) / 2;
  if (std::abs(smallMid - largeMid) < static_cast<int>(3.0f * density)) {
    std::cerr << "mixed-size nested runs look center-aligned in the line box,"
              << " smallMid=" << smallMid << " largeMid=" << largeMid
              << '\n';
    return false;
  }

  ReactNativeSimulator::TextParagraph onlyLarge;
  onlyLarge.includeFontPadding = true;
  ReactNativeSimulator::TextRun onlyLargeRun = large;
  onlyLarge.runs.push_back(onlyLargeRun);
  const auto largeOnly = engine.prepare(onlyLarge, 280);
  if (prepared->height() + 4.0f < largeOnly->height()) {
    std::cerr << "mixed-size includeFontPadding used the small run envelope,"
              << " mixedH=" << prepared->height()
              << " largeH=" << largeOnly->height() << '\n';
    return false;
  }

  ReactNativeSimulator::TextParagraph mixedLineHeight;
  mixedLineHeight.includeFontPadding = true;
  ReactNativeSimulator::TextRun body;
  body.text = "inexpensive ";
  body.fontSize = 16;
  body.lineHeight = 35;
  body.fontFamily = "sans-serif";
  body.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 1, 1};
  mixedLineHeight.runs.push_back(body);
  ReactNativeSimulator::TextRun nested;
  nested.text = "Continually";
  nested.fontSize = 20;
  nested.lineHeight = 35;
  nested.fontFamily = "sans-serif";
  nested.foregroundColor = ReactNativeSimulator::TextColor{1, 0, 0, 1};
  mixedLineHeight.runs.push_back(nested);
  const auto lineHeightPrepared = engine.prepare(mixedLineHeight, 360);
  const auto lineHeightFrame =
      renderPrepared(renderer, lineHeightPrepared, 360, 48, density);
  if (!lineHeightFrame) {
    std::cerr << "mixed-size lineHeight render failed: "
              << lineHeightFrame.error << '\n';
    return false;
  }
  const auto lhBlue = inkBox(lineHeightFrame, 0, lineHeightFrame.width, isBlue);
  const auto lhRed = inkBox(lineHeightFrame, 0, lineHeightFrame.width, isRed);
  if (lhBlue.pixels < 8 || lhRed.pixels < 8) {
    std::cerr << "mixed-size lineHeight missing run ink, blue="
              << lhBlue.pixels << " red=" << lhRed.pixels << '\n';
    return false;
  }
  if (std::abs(lhBlue.bottom - lhRed.bottom) > baselineSlop) {
    std::cerr << "lineHeight mixed nested sizes did not share alphabetic "
              << "baseline, blueBottom=" << lhBlue.bottom
              << " redBottom=" << lhRed.bottom << " H="
              << lineHeightPrepared->height() << '\n';
    return false;
  }
  ReactNativeSimulator::TextParagraph compressedLines;
  compressedLines.includeFontPadding = true;
  ReactNativeSimulator::TextRun compressed;
  compressed.text = "one\ntwo\nthree\nfour\nfive";
  compressed.fontSize = 16;
  compressed.lineHeight = 8;
  compressedLines.runs.push_back(compressed);
  const auto compressedPrepared = engine.prepare(compressedLines, 300, density);
  if (compressedPrepared->lineCount() != 5 ||
      compressedPrepared->height() < 39.5f ||
      compressedPrepared->height() > 40.5f) {
    std::cerr << "explicit 8dp lineHeight was clamped to glyph metrics, lines="
              << compressedPrepared->lineCount() << " height="
              << compressedPrepared->height() << '\n';
    return false;
  }
  return true;
}

bool hanUsesNotoSansCjk(
    ReactNativeSimulator::SkiaTextLayoutEngine& engine) {
  auto sample = [](std::string family) {
    ReactNativeSimulator::TextParagraph paragraph;
    paragraph.includeFontPadding = true;
    ReactNativeSimulator::TextRun run;
    run.text = "星";
    run.fontSize = 14;
    run.fontFamily = std::move(family);
    paragraph.runs.push_back(std::move(run));
    return paragraph;
  };
  const auto fallback = engine.prepare(sample(""), 80);
  const auto sc = engine.prepare(sample("Noto Sans CJK SC"), 80);
  const auto jp = engine.prepare(sample("Noto Sans CJK JP"), 80);
  const auto roboto = engine.prepare(sample("Roboto"), 80);
  const auto latin = engine.prepare(
      [] {
        ReactNativeSimulator::TextParagraph paragraph;
        ReactNativeSimulator::TextRun run;
        run.text = "M";
        run.fontSize = 14;
        run.fontFamily = "sans-serif";
        paragraph.runs.push_back(std::move(run));
        return paragraph;
      }(),
      80);
  const auto fallbackW = fallback->longestLine();
  const auto scW = sc->longestLine();
  const auto jpW = jp->longestLine();
  const auto robotoW = roboto->longestLine();
  const auto latinW = latin->longestLine();
  // Noto Sans CJK ideographs are 1em. Roboto has no Han; a .notdef/tofu or
  // last-resort must not win over the configured TTC.
  if (std::fabs(scW - 14.0f) > 1.5f && std::fabs(jpW - 14.0f) > 1.5f) {
    std::cerr << "Noto Sans CJK was not loaded, scW=" << scW
              << " jpW=" << jpW << '\n';
    return false;
  }
  const auto cjkW = std::fabs(scW - 14.0f) <= 1.5f ? scW : jpW;
  if (std::fabs(fallbackW - cjkW) > 0.75f) {
    std::cerr << "Han fallback did not select Noto Sans CJK, fallbackW="
              << fallbackW << " cjkW=" << cjkW << " robotoW=" << robotoW
              << " latinW=" << latinW << '\n';
    return false;
  }
  if (std::fabs(robotoW - cjkW) < 0.25f &&
      std::fabs(robotoW - latinW) < 0.75f) {
    std::cerr << "Roboto claimed Han at Latin width, robotoW=" << robotoW
              << " cjkW=" << cjkW << " latinW=" << latinW << '\n';
    return false;
  }
  ReactNativeSimulator::TextParagraph banner;
  banner.includeFontPadding = true;
  banner.breakStrategy = ReactNativeSimulator::TextBreakStrategy::HighQuality;
  ReactNativeSimulator::TextRun bannerRun;
  bannerRun.text =
      "星际争霸是世界上最好的游戏。星际争霸是世界上最好的游戏。"
      "星际争霸是世界上最好的游戏。星际争霸是世界上最好的游戏。";
  bannerRun.fontSize = 14;
  banner.runs.push_back(bannerRun);
  const auto wrapped = engine.prepare(banner, 360.0f);
  ReactNativeSimulator::TextParagraph explicitBanner = banner;
  explicitBanner.runs.front().fontFamily = "Noto Sans CJK SC";
  const auto explicitWrapped = engine.prepare(explicitBanner, 360.0f);
  if (wrapped->lineCount() != explicitWrapped->lineCount()) {
    std::cerr << "Han wrap did not follow Noto Sans CJK, fallbackLines="
              << wrapped->lineCount() << " cjkLines="
              << explicitWrapped->lineCount() << " fallbackW="
              << fallbackW << '\n';
    return false;
  }
  if (wrapped->lineCount() < 2 || wrapped->lineCount() > 3) {
    std::cerr << "CJK 4-sentence banner wrap was not glance-level, lines="
              << wrapped->lineCount() << '\n';
    return false;
  }
  return true;
}

bool fallbackMatchesFamily(
    ReactNativeSimulator::SkiaTextLayoutEngine& engine,
    const char* script,
    std::string text,
    std::string family) {
  auto sample = [&](std::string fontFamily) {
    ReactNativeSimulator::TextParagraph paragraph;
    paragraph.includeFontPadding = true;
    ReactNativeSimulator::TextRun run;
    run.text = text;
    run.fontSize = 24;
    run.fontFamily = std::move(fontFamily);
    paragraph.runs.push_back(std::move(run));
    return paragraph;
  };
  const auto fallback = engine.prepare(sample(""), 400);
  const auto explicitFace = engine.prepare(sample(family), 400);
  const auto fallbackW = fallback->longestLine();
  const auto explicitW = explicitFace->longestLine();
  if (explicitW < 8.0f) {
    std::cerr << script << " font " << family << " did not load, width="
              << explicitW << '\n';
    return false;
  }
  if (std::fabs(fallbackW - explicitW) > 1.25f) {
    std::cerr << script << " fallback did not select " << family
              << ", fallbackW=" << fallbackW << " explicitW=" << explicitW
              << '\n';
    return false;
  }
  return true;
}

bool letterSpacingUppercaseFits(
    ReactNativeSimulator::SkiaTextLayoutEngine& engine) {
  ReactNativeSimulator::TextParagraph para;
  ReactNativeSimulator::TextRun run;
  run.text = "Works with other text styles";
  run.fontSize = 16;
  run.letterSpacing = 2;
  run.lineHeight = 32;
  run.textTransform = ReactNativeSimulator::TextTransform::Uppercase;
  run.foregroundColor = ReactNativeSimulator::TextColor{0.25f, 0.88f, 0.82f, 1};
  run.backgroundColor = ReactNativeSimulator::TextColor{0, 0, 1, 1};
  para.runs.push_back(std::move(run));
  const auto prepared = engine.prepare(para, 360);
  if (prepared->lineCount() != 1) {
    std::cerr << "letterSpacing uppercase wrapped, lines="
              << prepared->lineCount();
    for (const auto& line : prepared->lines()) {
      std::cerr << " [" << line.text << "]";
    }
    std::cerr << " width=" << prepared->width() << '\n';
    return false;
  }
  if (prepared->width() >= 350.0f) {
    std::cerr << "letterSpacing uppercase did not shrink-wrap, width="
              << prepared->width() << '\n';
    return false;
  }
  return true;
}

} // namespace

#if defined(__linux__)
constexpr bool kLinuxFontConfigHost = true;
#else
constexpr bool kLinuxFontConfigHost = false;
#endif

int main() {
  ReactNativeSimulator::SceneSnapshot scene;
  scene.surfaceId = 1;
  scene.revision = 1;
  scene.rootTag = 1;
  scene.viewportWidth = 100;
  scene.viewportHeight = 50;
  scene.pointScaleFactor = 2;

  ReactNativeSimulator::SceneNode root;
  root.tag = 1;
  root.layoutable = true;
  root.width = 100;
  root.height = 50;
  scene.nodes.push_back(root);

  ReactNativeSimulator::SceneNode child;
  child.tag = 2;
  child.parentTag = 1;
  child.layoutable = true;
  child.x = 5;
  child.y = 6;
  child.absoluteX = 5;
  child.absoluteY = 6;
  child.width = 40;
  child.height = 20;
  child.hasBackgroundColor = true;
  child.backgroundRed = 1;
  child.backgroundAlpha = 1;
  scene.nodes.push_back(child);

  ReactNativeSimulator::SkiaTextLayoutEngine textLayout;
  ReactNativeSimulator::TextParagraph textParagraph;
  ReactNativeSimulator::TextRun firstRun;
  firstRun.text = "Hello ";
  firstRun.fontSize = 16;
  firstRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  textParagraph.runs.push_back(firstRun);
  ReactNativeSimulator::TextRun secondRun;
  secondRun.text = "世界";
  secondRun.fontSize = 16;
  secondRun.fontWeight = 700;
  secondRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 1, 1};
  textParagraph.runs.push_back(secondRun);
  const auto preparedText = textLayout.prepare(textParagraph, 90);
  if (preparedText->width() <= 0 || preparedText->height() <= 0 ||
      preparedText->layoutWidth() != 90) {
    std::cerr << "Skia did not prepare measurable attributed text\n";
    return 1;
  }
  preparedText->markMeasured();
  if (!preparedText->wasMeasured() ||
      !preparedText->canPaintAtWidth(preparedText->width())) {
    std::cerr << "prepared paragraph measurement identity was lost\n";
    return 1;
  }

  auto wrappingParagraph = textParagraph;
  wrappingParagraph.runs.front().text = "one two three four five six";
  wrappingParagraph.runs.resize(1);
  const auto wrappedText = textLayout.prepare(wrappingParagraph, 35);
  if (wrappedText->height() <= preparedText->height() ||
      wrappedText->canPaintAtWidth(90)) {
    std::cerr << "wrapped paragraph was treated as width-independent\n";
    return 1;
  }

  ReactNativeSimulator::TextParagraph naturalRtlParagraph;
  ReactNativeSimulator::TextRun rtlRun;
  rtlRun.text = "(مرحبا)";
  rtlRun.fontSize = 16;
  naturalRtlParagraph.runs.push_back(rtlRun);
  const auto naturalRtlText = textLayout.prepare(naturalRtlParagraph, 90);
  if (naturalRtlText->writingDirection() !=
      ReactNativeSimulator::TextWritingDirection::RightToLeft) {
    std::cerr << "natural RTL direction was not retained\n";
    return 1;
  }
  if (naturalRtlText->lines().empty() ||
      naturalRtlText->lines().front().left > 8.0f) {
    std::cerr << "android auto/left packs RTL script to the left of an LTR paragraph\n";
    return 1;
  }

  ReactNativeSimulator::TextParagraph neutralParagraph;
  ReactNativeSimulator::TextRun neutralRun;
  neutralRun.text = "123 (---)";
  neutralParagraph.runs.push_back(neutralRun);
  const auto neutralText = textLayout.prepare(neutralParagraph, 90);
  if (neutralText->writingDirection() !=
      ReactNativeSimulator::TextWritingDirection::LeftToRight) {
    std::cerr << "neutral text did not use the deterministic LTR default\n";
    return 1;
  }

  auto explicitLtrParagraph = naturalRtlParagraph;
  explicitLtrParagraph.writingDirection =
      ReactNativeSimulator::TextWritingDirection::LeftToRight;
  if (textLayout.prepare(explicitLtrParagraph, 90)->writingDirection() !=
      ReactNativeSimulator::TextWritingDirection::LeftToRight) {
    std::cerr << "explicit LTR direction was not respected\n";
    return 1;
  }

  ReactNativeSimulator::TextParagraph attachmentParagraph;
  ReactNativeSimulator::TextRun attachment;
  attachment.attachment = true;
  attachment.attachmentWidth = 12;
  attachment.attachmentHeight = 8;
  attachmentParagraph.runs.push_back(attachment);
  const auto preparedAttachment = textLayout.prepare(attachmentParagraph, 30);
  if (preparedAttachment->attachments().size() != 1 ||
      preparedAttachment->attachments().front().width != 12 ||
      preparedAttachment->attachments().front().height != 8) {
    std::cerr << "inline attachment metrics were not retained\n";
    return 1;
  }
  ReactNativeSimulator::TextParagraph wrappedImageParagraph;
  ReactNativeSimulator::TextRun wrappedPrefix;
  wrappedPrefix.text = "This is an inline image";
  wrappedImageParagraph.runs.push_back(wrappedPrefix);
  ReactNativeSimulator::TextRun wrappedImage;
  wrappedImage.attachment = true;
  wrappedImage.attachmentWidth = 50;
  wrappedImage.attachmentHeight = 100;
  wrappedImageParagraph.runs.push_back(wrappedImage);
  const auto wrappedImagePrepared =
      textLayout.prepare(wrappedImageParagraph, 175);
  if (wrappedImagePrepared->attachments().size() != 1) {
    std::cerr << "wrapped inline image attachment missing\n";
    return 1;
  }
  const auto& wrappedImageAttachment =
      wrappedImagePrepared->attachments().front();
  if (wrappedImageAttachment.width + 0.5f < 50.0f ||
      wrappedImageAttachment.height + 0.5f < 100.0f ||
      wrappedImageAttachment.y < 10.0f) {
    std::cerr << "wrapped inline image placeholder size was not retained: "
              << wrappedImageAttachment.width << "x"
              << wrappedImageAttachment.height << " y="
              << wrappedImageAttachment.y << '\n';
    return 1;
  }

  auto tailEllipsis = wrappingParagraph;
  tailEllipsis.maximumNumberOfLines = 1;
  tailEllipsis.ellipsizeMode = ReactNativeSimulator::TextEllipsizeMode::Tail;
  if (!textLayout.prepare(tailEllipsis, 35)->exceededMaximumLines()) {
    std::cerr << "tail ellipsis did not enforce maximum lines\n";
    return 1;
  }

  ReactNativeSimulator::TextParagraph fitParagraph;
  fitParagraph.adjustsFontSizeToFit = true;
  fitParagraph.maximumNumberOfLines = 1;
  ReactNativeSimulator::TextRun fitRun;
  fitRun.text = "MMMMMMMMMMMMMMMM";
  fitRun.fontSize = 40;
  fitRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  fitParagraph.runs.push_back(fitRun);
  std::shared_ptr<const ReactNativeSimulator::SkiaPreparedParagraph> fitted;
  try {
    fitted = textLayout.prepare(fitParagraph, 40);
  } catch (const std::runtime_error& error) {
    std::cerr << "adjustsFontSizeToFit threw: " << error.what() << '\n';
    return 1;
  }
  if (!fitted || fitted->lineCount() == 0 || fitted->height() <= 0) {
    std::cerr << "adjustsFontSizeToFit did not produce a layout\n";
    return 1;
  }
  if (fitted->lineCount() > 1) {
    std::cerr << "adjustsFontSizeToFit kept more than one line\n";
    return 1;
  }

  ReactNativeSimulator::TextParagraph fitHeight;
  fitHeight.adjustsFontSizeToFit = true;
  fitHeight.maxHeight = 24;
  ReactNativeSimulator::TextRun fitHeightRun;
  fitHeightRun.text =
      "Text limited by height should shrink until it fits the box instead of overflowing.";
  fitHeightRun.fontSize = 20;
  fitHeightRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  fitHeight.runs.push_back(fitHeightRun);
  std::shared_ptr<const ReactNativeSimulator::SkiaPreparedParagraph>
      fittedHeight;
  try {
    fittedHeight = textLayout.prepare(fitHeight, 200);
  } catch (const std::runtime_error& error) {
    std::cerr << "adjustsFontSizeToFit maxHeight threw: " << error.what()
              << '\n';
    return 1;
  }
  if (!fittedHeight || fittedHeight->height() > 25.0f) {
    std::cerr << "adjustsFontSizeToFit did not shrink to maxHeight, height="
              << (fittedHeight ? fittedHeight->height() : -1) << '\n';
    return 1;
  }

  auto longEllipsis = wrappingParagraph;
  longEllipsis.maximumNumberOfLines = 1;
  longEllipsis.ellipsizeMode = ReactNativeSimulator::TextEllipsizeMode::Middle;
  std::shared_ptr<const ReactNativeSimulator::SkiaPreparedParagraph> middle;
  try {
    middle = textLayout.prepare(longEllipsis, 35);
  } catch (const std::runtime_error& error) {
    std::cerr << "middle ellipsis threw: " << error.what() << '\n';
    return 1;
  }
  if (!middle || middle->lineCount() != 1 || middle->longestLine() > 36.0f) {
    std::cerr << "middle ellipsis did not fit one line\n";
    return 1;
  }
  longEllipsis.ellipsizeMode = ReactNativeSimulator::TextEllipsizeMode::Head;
  std::shared_ptr<const ReactNativeSimulator::SkiaPreparedParagraph> head;
  try {
    head = textLayout.prepare(longEllipsis, 35);
  } catch (const std::runtime_error& error) {
    std::cerr << "head ellipsis threw: " << error.what() << '\n';
    return 1;
  }
  if (!head || head->lineCount() != 1 || head->longestLine() > 36.0f) {
    std::cerr << "head ellipsis did not fit one line\n";
    return 1;
  }
  auto shortEllipsis = textParagraph;
  shortEllipsis.maximumNumberOfLines = 1;
  shortEllipsis.ellipsizeMode = ReactNativeSimulator::TextEllipsizeMode::Middle;
  const auto shortPlain = textLayout.prepare(textParagraph, 90);
  const auto shortMiddle = textLayout.prepare(shortEllipsis, 90);
  if (std::fabs(shortPlain->width() - shortMiddle->width()) > 1.0f) {
    std::cerr << "middle ellipsis changed text that already fits\n";
    return 1;
  }

  ReactNativeSimulator::SceneNode textNode;
  textNode.tag = 3;
  textNode.parentTag = 1;
  textNode.layoutable = true;
  textNode.absoluteX = 5;
  textNode.absoluteY = 28;
  textNode.width = 90;
  textNode.height = preparedText->height();
  textNode.preparedText = preparedText;
  scene.nodes.push_back(textNode);

  ReactNativeSimulator::SceneNode scaled;
  scaled.tag = 4;
  scaled.parentTag = 1;
  scaled.layoutable = true;
  scaled.absoluteX = 70;
  scaled.absoluteY = 6;
  scaled.width = 20;
  scaled.height = 20;
  scaled.hasBackgroundColor = true;
  scaled.backgroundBlue = 1;
  scaled.backgroundAlpha = 1;
  scaled.hasTransform = true;
  scaled.transformM[0] = 2;
  scaled.transformM[5] = 2;
  scene.nodes.push_back(scaled);

  ReactNativeSimulator::SceneNode spinner;
  spinner.tag = 5;
  spinner.parentTag = 1;
  spinner.componentName = "AndroidProgressBar";
  spinner.layoutable = true;
  spinner.absoluteX = 78;
  spinner.absoluteY = 32;
  spinner.width = 16;
  spinner.height = 16;
  spinner.activityIndicator = true;
  spinner.activityIndicatorAnimating = true;
  spinner.hasActivityIndicatorColor = true;
  spinner.activityIndicatorRed = 0;
  spinner.activityIndicatorGreen = 1;
  spinner.activityIndicatorBlue = 0;
  spinner.activityIndicatorAlpha = 1;
  scene.nodes.push_back(spinner);

  ReactNativeSimulator::SkiaMountedTreeRenderer renderer;
  // 650ms is near the Material trim peak (~270deg), so the spinner is visible
  // instead of vanishing at the 0/1333ms cycle boundary.
  const auto frame = renderer.render(scene, 650);
  if (!frame || frame.width != 200 || frame.height != 100 ||
      frame.rowBytes != 800) {
    std::cerr << frame.error << '\n';
    return 1;
  }

  std::size_t visibleRedPixels = 0;
  std::size_t visibleTextPixels = 0;
  std::size_t visibleBluePixels = 0;
  std::size_t visibleGreenSpinnerPixels = 0;
  for (std::size_t offset = 0; offset + 3 < frame.rgba.size(); offset += 4) {
    if (frame.rgba[offset] > 200 && frame.rgba[offset + 1] < 20 &&
        frame.rgba[offset + 2] < 20 && frame.rgba[offset + 3] > 200) {
      ++visibleRedPixels;
    }
    if (frame.rgba[offset] < 20 && frame.rgba[offset + 1] < 20 &&
        frame.rgba[offset + 2] > 200 && frame.rgba[offset + 3] > 200) {
      ++visibleBluePixels;
    }
    if (frame.rgba[offset] < 40 && frame.rgba[offset + 1] > 180 &&
        frame.rgba[offset + 2] < 40 && frame.rgba[offset + 3] > 80) {
      ++visibleGreenSpinnerPixels;
    }
    if (frame.rgba[offset + 3] > 20 &&
        !(frame.rgba[offset] > 200 && frame.rgba[offset + 1] < 20 &&
          frame.rgba[offset + 2] < 20)) {
      ++visibleTextPixels;
    }
  }
  if (visibleTextPixels < 20) {
    std::cerr << "prepared paragraph produced only " << visibleTextPixels
              << " visible text pixels\n";
    return 1;
  }
  if (visibleRedPixels < 3000) {
    std::cerr << "typed scene rendered only " << visibleRedPixels
              << " visible red pixels\n";
    return 1;
  }
  if (visibleBluePixels < 4000) {
    std::cerr << "scaled transform rendered only " << visibleBluePixels
              << " visible blue pixels\n";
    return 1;
  }
  if (visibleGreenSpinnerPixels < 20) {
    std::cerr << "activity indicator rendered only "
              << visibleGreenSpinnerPixels << " visible green pixels\n";
    return 1;
  }
  const auto frameAgain = renderer.render(scene, 650);
  if (!frameAgain || frameAgain.rgba.size() != frame.rgba.size()) {
    std::cerr << "reused Skia surface failed to render: "
              << frameAgain.error << '\n';
    return 1;
  }
  std::size_t visibleRedPixelsAgain = 0;
  for (std::size_t offset = 0; offset + 3 < frameAgain.rgba.size();
       offset += 4) {
    if (frameAgain.rgba[offset] > 200 && frameAgain.rgba[offset + 1] < 20 &&
        frameAgain.rgba[offset + 2] < 20 &&
        frameAgain.rgba[offset + 3] > 200) {
      ++visibleRedPixelsAgain;
    }
  }
  if (visibleRedPixelsAgain + 50 < visibleRedPixels) {
    std::cerr << "reused Skia surface dropped red pixels from "
              << visibleRedPixels << " to " << visibleRedPixelsAgain << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot spinnerScene;
  spinnerScene.surfaceId = 1;
  spinnerScene.revision = 1;
  spinnerScene.rootTag = 1;
  spinnerScene.viewportWidth = 48;
  spinnerScene.viewportHeight = 48;
  spinnerScene.pointScaleFactor = 2;
  ReactNativeSimulator::SceneNode spinnerRoot;
  spinnerRoot.tag = 1;
  spinnerRoot.layoutable = true;
  spinnerRoot.width = 48;
  spinnerRoot.height = 48;
  spinnerScene.nodes.push_back(spinnerRoot);
  ReactNativeSimulator::SceneNode cycleSpinner;
  cycleSpinner.tag = 2;
  cycleSpinner.parentTag = 1;
  cycleSpinner.componentName = "AndroidProgressBar";
  cycleSpinner.layoutable = true;
  cycleSpinner.width = 48;
  cycleSpinner.height = 48;
  cycleSpinner.activityIndicator = true;
  cycleSpinner.activityIndicatorAnimating = true;
  cycleSpinner.hasActivityIndicatorColor = true;
  cycleSpinner.activityIndicatorRed = 0;
  cycleSpinner.activityIndicatorGreen = 1;
  cycleSpinner.activityIndicatorBlue = 0;
  cycleSpinner.activityIndicatorAlpha = 1;
  spinnerScene.nodes.push_back(cycleSpinner);
  const auto growing = renderer.render(spinnerScene, 80);
  const auto peak = renderer.render(spinnerScene, 666);
  const auto shrinking = renderer.render(spinnerScene, 1250);
  if (!growing || !peak || !shrinking) {
    std::cerr << "activity indicator cycle frames failed to render\n";
    return 1;
  }
  const auto growingPixels = countGreenSpinnerPixels(growing);
  const auto peakPixels = countGreenSpinnerPixels(peak);
  const auto shrinkingPixels = countGreenSpinnerPixels(shrinking);
  if (peakPixels < 80 || growingPixels >= peakPixels ||
      shrinkingPixels >= peakPixels) {
    std::cerr << "activity indicator trim-path cycle is not grow/chase: grow="
              << growingPixels << " peak=" << peakPixels
              << " shrink=" << shrinkingPixels << '\n';
    return 1;
  }
  if (const char* dumpDir = std::getenv("RNSIM_DUMP_SPINNER")) {
    std::filesystem::create_directories(dumpDir);
    const int times[] = {0, 166, 333, 500, 666, 833, 1000, 1166};
    for (int timeMs : times) {
      const auto dumpFrame = renderer.render(spinnerScene, timeMs);
      if (!dumpFrame) {
        std::cerr << "failed to dump spinner frame at " << timeMs << "ms\n";
        return 1;
      }
      const auto path = std::filesystem::path(dumpDir) /
          ("spinner-" + std::to_string(timeMs) + ".rgba");
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      out.write(
          reinterpret_cast<const char*>(dumpFrame.rgba.data()),
          static_cast<std::streamsize>(dumpFrame.rgba.size()));
      if (!out) {
        std::cerr << "failed to write " << path << '\n';
        return 1;
      }
    }
  }

  folly::dynamic wireNodes = folly::dynamic::array;
  wireNodes.push_back(folly::dynamic::object
      ("tag", 1)("parentTag", nullptr)("layoutable", true)
      ("layout", folly::dynamic::object
          ("x", 0)("y", 0)("width", 100)("height", 50)
          ("absoluteX", 0)("absoluteY", 0)("display", "flex"))
      ("props", folly::dynamic::object
          ("opacity", 1)("backgroundColor", nullptr)("borderRadius", 0)
          ("text", "")("fontSize", 0)("fontWeight", 400)
          ("fontFamily", "")("textColor", nullptr)("scroll", nullptr)));
  wireNodes.push_back(folly::dynamic::object
      ("tag", 2)("parentTag", 1)("layoutable", true)
      ("layout", folly::dynamic::object
          ("x", 5)("y", 6)("width", 40)("height", 20)
          ("absoluteX", 5)("absoluteY", 6)("display", "flex"))
      ("props", folly::dynamic::object
          ("opacity", 1)
          ("backgroundColor", folly::dynamic::object
              ("red", 1)("green", 0)("blue", 0)("alpha", 1))
          ("borderRadius", 0)("text", "")("fontSize", 0)
          ("fontWeight", 400)("fontFamily", "")("textColor", nullptr)
          ("scroll", nullptr)));
  folly::dynamic wire = folly::dynamic::object
      ("schemaVersion", 1)("surfaceId", 1)("revision", 1)("rootTag", 1)
      ("viewport", folly::dynamic::object
          ("width", 100)("height", 50)("pointScaleFactor", 2))
      ("mountingErrors", folly::dynamic::array)("nodes", wireNodes);
  const auto wireFrame = renderer.renderSceneWire(wire);
  if (!wireFrame || wireFrame.width != frame.width ||
      wireFrame.height != frame.height) {
    std::cerr << "independent scene wire payload did not render: "
              << wireFrame.error << '\n';
    return 1;
  }
  folly::dynamic invalidWire = wire;
  invalidWire["schemaVersion"] = 2;
  const auto invalidWireFrame = renderer.renderSceneWire(invalidWire);
  if (invalidWireFrame ||
      invalidWireFrame.error.find("schemaVersion") == std::string::npos) {
    std::cerr << "invalid scene wire schema was not rejected: "
              << invalidWireFrame.error << '\n';
    return 1;
  }
  invalidWire = wire;
  invalidWire.erase("nodes");
  const auto missingNodesFrame = renderer.renderSceneWire(invalidWire);
  if (missingNodesFrame ||
      missingNodesFrame.error.find("missing nodes") == std::string::npos) {
    std::cerr << "incomplete scene wire was not rejected: "
              << missingNodesFrame.error << '\n';
    return 1;
  }

  auto invalidScene = scene;
  invalidScene.nodes[1].parentTag = 999;
  const auto invalidFrame = renderer.render(invalidScene);
  if (invalidFrame ||
      invalidFrame.error.find("missing parent 999") == std::string::npos) {
    std::cerr << "invalid scene was not rejected: " << invalidFrame.error
              << '\n';
    return 1;
  }

  invalidScene = scene;
  invalidScene.nodes.push_back(invalidScene.nodes[1]);
  const auto duplicateFrame = renderer.render(invalidScene);
  if (duplicateFrame ||
      duplicateFrame.error.find("duplicate tag 2") == std::string::npos) {
    std::cerr << "duplicate scene tag was not rejected: "
              << duplicateFrame.error << '\n';
    return 1;
  }

  ReactNativeSimulator::SkiaTextLayoutEngine androidTextLayout(
      {}, ReactNativeSimulator::TextFontPlatform::Android);
  ReactNativeSimulator::TextParagraph paddingParagraph;
  ReactNativeSimulator::TextRun paddingRun;
  paddingRun.text = "Components";
  paddingRun.fontSize = 19;
  paddingRun.fontWeight = 600;
  paddingParagraph.runs.push_back(paddingRun);
  paddingParagraph.includeFontPadding = false;
  const auto withoutPadding = androidTextLayout.prepare(paddingParagraph, 300);
  paddingParagraph.includeFontPadding = true;
  const auto withPadding = androidTextLayout.prepare(paddingParagraph, 300);
  if (withPadding->height() <= withoutPadding->height()) {
    std::cerr << "android includeFontPadding did not increase height\n";
    return 1;
  }

  const std::filesystem::path androidFonts{"build/android-fonts"};
  if (std::filesystem::exists(androidFonts / "NotoSerif-Regular.ttf") &&
      std::filesystem::exists(androidFonts / "DroidSansMono.ttf")) {
    ReactNativeSimulator::SkiaTextLayoutEngine aliasedLayout(
        androidFonts, ReactNativeSimulator::TextFontPlatform::Android);
    ReactNativeSimulator::TextParagraph baselineAttachment;
    baselineAttachment.includeFontPadding = true;
    ReactNativeSimulator::TextRun baselineTextRun;
    baselineTextRun.text = "Text ";
    baselineTextRun.fontSize = 14;
    baselineAttachment.runs.push_back(baselineTextRun);
    ReactNativeSimulator::TextRun baselineViewRun;
    baselineViewRun.attachment = true;
    baselineViewRun.attachmentWidth = 25;
    baselineViewRun.attachmentHeight = 25;
    baselineAttachment.runs.push_back(baselineViewRun);
    const auto baselineAttachmentLayout =
        aliasedLayout.prepare(baselineAttachment, 300, 2.75f);
    if (!baselineAttachmentLayout ||
        baselineAttachmentLayout->attachments().size() != 1 ||
        baselineAttachmentLayout->lines().empty() ||
        std::fabs(
            baselineAttachmentLayout->attachments().front().y +
                baselineAttachmentLayout->attachments().front().height -
                baselineAttachmentLayout->lines().front().baseline) > 0.75f) {
      std::cerr << "first-line ReplacementSpan did not own alphabetic baseline\n";
      return 1;
    }
    auto sample = [](std::string family, int weight = 400) {
      ReactNativeSimulator::TextParagraph paragraph;
      ReactNativeSimulator::TextRun run;
      run.text = "MMMMMMMM";
      run.fontSize = 20;
      run.fontFamily = std::move(family);
      run.fontWeight = weight;
      paragraph.runs.push_back(std::move(run));
      return paragraph;
    };
    const auto sans = aliasedLayout.prepare(sample("sans-serif"), 400);
    const auto serif = aliasedLayout.prepare(sample("serif"), 400);
    const auto mono = aliasedLayout.prepare(sample("monospace"), 400);
    const auto unknown =
        aliasedLayout.prepare(sample("Unknown Font Family"), 400);
    if (std::fabs(serif->longestLine() - sans->longestLine()) < 0.5f) {
      std::cerr << "serif alias did not select a different family from sans-serif\n";
      return 1;
    }
    if (std::fabs(mono->longestLine() - sans->longestLine()) < 0.5f) {
      std::cerr << "monospace alias did not select a different family from sans-serif\n";
      return 1;
    }
    if (std::fabs(unknown->longestLine() - sans->longestLine()) > 1.0f) {
      std::cerr << "unknown family did not fall back to sans-serif\n";
      return 1;
    }
    auto sampleInk = sample("sans-serif-light");
    sampleInk.runs.front().foregroundColor =
        ReactNativeSimulator::TextColor{0, 0, 0, 1};
    auto boldInkPara = sample("sans-serif", 700);
    boldInkPara.runs.front().foregroundColor =
        ReactNativeSimulator::TextColor{0, 0, 0, 1};
    const auto light = aliasedLayout.prepare(sampleInk, 400);
    const auto boldSans = aliasedLayout.prepare(boldInkPara, 400);
    auto darkGlyphPixels = [&](const auto& prepared) {
      const auto frame = renderPrepared(renderer, prepared, 220, 32);
      if (!frame) {
        std::cerr << "sans-serif weight render failed: " << frame.error
                  << '\n';
        return static_cast<std::size_t>(0);
      }
      std::size_t count = 0;
      for (std::size_t offset = 0; offset + 3 < frame.rgba.size();
           offset += 4) {
        if (frame.rgba[offset] < 80 && frame.rgba[offset + 1] < 80 &&
            frame.rgba[offset + 2] < 80 && frame.rgba[offset + 3] > 80) {
          ++count;
        }
      }
      return count;
    };
    const auto lightInk = darkGlyphPixels(light);
    const auto boldInk = darkGlyphPixels(boldSans);
    // HINTING_ON can snap Light "M" advances wider than Bold at 20px.
    // Stem coverage still distinguishes the mapped Roboto weights.
    if (lightInk == 0 || boldInk <= lightInk) {
      std::cerr << "sans-serif-light was not lighter than sans-serif bold"
                << ", lightInk=" << lightInk << " boldInk=" << boldInk
                << " lightW=" << light->longestLine()
                << " boldW=" << boldSans->longestLine() << '\n';
      return 1;
    }
    if (!letterSpacingUppercaseFits(aliasedLayout)) {
      return 1;
    }
    ReactNativeSimulator::TextParagraph pixelCard;
    ReactNativeSimulator::TextRun pixelRun;
    pixelRun.text = "Works with other text styles";
    pixelRun.fontSize = 16;
    pixelRun.letterSpacing = 2;
    pixelRun.lineHeight = 32;
    pixelRun.textTransform = ReactNativeSimulator::TextTransform::Uppercase;
    pixelCard.runs.push_back(std::move(pixelRun));
    const auto pixelPrepared = aliasedLayout.prepare(pixelCard, 310.5f);
    if (pixelPrepared->lineCount() != 1) {
      std::cerr << "letterSpacing uppercase wrapped at Pixel card ~310dp, lines="
                << pixelPrepared->lineCount() << " width="
                << pixelPrepared->width() << '\n';
      return 1;
    }
    if (!robotoSmallCapsMatchesPlain(aliasedLayout, renderer)) {
      return 1;
    }
    ReactNativeSimulator::TextParagraph crlfParagraph;
    ReactNativeSimulator::TextRun crlfRun;
    crlfRun.text =
        ".aa\tbb\t\tcc  dd EE \r\nZZ I like to eat apples. "
        "\n中文éé 我喜欢吃苹果。awdawd   ";
    crlfRun.fontSize = 14;
    crlfParagraph.runs.push_back(crlfRun);
    const auto crlfPrepared =
        aliasedLayout.prepare(crlfParagraph, 392.7273f, 2.75f);
    if (!crlfPrepared || crlfPrepared->lineCount() != 3) {
      std::cerr << "Android CRLF did not normalize to one line break, lines="
                << (crlfPrepared ? crlfPrepared->lineCount() : 0) << '\n';
      return 1;
    }

    // Pixel 4a TextExample magenta boxes: content-box ~290dp, HIGH_QUALITY
    // keeps greedy wrap ("uniform" on line 1, "borderRadii" on line 2).
    ReactNativeSimulator::TextParagraph magentaBox;
    magentaBox.breakStrategy =
        ReactNativeSimulator::TextBreakStrategy::HighQuality;
    magentaBox.includeFontPadding = true;
    ReactNativeSimulator::TextRun magentaRun;
    magentaRun.text = "Text with background color and uniform borderRadii";
    magentaRun.fontSize = 14;
    magentaBox.runs.push_back(magentaRun);
    const auto magentaPrepared = aliasedLayout.prepare(magentaBox, 290.0f);
    // Pixel 4a: 14sp ceils to 39px, StaticLayout 2-line includePad is 99px,
    // 99/2.75 = 36dp; Yoga padding 10+10 → 56dp magenta box.
    const auto magentaAtDensity = aliasedLayout.prepare(
        magentaBox, 290.0f, 2.75f);
    if (magentaAtDensity->lineCount() != 2) {
      std::cerr << "magenta box at 2.75 density wrapped to "
                << magentaAtDensity->lineCount() << " lines, expected 2\n";
      return 1;
    }
    const auto boxHeight = magentaAtDensity->height() + 20.0f;
    if (std::fabs(boxHeight - 56.0f) > 0.05f) {
      std::cerr << "magenta 2-line box at 2.75 density was "
                << boxHeight << "dp, expected 56dp (text "
                << magentaAtDensity->height() << ")\n";
      return 1;
    }
    ReactNativeSimulator::TextParagraph inlineWrap;
    ReactNativeSimulator::TextRun inlinePrefix;
    inlinePrefix.text = "ParentChild";
    inlinePrefix.fontSize = 14;
    inlineWrap.runs.push_back(inlinePrefix);
    ReactNativeSimulator::TextRun inlineView;
    inlineView.attachment = true;
    inlineView.attachmentWidth = 30;
    inlineView.attachmentHeight = 30;
    inlineWrap.runs.push_back(inlineView);
    ReactNativeSimulator::TextRun inlineSuffix;
    inlineSuffix.text =
        "Childaaaa a aaaa aaaaaa aaa a a a aaaaa sdsds dsdSAD asd ASDasd ASDas";
    inlineSuffix.fontSize = 14;
    inlineWrap.runs.push_back(inlineSuffix);
    const auto inlinePrepared =
        aliasedLayout.prepare(inlineWrap, 384.73f, 2.75f);
    if (inlinePrepared->lineCount() < 2 || inlinePrepared->lines().empty()) {
      std::cerr << "inline StaticLayout fixture did not wrap\n";
      return 1;
    }
    const auto& inlineLastLine = inlinePrepared->lines().back();
    const auto inlinePaintBottom =
        inlineLastLine.baseline + inlineLastLine.descent;
    if (inlinePrepared->height() + 0.01f < inlinePaintBottom) {
      std::cerr << "inline StaticLayout height clipped the last line: height="
                << inlinePrepared->height() << " bottom=" << inlinePaintBottom
                << '\n';
      return 1;
    }
    const auto magentaSimple = [&] {
      auto simple = magentaBox;
      simple.breakStrategy = ReactNativeSimulator::TextBreakStrategy::Simple;
      return aliasedLayout.prepare(simple, 290.0f);
    }();
    auto magentaLine = [](const auto& prepared, std::size_t index) {
      return index < prepared->lines().size() ? prepared->lines()[index].text
                                              : std::string{};
    };
    const auto magentaFirst = magentaLine(magentaPrepared, 0);
    const auto magentaSecond = magentaLine(magentaPrepared, 1);
    if (magentaPrepared->lineCount() != 2 ||
        magentaFirst.find("uniform") == std::string::npos ||
        magentaSecond.find("borderRadii") == std::string::npos ||
        magentaSecond.find("uniform") != std::string::npos) {
      std::cerr << "magenta box HighQuality wrap was not greedy Pixel wrap,"
                << " lines=" << magentaPrepared->lineCount()
                << " first=[" << magentaFirst << "] second=["
                << magentaSecond << "]\n";
      return 1;
    }
    if (magentaSimple->lineCount() != magentaPrepared->lineCount() ||
        magentaLine(magentaSimple, 0).find("uniform") == std::string::npos) {
      std::cerr << "magenta box Simple wrap diverged from HighQuality\n";
      return 1;
    }
    ReactNativeSimulator::TextParagraph baselineLongText;
    baselineLongText.breakStrategy =
        ReactNativeSimulator::TextBreakStrategy::HighQuality;
    ReactNativeSimulator::TextRun baselineLongRun;
    baselineLongRun.fontSize = 15;
    baselineLongRun.text =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
        "eiusmod tempor incididunt ut labore et dolore magna aliqua.";
    baselineLongText.runs.push_back(baselineLongRun);
    const auto baselineLongPrepared =
        aliasedLayout.prepare(baselineLongText, 125.0f, 2.75f);
    const auto trimmedLines = [](const auto& prepared) {
      std::vector<std::string> lines;
      for (const auto& line : prepared.lines()) {
        auto text = line.text;
        while (!text.empty() &&
               (text.back() == ' ' || text.back() == '\t' ||
                text.back() == '\n')) {
          text.pop_back();
        }
        lines.push_back(std::move(text));
      }
      return lines;
    };
    const std::vector<std::string> expected15{
        "Lorem ipsum",
        "dolor sit amet,",
        "consectetur",
        "adipiscing elit,",
        "sed do eiusmod",
        "tempor incididunt",
        "ut labore et dolore",
        "magna aliqua.",
    };
    const auto actual15 = trimmedLines(*baselineLongPrepared);
    if (actual15 != expected15) {
      std::string baselineLines;
      for (const auto& line : baselineLongPrepared->lines()) {
        baselineLines += '[' + line.text + ']';
      }
      std::cerr << "Android HighQuality 15sp baseline wrap diverged: "
                << baselineLines << '\n';
      return 1;
    }
    baselineLongText.runs.front().fontSize = 10;
    const auto baselineSmallPrepared =
        aliasedLayout.prepare(baselineLongText, 125.0f, 2.75f);
    const std::vector<std::string> expected10{
        "Lorem ipsum dolor",
        "sit amet, consectetur",
        "adipiscing elit, sed do",
        "eiusmod tempor incididunt",
        "ut labore et dolore magna",
        "aliqua.",
    };
    const auto actual10 = trimmedLines(*baselineSmallPrepared);
    if (actual10 != expected10) {
      std::string baselineLines;
      for (const auto& line : baselineSmallPrepared->lines()) {
        baselineLines += '[' + line.text + ']';
      }
      std::cerr << "Android HighQuality 10sp baseline wrap diverged: "
                << baselineLines << '\n';
      return 1;
    }
    // 36pt Dynamic Font tail: includeFontPadding extras follow this font's
    // OS/2 win (2146/555 per 2048 ≈ 47.5dp), matching Pixel 4a of the same
    // file. Do not pin the historical 1946/512 grid.
    ReactNativeSimulator::TextParagraph dynamicPad;
    dynamicPad.includeFontPadding = true;
    dynamicPad.maximumNumberOfLines = 1;
    dynamicPad.ellipsizeMode = ReactNativeSimulator::TextEllipsizeMode::Tail;
    ReactNativeSimulator::TextRun dynamicPadRun;
    dynamicPadRun.text = "Truncated text is baaaaad.";
    dynamicPadRun.fontSize = 36;
    dynamicPad.runs.push_back(dynamicPadRun);
    const auto dynamicWithPad = aliasedLayout.prepare(dynamicPad, 332.73f);
    dynamicPad.includeFontPadding = false;
    const auto dynamicNoPad = aliasedLayout.prepare(dynamicPad, 332.73f);
    const auto dynamicPadText = dynamicWithPad->lines().empty()
        ? std::string{}
        : dynamicWithPad->lines().front().text;
    if (dynamicWithPad->lineCount() != 1 ||
        dynamicWithPad->height() <= dynamicNoPad->height() ||
        dynamicWithPad->height() < 46.5f ||
        dynamicWithPad->height() > 49.0f ||
        dynamicPadText.find("is b") == std::string::npos ||
        (dynamicPadText.find("…") == std::string::npos &&
         dynamicPadText.find("...") == std::string::npos) ||
        dynamicPadText.find("baaaaad") != std::string::npos) {
      std::cerr << "36pt includeFontPadding line box was not Android-like, H="
                << dynamicWithPad->height() << " noPadH="
                << dynamicNoPad->height() << " text=" << dynamicPadText
                << '\n';
      return 1;
    }
    if (!mixedSizeSharesAlphabeticBaseline(aliasedLayout, renderer)) {
      return 1;
    }
    if (std::filesystem::exists(androidFonts / "NotoSansCJK-Regular.ttc") &&
        !hanUsesNotoSansCjk(aliasedLayout)) {
      return 1;
    }
    if (std::filesystem::exists(androidFonts / "NotoSansCJK-Regular.ttc")) {
      ReactNativeSimulator::TextParagraph cjkWrap;
      ReactNativeSimulator::TextRun cjkRun;
      cjkRun.text =
          "星际争霸是世界上最好的游戏。星际争霸是世界上最好的游戏。"
          "星际争霸是世界上最好的游戏。星际争霸是世界上最好的游戏。";
      cjkRun.fontSize = 14;
      cjkWrap.runs.push_back(cjkRun);
      const auto cjkLaid = aliasedLayout.prepare(cjkWrap, 360);
      if (!cjkLaid || cjkLaid->lineCount() < 3) {
        std::cerr << "CJK pangram did not wrap, lines="
                  << (cjkLaid ? cjkLaid->lineCount() : 0) << '\n';
        return 1;
      }
      const auto cjkPixelWidth =
          aliasedLayout.prepare(cjkWrap, 392.7273f, 2.75f);
      if (!cjkPixelWidth || cjkPixelWidth->lineCount() < 3) {
        std::cerr << "Pixel-width CJK pangram did not preserve 39px advances, lines="
                  << (cjkPixelWidth ? cjkPixelWidth->lineCount() : 0) << '\n';
        return 1;
      }
      auto cjkOneLine = cjkWrap;
      cjkOneLine.runs.front().text = "星际争霸是世界上最好的游戏。";
      const auto cjkPixelLine =
          aliasedLayout.prepare(cjkOneLine, 392.7273f, 2.75f);
      if (!cjkPixelLine || cjkPixelLine->lineCount() != 1 ||
          cjkPixelLine->height() < 20.0f || cjkPixelLine->height() > 22.0f) {
        std::cerr << "Pixel CJK line retained duplicate includePad, height="
                  << (cjkPixelLine ? cjkPixelLine->height() : 0) << '\n';
        return 1;
      }
      auto mixedFallbackLines = cjkWrap;
      mixedFallbackLines.runs.front().text = "latin\nlatin\n中文";
      auto allFallbackLines = cjkWrap;
      allFallbackLines.runs.front().text = "中文\n中文\n中文";
      const auto mixedFallback =
          aliasedLayout.prepare(mixedFallbackLines, 392.7273f, 2.75f);
      const auto allFallback =
          aliasedLayout.prepare(allFallbackLines, 392.7273f, 2.75f);
      if (!mixedFallback || !allFallback ||
          mixedFallback->lineCount() != 3 || allFallback->lineCount() != 3 ||
          mixedFallback->height() + 3.0f >= allFallback->height()) {
        std::cerr << "CJK fallback metrics leaked into Latin-only lines, mixed="
                  << (mixedFallback ? mixedFallback->height() : 0)
                  << " allCjk="
                  << (allFallback ? allFallback->height() : 0) << '\n';
        return 1;
      }
    }
    if (std::filesystem::exists(androidFonts / "NotoSansHebrew-Regular.ttf") &&
        !fallbackMatchesFamily(
            aliasedLayout, "Hebrew", "דג סקרן", "Noto Sans Hebrew")) {
      return 1;
    }
    if (std::filesystem::exists(androidFonts / "NotoSansHebrew-Regular.ttf")) {
      ReactNativeSimulator::TextParagraph hebrewPangram;
      hebrewPangram.includeFontPadding = true;
      ReactNativeSimulator::TextRun hebrewRun;
      hebrewRun.text =
          "דג סקרן שט בים מאוכזב ולפתע מצא חברה";
      hebrewRun.fontSize = 50;
      hebrewPangram.runs.push_back(hebrewRun);
      const auto hebrewLaid = aliasedLayout.prepare(hebrewPangram, 360);
      if (!hebrewLaid || hebrewLaid->lineCount() == 0 ||
          hebrewLaid->height() < 40.0f ||
          hebrewLaid->writingDirection() !=
              ReactNativeSimulator::TextWritingDirection::RightToLeft) {
        std::cerr << "Hebrew pangram did not layout as RTL, lines="
                  << (hebrewLaid ? hebrewLaid->lineCount() : 0) << " H="
                  << (hebrewLaid ? hebrewLaid->height() : 0) << '\n';
        return 1;
      }
      ReactNativeSimulator::SceneSnapshot hebrewScene;
      hebrewScene.surfaceId = 1;
      hebrewScene.revision = 1;
      hebrewScene.rootTag = 1;
      hebrewScene.viewportWidth = 360;
      hebrewScene.viewportHeight = 200;
      hebrewScene.pointScaleFactor = 2.75f;
      ReactNativeSimulator::SceneNode hebrewRoot;
      hebrewRoot.tag = 1;
      hebrewRoot.layoutable = true;
      hebrewRoot.width = 360;
      hebrewRoot.height = 200;
      hebrewRoot.hasBackgroundColor = true;
      hebrewRoot.backgroundRed = 1;
      hebrewRoot.backgroundGreen = 1;
      hebrewRoot.backgroundBlue = 1;
      hebrewRoot.backgroundAlpha = 1;
      hebrewScene.nodes.push_back(hebrewRoot);
      ReactNativeSimulator::SceneNode hebrewText;
      hebrewText.tag = 2;
      hebrewText.parentTag = 1;
      hebrewText.layoutable = true;
      hebrewText.width = 360;
      hebrewText.height = hebrewLaid->height();
      hebrewText.preparedText = hebrewLaid;
      hebrewScene.nodes.push_back(hebrewText);
      const auto hebrewFrame = renderer.render(hebrewScene);
      if (!hebrewFrame || hebrewFrame.rgba.empty()) {
        std::cerr << "Hebrew pangram paint failed: " << hebrewFrame.error
                  << '\n';
        return 1;
      }
    }
    if (std::filesystem::exists(
            androidFonts / "NotoSansDevanagariUI-VF.ttf") &&
        !fallbackMatchesFamily(
            aliasedLayout, "Hindi", "राम", "Noto Sans Devanagari UI")) {
      return 1;
    }
    if (std::filesystem::exists(androidFonts / "NotoSansThaiUI-Regular.ttf") &&
        !fallbackMatchesFamily(
            aliasedLayout, "Thai", "กขคง", "Noto Sans Thai UI")) {
      return 1;
    }
    if (std::filesystem::exists(androidFonts / "NotoColorEmoji.ttf") &&
        !fallbackMatchesFamily(
            aliasedLayout, "Emoji", "😍🚗", "Noto Color Emoji")) {
      return 1;
    }
    ReactNativeSimulator::TextParagraph digits;
    ReactNativeSimulator::TextRun digitsRun;
    digitsRun.text = "FONT WEIGHT 900";
    digitsRun.fontSize = 14;
    digits.runs.push_back(digitsRun);
    const auto digitsFallback = aliasedLayout.prepare(digits, 400);
    digits.runs.front().fontFamily = "Roboto";
    const auto digitsRoboto = aliasedLayout.prepare(digits, 400);
    if (std::fabs(
            digitsFallback->longestLine() - digitsRoboto->longestLine()) >
        1.0f) {
      std::cerr << "ASCII digits used color-emoji fallback, fallbackW="
                << digitsFallback->longestLine() << " robotoW="
                << digitsRoboto->longestLine() << '\n';
      return 1;
    }
  }

  ReactNativeSimulator::TextParagraph underlineParagraph;
  ReactNativeSimulator::TextRun underlineRun;
  underlineRun.text = "WWWWWW";
  underlineRun.fontSize = 24;
  underlineRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  underlineRun.decorationLine =
      ReactNativeSimulator::TextDecorationLine::Underline;
  underlineRun.decorationColor = ReactNativeSimulator::TextColor{1, 0, 0, 1};
  underlineParagraph.runs.push_back(underlineRun);
  const auto underlined = textLayout.prepare(underlineParagraph, 160);
  ReactNativeSimulator::SceneSnapshot decoScene;
  decoScene.surfaceId = 1;
  decoScene.revision = 1;
  decoScene.rootTag = 1;
  decoScene.viewportWidth = 160;
  decoScene.viewportHeight = 40;
  decoScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode decoRoot;
  decoRoot.tag = 1;
  decoRoot.layoutable = true;
  decoRoot.width = 160;
  decoRoot.height = 40;
  decoRoot.hasBackgroundColor = true;
  decoRoot.backgroundRed = 1;
  decoRoot.backgroundGreen = 1;
  decoRoot.backgroundBlue = 1;
  decoRoot.backgroundAlpha = 1;
  decoScene.nodes.push_back(decoRoot);
  ReactNativeSimulator::SceneNode decoText;
  decoText.tag = 2;
  decoText.parentTag = 1;
  decoText.layoutable = true;
  decoText.width = 160;
  decoText.height = std::max(underlined->height(), 28.0f);
  decoText.preparedText = underlined;
  decoScene.nodes.push_back(decoText);
  const auto decoFrame = renderer.render(decoScene);
  if (!decoFrame) {
    std::cerr << "underline scene failed: " << decoFrame.error << '\n';
    return 1;
  }
  std::size_t redUnderlinePixels = 0;
  for (std::size_t offset = 0; offset + 3 < decoFrame.rgba.size();
       offset += 4) {
    if (decoFrame.rgba[offset] > 180 && decoFrame.rgba[offset + 1] < 80 &&
        decoFrame.rgba[offset + 2] < 80 && decoFrame.rgba[offset + 3] > 80) {
      ++redUnderlinePixels;
    }
  }
  if (redUnderlinePixels < 8) {
    std::cerr << "underline decoration did not paint, redPixels="
              << redUnderlinePixels << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph upperParagraph;
  ReactNativeSimulator::TextRun upperRun;
  upperRun.text = "AbC";
  upperRun.fontSize = 16;
  upperRun.textTransform = ReactNativeSimulator::TextTransform::Uppercase;
  upperRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  upperParagraph.runs.push_back(upperRun);
  const auto upperPrepared = textLayout.prepare(upperParagraph, 80);
  ReactNativeSimulator::TextParagraph plainUpper;
  ReactNativeSimulator::TextRun plainRun;
  plainRun.text = "ABC";
  plainRun.fontSize = 16;
  plainRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  plainUpper.runs.push_back(plainRun);
  const auto plainPrepared = textLayout.prepare(plainUpper, 80);
  if (std::fabs(upperPrepared->width() - plainPrepared->width()) > 1.5f) {
    std::cerr << "uppercase transform did not match ABC width\n";
    return 1;
  }

  ReactNativeSimulator::TextParagraph spacedLetters;
  ReactNativeSimulator::TextRun spacedRun;
  spacedRun.text = "AAAA";
  spacedRun.fontSize = 16;
  spacedRun.letterSpacing = 10;
  spacedRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  spacedLetters.runs.push_back(spacedRun);
  ReactNativeSimulator::TextParagraph unspacedLetters = spacedLetters;
  unspacedLetters.runs.front().letterSpacing.reset();
  const auto spacedPrepared = textLayout.prepare(spacedLetters, 400);
  const auto unspacedPrepared = textLayout.prepare(unspacedLetters, 400);
  const float letterSpacingExtra =
      spacedPrepared->longestLine() - unspacedPrepared->longestLine();
  // SkParagraph adds tracking after every glyph (N * spacing). Android /
  // CSS keep (N-1) * spacing by omitting the line-end edge. "AAAA" at
  // letterSpacing 10 must grow by ~30, not ~40.
  if (std::fabs(letterSpacingExtra - 30.0f) > 1.5f) {
    std::cerr << "letterSpacing extra was " << letterSpacingExtra
              << ", expected ~30 for 3 gaps\n";
    return 1;
  }

  if (!letterSpacingUppercaseFits(textLayout)) {
    return 1;
  }

  ReactNativeSimulator::TextParagraph shadowParagraph;
  ReactNativeSimulator::TextRun shadowRun;
  shadowRun.text = "Hi";
  shadowRun.fontSize = 20;
  shadowRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  shadowRun.textShadowOffsetX = 2;
  shadowRun.textShadowOffsetY = 2;
  shadowRun.textShadowRadius = 1;
  shadowRun.textShadowColor = ReactNativeSimulator::TextColor{0, 0.8f, 0.8f, 1};
  shadowParagraph.runs.push_back(shadowRun);
  const auto shadowText = textLayout.prepare(shadowParagraph, 60);
  ReactNativeSimulator::SceneSnapshot shadowTextScene;
  shadowTextScene.surfaceId = 1;
  shadowTextScene.revision = 1;
  shadowTextScene.rootTag = 1;
  shadowTextScene.viewportWidth = 40;
  shadowTextScene.viewportHeight = 30;
  shadowTextScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode textShadowRoot;
  textShadowRoot.tag = 1;
  textShadowRoot.layoutable = true;
  textShadowRoot.width = 40;
  textShadowRoot.height = 30;
  textShadowRoot.hasBackgroundColor = true;
  textShadowRoot.backgroundRed = 1;
  textShadowRoot.backgroundGreen = 1;
  textShadowRoot.backgroundBlue = 1;
  textShadowRoot.backgroundAlpha = 1;
  shadowTextScene.nodes.push_back(textShadowRoot);
  ReactNativeSimulator::SceneNode textShadowNode;
  textShadowNode.tag = 2;
  textShadowNode.parentTag = 1;
  textShadowNode.layoutable = true;
  textShadowNode.width = 40;
  textShadowNode.height = 30;
  textShadowNode.preparedText = shadowText;
  shadowTextScene.nodes.push_back(textShadowNode);
  const auto shadowTextFrame = renderer.render(shadowTextScene);
  if (!shadowTextFrame) {
    std::cerr << "text shadow scene failed: " << shadowTextFrame.error << '\n';
    return 1;
  }
  std::size_t cyanShadowPixels = 0;
  for (std::size_t offset = 0; offset + 3 < shadowTextFrame.rgba.size();
       offset += 4) {
    if (shadowTextFrame.rgba[offset] < 80 &&
        shadowTextFrame.rgba[offset + 1] > 120 &&
        shadowTextFrame.rgba[offset + 2] > 120 &&
        shadowTextFrame.rgba[offset + 3] > 40) {
      ++cyanShadowPixels;
    }
  }
  if (cyanShadowPixels < 4) {
    std::cerr << "text shadow did not paint, cyanPixels=" << cyanShadowPixels
              << '\n';
    return 1;
  }

  const auto pngPath =
      std::filesystem::temp_directory_path() / "rnsim-skia-image-smoke.png";
  {
    static const std::uint8_t kRedPng[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00,
        0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE,
        0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63,
        0xF8, 0xCF, 0xC0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0x18, 0xDD,
        0x8D, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
        0x42, 0x60, 0x82};
    std::ofstream png(pngPath, std::ios::binary);
    png.write(reinterpret_cast<const char*>(kRedPng), sizeof(kRedPng));
    if (!png) {
      std::cerr << "failed to write image smoke fixture\n";
      return 1;
    }
  }
  ReactNativeSimulator::SceneSnapshot imageScene;
  imageScene.surfaceId = 1;
  imageScene.revision = 1;
  imageScene.rootTag = 1;
  imageScene.viewportWidth = 20;
  imageScene.viewportHeight = 20;
  imageScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode imageRoot;
  imageRoot.tag = 1;
  imageRoot.layoutable = true;
  imageRoot.width = 20;
  imageRoot.height = 20;
  imageScene.nodes.push_back(imageRoot);
  ReactNativeSimulator::SceneNode imageNode;
  imageNode.tag = 2;
  imageNode.parentTag = 1;
  imageNode.componentName = "Image";
  imageNode.layoutable = true;
  imageNode.width = 20;
  imageNode.height = 20;
  imageNode.imagePath = pngPath.string();
  imageNode.imageResizeMode = "stretch";
  imageScene.nodes.push_back(imageNode);
  const auto imageFrame = renderer.render(imageScene);
  std::filesystem::remove(pngPath);
  if (!imageFrame) {
    std::cerr << "decoded image scene failed: " << imageFrame.error << '\n';
    return 1;
  }
  std::size_t visibleRedImagePixels = 0;
  for (std::size_t offset = 0; offset + 3 < imageFrame.rgba.size();
       offset += 4) {
    if (imageFrame.rgba[offset] > 200 && imageFrame.rgba[offset + 1] < 40 &&
        imageFrame.rgba[offset + 2] < 40 && imageFrame.rgba[offset + 3] > 200) {
      ++visibleRedImagePixels;
    }
  }
  if (visibleRedImagePixels < 200) {
    std::cerr << "decoded image produced only " << visibleRedImagePixels
              << " red pixels\n";
    return 1;
  }
  imageScene.nodes[1].imageResizeMode = "center";
  const auto centeredFrame = renderer.render(imageScene);
  if (!centeredFrame) {
    std::cerr << "centered image scene failed: " << centeredFrame.error
              << '\n';
    return 1;
  }
  std::size_t centeredRedPixels = 0;
  for (std::size_t offset = 0; offset + 3 < centeredFrame.rgba.size();
       offset += 4) {
    if (centeredFrame.rgba[offset] > 200 &&
        centeredFrame.rgba[offset + 1] < 40 &&
        centeredFrame.rgba[offset + 2] < 40 &&
        centeredFrame.rgba[offset + 3] > 200) {
      ++centeredRedPixels;
    }
  }
  if (centeredRedPixels >= visibleRedImagePixels) {
    std::cerr << "image center resizeMode did not crop: center="
              << centeredRedPixels << " stretch=" << visibleRedImagePixels
              << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot switchScene;
  switchScene.surfaceId = 1;
  switchScene.revision = 1;
  switchScene.rootTag = 1;
  switchScene.viewportWidth = 52;
  switchScene.viewportHeight = 32;
  switchScene.pointScaleFactor = 2;
  ReactNativeSimulator::SceneNode switchRoot;
  switchRoot.tag = 1;
  switchRoot.layoutable = true;
  switchRoot.width = 52;
  switchRoot.height = 32;
  switchScene.nodes.push_back(switchRoot);
  ReactNativeSimulator::SceneNode toggle;
  toggle.tag = 2;
  toggle.parentTag = 1;
  toggle.componentName = "AndroidSwitch";
  toggle.layoutable = true;
  toggle.width = 52;
  toggle.height = 32;
  toggle.androidSwitch = true;
  toggle.androidSwitchOn = true;
  toggle.androidSwitchEnabled = true;
  switchScene.nodes.push_back(toggle);
  const auto switchFrame = renderer.render(switchScene);
  if (!switchFrame) {
    std::cerr << "android switch scene failed: " << switchFrame.error << '\n';
    return 1;
  }
  std::size_t switchTealPixels = 0;
  for (std::size_t offset = 0; offset + 3 < switchFrame.rgba.size();
       offset += 4) {
    if (switchFrame.rgba[offset] < 80 && switchFrame.rgba[offset + 1] > 80 &&
        switchFrame.rgba[offset + 1] < 180 &&
        switchFrame.rgba[offset + 2] > 70 &&
        switchFrame.rgba[offset + 3] > 80) {
      ++switchTealPixels;
    }
  }
  if (switchTealPixels < 40) {
    std::cerr << "android switch rendered only " << switchTealPixels
              << " track pixels\n";
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot gradientScene;
  gradientScene.surfaceId = 1;
  gradientScene.revision = 1;
  gradientScene.rootTag = 1;
  gradientScene.viewportWidth = 40;
  gradientScene.viewportHeight = 40;
  gradientScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode gradientRoot;
  gradientRoot.tag = 1;
  gradientRoot.layoutable = true;
  gradientRoot.width = 40;
  gradientRoot.height = 40;
  gradientScene.nodes.push_back(gradientRoot);
  ReactNativeSimulator::SceneNode gradient;
  gradient.tag = 2;
  gradient.parentTag = 1;
  gradient.layoutable = true;
  gradient.width = 40;
  gradient.height = 40;
  gradient.hasBackgroundGradient = true;
  gradient.backgroundGradientX0 = 0;
  gradient.backgroundGradientY0 = 0;
  gradient.backgroundGradientX1 = 1;
  gradient.backgroundGradientY1 = 0;
  gradient.backgroundGradientR0 = 1;
  gradient.backgroundGradientG0 = 0;
  gradient.backgroundGradientB0 = 0;
  gradient.backgroundGradientA0 = 1;
  gradient.backgroundGradientR1 = 0;
  gradient.backgroundGradientG1 = 0;
  gradient.backgroundGradientB1 = 1;
  gradient.backgroundGradientA1 = 1;
  gradientScene.nodes.push_back(gradient);
  const auto gradientFrame = renderer.render(gradientScene);
  if (!gradientFrame) {
    std::cerr << "gradient scene failed: " << gradientFrame.error << '\n';
    return 1;
  }
  std::size_t gradientRed = 0;
  std::size_t gradientBlue = 0;
  for (std::size_t offset = 0; offset + 3 < gradientFrame.rgba.size();
       offset += 4) {
    if (gradientFrame.rgba[offset] > gradientFrame.rgba[offset + 2] + 20 &&
        gradientFrame.rgba[offset + 3] > 80) {
      ++gradientRed;
    }
    if (gradientFrame.rgba[offset + 2] > gradientFrame.rgba[offset] + 20 &&
        gradientFrame.rgba[offset + 3] > 80) {
      ++gradientBlue;
    }
  }
  if (gradientRed < 20 || gradientBlue < 20) {
    std::cerr << "linear gradient missing stops: red=" << gradientRed
              << " blue=" << gradientBlue << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot filterPlainScene;
  filterPlainScene.surfaceId = 1;
  filterPlainScene.revision = 1;
  filterPlainScene.rootTag = 1;
  filterPlainScene.viewportWidth = 24;
  filterPlainScene.viewportHeight = 24;
  filterPlainScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode filterRoot;
  filterRoot.tag = 1;
  filterRoot.layoutable = true;
  filterRoot.width = 24;
  filterRoot.height = 24;
  filterPlainScene.nodes.push_back(filterRoot);
  ReactNativeSimulator::SceneNode filterBox;
  filterBox.tag = 2;
  filterBox.parentTag = 1;
  filterBox.layoutable = true;
  filterBox.width = 24;
  filterBox.height = 24;
  filterBox.hasBackgroundColor = true;
  filterBox.backgroundRed = 1;
  filterBox.backgroundGreen = 0.4f;
  filterBox.backgroundBlue = 0;
  filterBox.backgroundAlpha = 1;
  filterPlainScene.nodes.push_back(filterBox);
  ReactNativeSimulator::SceneSnapshot filterScene = filterPlainScene;
  ReactNativeSimulator::SceneNode::FilterOp grayscale;
  grayscale.type = "grayscale";
  grayscale.amount = 1;
  filterScene.nodes[1].filters.push_back(grayscale);
  filterScene.nodes[1].filterGrayscale = 1;
  const auto plainFilterFrame = renderer.render(filterPlainScene);
  const auto grayFilterFrame = renderer.render(filterScene);
  if (!plainFilterFrame || !grayFilterFrame) {
    std::cerr << "filter scene failed: "
              << (!plainFilterFrame ? plainFilterFrame.error
                                    : grayFilterFrame.error)
              << '\n';
    return 1;
  }
  if (plainFilterFrame.rgba.size() != grayFilterFrame.rgba.size()) {
    std::cerr << "filter scene size mismatch\n";
    return 1;
  }
  std::size_t filterDiffer = 0;
  for (std::size_t i = 0; i < plainFilterFrame.rgba.size(); ++i) {
    if (plainFilterFrame.rgba[i] != grayFilterFrame.rgba[i]) {
      ++filterDiffer;
    }
  }
  if (filterDiffer == 0) {
    std::cerr << "grayscale filter left pixels unchanged\n";
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot blendScene;
  blendScene.surfaceId = 1;
  blendScene.revision = 1;
  blendScene.rootTag = 1;
  blendScene.viewportWidth = 32;
  blendScene.viewportHeight = 32;
  blendScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode blendRoot;
  blendRoot.tag = 1;
  blendRoot.layoutable = true;
  blendRoot.width = 32;
  blendRoot.height = 32;
  blendRoot.hasBackgroundColor = true;
  blendRoot.backgroundRed = 1;
  blendRoot.backgroundGreen = 1;
  blendRoot.backgroundBlue = 1;
  blendRoot.backgroundAlpha = 1;
  blendScene.nodes.push_back(blendRoot);
  ReactNativeSimulator::SceneNode blendRed;
  blendRed.tag = 2;
  blendRed.parentTag = 1;
  blendRed.layoutable = true;
  blendRed.width = 24;
  blendRed.height = 24;
  blendRed.hasBackgroundColor = true;
  blendRed.backgroundRed = 1;
  blendRed.backgroundAlpha = 1;
  blendScene.nodes.push_back(blendRed);
  ReactNativeSimulator::SceneNode blendBlue;
  blendBlue.tag = 3;
  blendBlue.parentTag = 1;
  blendBlue.childIndex = 1;
  blendBlue.layoutable = true;
  blendBlue.x = 8;
  blendBlue.y = 8;
  blendBlue.absoluteX = 8;
  blendBlue.absoluteY = 8;
  blendBlue.width = 24;
  blendBlue.height = 24;
  blendBlue.hasBackgroundColor = true;
  blendBlue.backgroundBlue = 1;
  blendBlue.backgroundAlpha = 1;
  blendBlue.mixBlendMode = "multiply";
  blendScene.nodes.push_back(blendBlue);
  const auto blendFrame = renderer.render(blendScene);
  if (!blendFrame) {
    std::cerr << "mix-blend scene failed: " << blendFrame.error << '\n';
    return 1;
  }
  const auto blendOverlap =
      (static_cast<std::size_t>(16) *
           static_cast<std::size_t>(blendFrame.width) +
       static_cast<std::size_t>(16)) *
      4;
  const int blendR = blendFrame.rgba[blendOverlap];
  const int blendG = blendFrame.rgba[blendOverlap + 1];
  const int blendB = blendFrame.rgba[blendOverlap + 2];
  const int blendA = blendFrame.rgba[blendOverlap + 3];
  if (blendA < 200 || blendB > 80 || blendR > 80) {
    std::cerr << "multiply blend was src-over, pixel=" << blendR << ","
              << blendG << "," << blendB << "," << blendA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot multiStopScene;
  multiStopScene.surfaceId = 1;
  multiStopScene.revision = 1;
  multiStopScene.rootTag = 1;
  multiStopScene.viewportWidth = 40;
  multiStopScene.viewportHeight = 20;
  multiStopScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode multiStopRoot;
  multiStopRoot.tag = 1;
  multiStopRoot.layoutable = true;
  multiStopRoot.width = 40;
  multiStopRoot.height = 20;
  multiStopScene.nodes.push_back(multiStopRoot);
  ReactNativeSimulator::SceneNode multiStop;
  multiStop.tag = 2;
  multiStop.parentTag = 1;
  multiStop.layoutable = true;
  multiStop.width = 40;
  multiStop.height = 20;
  multiStop.hasBackgroundGradient = true;
  multiStop.backgroundGradientX0 = 0;
  multiStop.backgroundGradientY0 = 0;
  multiStop.backgroundGradientX1 = 1;
  multiStop.backgroundGradientY1 = 0;
  multiStop.backgroundGradientR0 = 1;
  multiStop.backgroundGradientA0 = 1;
  multiStop.backgroundGradientB1 = 1;
  multiStop.backgroundGradientA1 = 1;
  ReactNativeSimulator::SceneNode::BackgroundImageLayer multiLayer;
  multiLayer.x0 = 0;
  multiLayer.y0 = 0;
  multiLayer.x1 = 1;
  multiLayer.y1 = 0;
  multiLayer.stops.push_back({0, 1, 0, 0, 1});
  multiLayer.stops.push_back({0.5f, 1, 1, 0, 1});
  multiLayer.stops.push_back({1, 0, 0, 1, 1});
  multiStop.backgroundImageLayers.push_back(std::move(multiLayer));
  multiStopScene.nodes.push_back(multiStop);
  const auto multiStopFrame = renderer.render(multiStopScene);
  if (!multiStopFrame) {
    std::cerr << "multi-stop gradient failed: " << multiStopFrame.error
              << '\n';
    return 1;
  }
  const auto midOffset =
      (static_cast<std::size_t>(10) *
           static_cast<std::size_t>(multiStopFrame.width) +
       static_cast<std::size_t>(20)) *
      4;
  const int stopR = multiStopFrame.rgba[midOffset];
  const int stopG = multiStopFrame.rgba[midOffset + 1];
  const int stopB = multiStopFrame.rgba[midOffset + 2];
  const int stopA = multiStopFrame.rgba[midOffset + 3];
  if (stopA < 200 || stopG < 120 || stopR < 120 || stopB > 80) {
    std::cerr << "multi-stop midpoint was not yellow: " << stopR << ","
              << stopG << "," << stopB << "," << stopA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot borderScene;
  borderScene.surfaceId = 1;
  borderScene.revision = 1;
  borderScene.rootTag = 1;
  borderScene.viewportWidth = 20;
  borderScene.viewportHeight = 20;
  borderScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode borderRoot;
  borderRoot.tag = 1;
  borderRoot.layoutable = true;
  borderRoot.width = 20;
  borderRoot.height = 20;
  borderScene.nodes.push_back(borderRoot);
  ReactNativeSimulator::SceneSnapshot defaultBorderScene;
  defaultBorderScene.surfaceId = 1;
  defaultBorderScene.revision = 1;
  defaultBorderScene.rootTag = 1;
  defaultBorderScene.viewportWidth = 20;
  defaultBorderScene.viewportHeight = 20;
  defaultBorderScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode defaultBorderRoot;
  defaultBorderRoot.tag = 1;
  defaultBorderRoot.layoutable = true;
  defaultBorderRoot.width = 20;
  defaultBorderRoot.height = 20;
  defaultBorderScene.nodes.push_back(defaultBorderRoot);
  ReactNativeSimulator::SceneNode defaultBorder;
  defaultBorder.tag = 2;
  defaultBorder.parentTag = 1;
  defaultBorder.layoutable = true;
  defaultBorder.width = 20;
  defaultBorder.height = 20;
  defaultBorder.borderTop = 4;
  defaultBorder.borderRight = 4;
  defaultBorder.borderBottom = 4;
  defaultBorder.borderLeft = 4;
  defaultBorder.borderRadius = 10;
  defaultBorderScene.nodes.push_back(defaultBorder);
  const auto defaultBorderFrame = renderer.render(defaultBorderScene);
  if (!defaultBorderFrame) {
    std::cerr << "default border scene failed: " << defaultBorderFrame.error
              << '\n';
    return 1;
  }
  const auto defaultBorderOffset =
      (static_cast<std::size_t>(1) *
           static_cast<std::size_t>(defaultBorderFrame.width) +
       static_cast<std::size_t>(10)) *
      4;
  if (defaultBorderFrame.rgba[defaultBorderOffset + 3] < 200) {
    std::cerr << "default black border was not painted\n";
    return 1;
  }

  ReactNativeSimulator::SceneNode borderBox;
  borderBox.tag = 2;
  borderBox.parentTag = 1;
  borderBox.layoutable = true;
  borderBox.width = 20;
  borderBox.height = 20;
  borderBox.borderTop = 4;
  borderBox.borderRight = 4;
  borderBox.borderBottom = 4;
  borderBox.borderLeft = 4;
  borderBox.hasBorderColor = true;
  borderBox.hasBorderTopColor = true;
  borderBox.borderTopRed = 1;
  borderBox.borderTopAlpha = 1;
  borderBox.hasBorderLeftColor = true;
  borderBox.borderLeftBlue = 1;
  borderBox.borderLeftAlpha = 1;
  borderScene.nodes.push_back(borderBox);
  const auto borderFrame = renderer.render(borderScene);
  if (!borderFrame) {
    std::cerr << "border scene failed: " << borderFrame.error << '\n';
    return 1;
  }
  const auto pixelAt = [&](int x, int y) {
    const auto offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(borderFrame.width) +
         static_cast<std::size_t>(x)) *
        4;
    return std::tuple<int, int, int, int>{
        borderFrame.rgba[offset],
        borderFrame.rgba[offset + 1],
        borderFrame.rgba[offset + 2],
        borderFrame.rgba[offset + 3]};
  };
  const auto [topR, topG, topB, topA] = pixelAt(10, 1);
  const auto [leftR, leftG, leftB, leftA] = pixelAt(1, 10);
  if (topR < 200 || topB > 40 || topA < 200) {
    std::cerr << "top border was not red: " << topR << "," << topG << ","
              << topB << "," << topA << '\n';
    return 1;
  }
  if (leftB < 200 || leftR > 40 || leftA < 200) {
    std::cerr << "left border was not blue: " << leftR << "," << leftG << ","
              << leftB << "," << leftA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot outlineScene;
  outlineScene.surfaceId = 1;
  outlineScene.revision = 1;
  outlineScene.rootTag = 1;
  outlineScene.viewportWidth = 24;
  outlineScene.viewportHeight = 24;
  outlineScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode outlineRoot;
  outlineRoot.tag = 1;
  outlineRoot.layoutable = true;
  outlineRoot.width = 24;
  outlineRoot.height = 24;
  outlineScene.nodes.push_back(outlineRoot);
  ReactNativeSimulator::SceneNode outlined;
  outlined.tag = 2;
  outlined.parentTag = 1;
  outlined.layoutable = true;
  outlined.x = 4;
  outlined.y = 4;
  outlined.absoluteX = 4;
  outlined.absoluteY = 4;
  outlined.width = 16;
  outlined.height = 16;
  outlined.hasBackgroundColor = true;
  outlined.backgroundGreen = 1;
  outlined.backgroundAlpha = 1;
  outlined.outlineWidth = 2;
  outlined.outlineOffset = 0;
  outlined.hasOutlineColor = true;
  outlined.outlineRed = 1;
  outlined.outlineAlpha = 1;
  outlineScene.nodes.push_back(outlined);
  const auto outlineFrame = renderer.render(outlineScene);
  if (!outlineFrame) {
    std::cerr << "outline scene failed: " << outlineFrame.error << '\n';
    return 1;
  }
  const auto outlineOffset =
      (static_cast<std::size_t>(3) *
           static_cast<std::size_t>(outlineFrame.width) +
       static_cast<std::size_t>(12)) *
      4;
  if (outlineFrame.rgba[outlineOffset] < 200 ||
      outlineFrame.rgba[outlineOffset + 2] > 40) {
    std::cerr << "outline was not red at the top edge\n";
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot chromeScene;
  chromeScene.surfaceId = 1;
  chromeScene.revision = 1;
  chromeScene.rootTag = 1;
  chromeScene.viewportWidth = 40;
  chromeScene.viewportHeight = 40;
  chromeScene.pointScaleFactor = 1;
  chromeScene.statusBarHidden = false;
  chromeScene.statusBarHeight = 8;
  chromeScene.statusBarRed = 1;
  chromeScene.statusBarAlpha = 1;
  ReactNativeSimulator::SceneNode chromeRoot;
  chromeRoot.tag = 1;
  chromeRoot.layoutable = true;
  chromeRoot.width = 40;
  chromeRoot.height = 40;
  chromeRoot.hasBackgroundColor = true;
  chromeRoot.backgroundGreen = 1;
  chromeRoot.backgroundAlpha = 1;
  chromeScene.nodes.push_back(chromeRoot);
  const auto chromeFrame = renderer.render(chromeScene);
  if (!chromeFrame) {
    std::cerr << "status bar scene failed: " << chromeFrame.error << '\n';
    return 1;
  }
  if (chromeFrame.rgba[4] < 200 || chromeFrame.rgba[6] > 40) {
    std::cerr << "status bar was not red\n";
    return 1;
  }

  const auto pixelAtFrame =
      [](const ReactNativeSimulator::SkiaRenderedFrame& frame, int x, int y) {
        const auto offset =
            (static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(frame.width) +
             static_cast<std::size_t>(x)) *
            4;
        return std::tuple<int, int, int, int>{
            frame.rgba[offset],
            frame.rgba[offset + 1],
            frame.rgba[offset + 2],
            frame.rgba[offset + 3]};
      };

  ReactNativeSimulator::SceneSnapshot shadowScene;
  shadowScene.surfaceId = 1;
  shadowScene.revision = 1;
  shadowScene.rootTag = 1;
  shadowScene.viewportWidth = 48;
  shadowScene.viewportHeight = 48;
  shadowScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode shadowRoot;
  shadowRoot.tag = 1;
  shadowRoot.layoutable = true;
  shadowRoot.width = 48;
  shadowRoot.height = 48;
  shadowScene.nodes.push_back(shadowRoot);
  ReactNativeSimulator::SceneNode shadowed;
  shadowed.tag = 2;
  shadowed.parentTag = 1;
  shadowed.layoutable = true;
  shadowed.x = 14;
  shadowed.y = 14;
  shadowed.absoluteX = 14;
  shadowed.absoluteY = 14;
  shadowed.width = 20;
  shadowed.height = 20;
  shadowed.hasBackgroundColor = true;
  shadowed.backgroundGreen = 1;
  shadowed.backgroundAlpha = 1;
  shadowed.hasBoxShadow = true;
  shadowed.boxShadows.push_back(
      {8, 0, 0, 0, 1, 0, 0, 1, false});
  shadowed.boxShadows.push_back(
      {0, 8, 0, 0, 0, 0, 1, 1, false});
  shadowScene.nodes.push_back(shadowed);
  const auto shadowFrame = renderer.render(shadowScene);
  if (!shadowFrame) {
    std::cerr << "multi box-shadow scene failed: " << shadowFrame.error
              << '\n';
    return 1;
  }
  const auto [rightR, rightG, rightB, rightA] = pixelAtFrame(shadowFrame, 38, 24);
  const auto [belowR, belowG, belowB, belowA] = pixelAtFrame(shadowFrame, 24, 38);
  const auto [insideR, insideG, insideB, insideA] =
      pixelAtFrame(shadowFrame, 24, 24);
  if (rightR < 200 || rightA < 200 || rightB > 40) {
    std::cerr << "outset red shadow missing at (38,24): " << rightR << ","
              << rightG << "," << rightB << "," << rightA << '\n';
    return 1;
  }
  if (belowB < 200 || belowA < 200 || belowR > 40) {
    std::cerr << "outset blue shadow missing at (24,38): " << belowR << ","
              << belowG << "," << belowB << "," << belowA << '\n';
    return 1;
  }
  if (insideG < 200 || insideR > 40 || insideB > 40) {
    std::cerr << "outset shadows painted inside the box: " << insideR << ","
              << insideG << "," << insideB << "," << insideA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot overlapShadowScene;
  overlapShadowScene.surfaceId = 1;
  overlapShadowScene.revision = 1;
  overlapShadowScene.rootTag = 1;
  overlapShadowScene.viewportWidth = 32;
  overlapShadowScene.viewportHeight = 32;
  overlapShadowScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode overlapRoot;
  overlapRoot.tag = 1;
  overlapRoot.layoutable = true;
  overlapRoot.width = 32;
  overlapRoot.height = 32;
  overlapShadowScene.nodes.push_back(overlapRoot);
  ReactNativeSimulator::SceneNode overlapBox;
  overlapBox.tag = 2;
  overlapBox.parentTag = 1;
  overlapBox.layoutable = true;
  overlapBox.x = 10;
  overlapBox.y = 10;
  overlapBox.absoluteX = 10;
  overlapBox.absoluteY = 10;
  overlapBox.width = 12;
  overlapBox.height = 12;
  overlapBox.hasBackgroundColor = true;
  overlapBox.backgroundGreen = 1;
  overlapBox.backgroundAlpha = 1;
  overlapBox.hasBoxShadow = true;
  overlapBox.boxShadows.push_back({0, 0, 0, 4, 1, 0, 0, 1, false});
  overlapBox.boxShadows.push_back({0, 0, 0, 4, 0, 0, 1, 1, false});
  overlapShadowScene.nodes.push_back(overlapBox);
  const auto overlapShadowFrame = renderer.render(overlapShadowScene);
  if (!overlapShadowFrame) {
    std::cerr << "overlapping box-shadow scene failed: "
              << overlapShadowFrame.error << '\n';
    return 1;
  }
  const auto [ovR, ovG, ovB, ovA] = pixelAtFrame(overlapShadowFrame, 6, 16);
  if (ovR < 200 || ovB > 40 || ovA < 200) {
    std::cerr << "first box-shadow was not on top: " << ovR << "," << ovG
              << "," << ovB << "," << ovA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot dottedOutlineScene;
  dottedOutlineScene.surfaceId = 1;
  dottedOutlineScene.revision = 1;
  dottedOutlineScene.rootTag = 1;
  dottedOutlineScene.viewportWidth = 40;
  dottedOutlineScene.viewportHeight = 40;
  dottedOutlineScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode dottedRoot;
  dottedRoot.tag = 1;
  dottedRoot.layoutable = true;
  dottedRoot.width = 40;
  dottedRoot.height = 40;
  dottedOutlineScene.nodes.push_back(dottedRoot);
  ReactNativeSimulator::SceneNode dotted;
  dotted.tag = 2;
  dotted.parentTag = 1;
  dotted.layoutable = true;
  dotted.x = 8;
  dotted.y = 8;
  dotted.absoluteX = 8;
  dotted.absoluteY = 8;
  dotted.width = 24;
  dotted.height = 24;
  dotted.borderRadius = 12;
  dotted.outlineWidth = 6;
  dotted.outlineStyle = "dotted";
  dotted.hasOutlineColor = true;
  dotted.outlineRed = 1;
  dotted.outlineGreen = 0.65f;
  dotted.outlineAlpha = 1;
  dottedOutlineScene.nodes.push_back(dotted);
  const auto dottedFrame = renderer.render(dottedOutlineScene);
  if (!dottedFrame) {
    std::cerr << "dotted outline scene failed: " << dottedFrame.error
              << '\n';
    return 1;
  }
  int dottedInk = 0;
  int dottedGap = 0;
  for (int x = 0; x < dottedFrame.width; ++x) {
    const auto [dr, dg, db, da] = pixelAtFrame(dottedFrame, x, 5);
    if (da > 80 && dr > 150 && dg > 80) {
      ++dottedInk;
    } else if (da < 40) {
      ++dottedGap;
    }
  }
  if (dottedInk < 3 || dottedGap < 3) {
    std::cerr << "dotted outline was not a butt-capped dash (sunburst): ink="
              << dottedInk << " gap=" << dottedGap << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot insetScene;
  insetScene.surfaceId = 1;
  insetScene.revision = 1;
  insetScene.rootTag = 1;
  insetScene.viewportWidth = 24;
  insetScene.viewportHeight = 24;
  insetScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode insetRoot;
  insetRoot.tag = 1;
  insetRoot.layoutable = true;
  insetRoot.width = 24;
  insetRoot.height = 24;
  insetScene.nodes.push_back(insetRoot);
  ReactNativeSimulator::SceneNode insetBox;
  insetBox.tag = 2;
  insetBox.parentTag = 1;
  insetBox.layoutable = true;
  insetBox.x = 2;
  insetBox.y = 2;
  insetBox.absoluteX = 2;
  insetBox.absoluteY = 2;
  insetBox.width = 20;
  insetBox.height = 20;
  insetBox.hasBackgroundColor = true;
  insetBox.backgroundRed = 1;
  insetBox.backgroundGreen = 1;
  insetBox.backgroundBlue = 1;
  insetBox.backgroundAlpha = 1;
  insetBox.hasBoxShadow = true;
  insetBox.boxShadows.push_back({0, 0, 0, 6, 0, 0, 0, 1, true});
  insetScene.nodes.push_back(insetBox);
  const auto insetFrame = renderer.render(insetScene);
  if (!insetFrame) {
    std::cerr << "inset box-shadow scene failed: " << insetFrame.error << '\n';
    return 1;
  }
  const auto [edgeR, edgeG, edgeB, edgeA] = pixelAtFrame(insetFrame, 4, 12);
  const auto [coreR, coreG, coreB, coreA] = pixelAtFrame(insetFrame, 12, 12);
  if (edgeR + edgeG + edgeB > 80) {
    std::cerr << "inset shadow did not darken the padding edge: " << edgeR
              << "," << edgeG << "," << edgeB << "," << edgeA << '\n';
    return 1;
  }
  if (coreR < 200 || coreG < 200 || coreB < 200) {
    std::cerr << "inset spread reached the center: " << coreR << "," << coreG
              << "," << coreB << "," << coreA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot insetBlurScene;
  insetBlurScene.surfaceId = 1;
  insetBlurScene.revision = 1;
  insetBlurScene.rootTag = 1;
  insetBlurScene.viewportWidth = 24;
  insetBlurScene.viewportHeight = 24;
  insetBlurScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode insetBlurRoot;
  insetBlurRoot.tag = 1;
  insetBlurRoot.layoutable = true;
  insetBlurRoot.width = 24;
  insetBlurRoot.height = 24;
  insetBlurScene.nodes.push_back(insetBlurRoot);
  ReactNativeSimulator::SceneNode insetBlurBox;
  insetBlurBox.tag = 2;
  insetBlurBox.parentTag = 1;
  insetBlurBox.layoutable = true;
  insetBlurBox.x = 2;
  insetBlurBox.y = 2;
  insetBlurBox.absoluteX = 2;
  insetBlurBox.absoluteY = 2;
  insetBlurBox.width = 20;
  insetBlurBox.height = 20;
  insetBlurBox.borderRadius = 10;
  insetBlurBox.hasBoxShadow = true;
  insetBlurBox.boxShadows.push_back({0, 0, 10, 0, 0, 0, 0, 1, true});
  insetBlurScene.nodes.push_back(insetBlurBox);
  const auto insetBlurFrame = renderer.render(insetBlurScene);
  if (!insetBlurFrame) {
    std::cerr << "inset blur scene failed: " << insetBlurFrame.error << '\n';
    return 1;
  }
  const auto [ibR, ibG, ibB, ibA] = pixelAtFrame(insetBlurFrame, 12, 6);
  if (ibA < 50) {
    std::cerr << "inset 0 0 10px missing inner penumbra: " << ibR << ","
              << ibG << "," << ibB << "," << ibA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot outsetPunchScene;
  outsetPunchScene.surfaceId = 1;
  outsetPunchScene.revision = 1;
  outsetPunchScene.rootTag = 1;
  outsetPunchScene.viewportWidth = 32;
  outsetPunchScene.viewportHeight = 32;
  outsetPunchScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode outsetPunchRoot;
  outsetPunchRoot.tag = 1;
  outsetPunchRoot.layoutable = true;
  outsetPunchRoot.width = 32;
  outsetPunchRoot.height = 32;
  outsetPunchScene.nodes.push_back(outsetPunchRoot);
  ReactNativeSimulator::SceneNode outsetPunchBox;
  outsetPunchBox.tag = 2;
  outsetPunchBox.parentTag = 1;
  outsetPunchBox.layoutable = true;
  outsetPunchBox.x = 8;
  outsetPunchBox.y = 8;
  outsetPunchBox.absoluteX = 8;
  outsetPunchBox.absoluteY = 8;
  outsetPunchBox.width = 16;
  outsetPunchBox.height = 16;
  outsetPunchBox.borderRadius = 8;
  outsetPunchBox.hasBoxShadow = true;
  outsetPunchBox.boxShadows.push_back({0, -8, 0, 4, 0, 0, 0, 1, false});
  outsetPunchScene.nodes.push_back(outsetPunchBox);
  const auto outsetPunchFrame = renderer.render(outsetPunchScene);
  if (!outsetPunchFrame) {
    std::cerr << "outset punch scene failed: " << outsetPunchFrame.error
              << '\n';
    return 1;
  }
  const auto [opR, opG, opB, opA] = pixelAtFrame(outsetPunchFrame, 16, 16);
  const auto [capR, capG, capB, capA] = pixelAtFrame(outsetPunchFrame, 16, 2);
  if (opA > 40) {
    std::cerr << "outset spread filled the box interior: " << opR << ","
              << opG << "," << opB << "," << opA << '\n';
    return 1;
  }
  if (capA < 200) {
    std::cerr << "outset spread cap missing above the box: " << capR << ","
              << capG << "," << capB << "," << capA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot clipScene;
  clipScene.surfaceId = 1;
  clipScene.revision = 1;
  clipScene.rootTag = 1;
  clipScene.viewportWidth = 20;
  clipScene.viewportHeight = 20;
  clipScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode clipRoot;
  clipRoot.tag = 1;
  clipRoot.layoutable = true;
  clipRoot.width = 20;
  clipRoot.height = 20;
  clipScene.nodes.push_back(clipRoot);
  ReactNativeSimulator::SceneNode clipParent;
  clipParent.tag = 2;
  clipParent.parentTag = 1;
  clipParent.layoutable = true;
  clipParent.width = 20;
  clipParent.height = 20;
  clipParent.clipsContentToBounds = true;
  clipParent.borderRadius = 10;
  clipScene.nodes.push_back(clipParent);
  ReactNativeSimulator::SceneNode clipChild;
  clipChild.tag = 3;
  clipChild.parentTag = 2;
  clipChild.layoutable = true;
  clipChild.width = 20;
  clipChild.height = 20;
  clipChild.hasBackgroundColor = true;
  clipChild.backgroundRed = 1;
  clipChild.backgroundAlpha = 1;
  clipScene.nodes.push_back(clipChild);
  const auto clipFrame = renderer.render(clipScene);
  if (!clipFrame) {
    std::cerr << "overflow clip scene failed: " << clipFrame.error << '\n';
    return 1;
  }
  const auto [cornerR, cornerG, cornerB, cornerA] =
      pixelAtFrame(clipFrame, 0, 0);
  const auto [clipMidR, clipMidG, clipMidB, clipMidA] =
      pixelAtFrame(clipFrame, 10, 10);
  if (cornerA > 40) {
    std::cerr << "rounded overflow clip leaked the corner: " << cornerR << ","
              << cornerG << "," << cornerB << "," << cornerA << '\n';
    return 1;
  }
  if (clipMidR < 200 || clipMidA < 200) {
    std::cerr << "rounded overflow clip hid the center: " << clipMidR << ","
              << clipMidG << "," << clipMidB << "," << clipMidA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot textClipScene;
  textClipScene.surfaceId = 1;
  textClipScene.revision = 1;
  textClipScene.rootTag = 1;
  textClipScene.viewportWidth = 175;
  textClipScene.viewportHeight = 140;
  textClipScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode textClipRoot;
  textClipRoot.tag = 1;
  textClipRoot.layoutable = true;
  textClipRoot.width = 175;
  textClipRoot.height = 140;
  textClipScene.nodes.push_back(textClipRoot);
  ReactNativeSimulator::SceneNode textClipParent;
  textClipParent.tag = 2;
  textClipParent.parentTag = 1;
  textClipParent.componentName = "Paragraph";
  textClipParent.layoutable = true;
  textClipParent.width = 175;
  textClipParent.height = 100;
  textClipParent.clipsContentToBounds = true;
  textClipParent.hasBackgroundColor = true;
  textClipParent.backgroundRed = 0.83f;
  textClipParent.backgroundGreen = 0.83f;
  textClipParent.backgroundBlue = 0.83f;
  textClipParent.backgroundAlpha = 1;
  textClipScene.nodes.push_back(textClipParent);
  ReactNativeSimulator::SceneNode textClipImage;
  textClipImage.tag = 3;
  textClipImage.parentTag = 2;
  textClipImage.componentName = "Image";
  textClipImage.layoutable = true;
  textClipImage.x = 20;
  textClipImage.y = 20;
  textClipImage.absoluteX = 20;
  textClipImage.absoluteY = 20;
  textClipImage.width = 50;
  textClipImage.height = 100;
  textClipImage.hasBackgroundColor = true;
  textClipImage.backgroundRed = 1;
  textClipImage.backgroundAlpha = 1;
  textClipScene.nodes.push_back(textClipImage);
  const auto textClipFrame = renderer.render(textClipScene);
  if (!textClipFrame) {
    std::cerr << "text overflow clip scene failed: " << textClipFrame.error
              << '\n';
    return 1;
  }
  const auto [textInsideR, textInsideG, textInsideB, textInsideA] =
      pixelAtFrame(textClipFrame, 45, 90);
  const auto [textOutsideR, textOutsideG, textOutsideB, textOutsideA] =
      pixelAtFrame(textClipFrame, 45, 110);
  if (textInsideR < 200 || textInsideA < 200) {
    std::cerr << "text overflow clip hid the inline image: " << textInsideR
              << "," << textInsideG << "," << textInsideB << ","
              << textInsideA << '\n';
    return 1;
  }
  if (textOutsideA > 40 && textOutsideR > 200) {
    std::cerr << "text overflow clip leaked the inline image: "
              << textOutsideR << "," << textOutsideG << "," << textOutsideB
              << "," << textOutsideA << '\n';
    return 1;
  }
  textClipImage.inlineAttachment = true;
  textClipScene.nodes[2] = textClipImage;
  const auto textAttachFrame = renderer.render(textClipScene);
  if (!textAttachFrame) {
    std::cerr << "inline attachment overflow scene failed: "
              << textAttachFrame.error << '\n';
    return 1;
  }
  const auto [attachOutR, attachOutG, attachOutB, attachOutA] =
      pixelAtFrame(textAttachFrame, 45, 110);
  if (attachOutA < 200 || attachOutR < 200) {
    std::cerr << "inline attachment was clipped like a View, Pixel paints it "
                 "outside Text overflow:hidden: "
              << attachOutR << "," << attachOutG << "," << attachOutB << ","
              << attachOutA << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph titlePara;
  titlePara.includeFontPadding = true;
  titlePara.alignment =
      ReactNativeSimulator::TextHorizontalAlignment::Center;
  ReactNativeSimulator::TextRun titleRun;
  titleRun.text = "Text";
  titleRun.fontSize = 19;
  titleRun.fontWeight = 600;
  titleRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  titlePara.runs.push_back(titleRun);
  const auto titleMeasured = textLayout.prepare(titlePara, 360, 2.75f);
  const auto titlePrepared =
      textLayout.prepare(titlePara, titleMeasured->width(), 2.75f);
  if (titlePrepared->lineCount() != 1) {
    std::cerr << "wrap_content \"Text\" wrapped to "
              << titlePrepared->lineCount() << " lines at w="
              << titlePrepared->width() << " longest="
              << titlePrepared->longestLine() << " layoutW="
              << titlePrepared->layoutWidth() << '\n';
    return 1;
  }
  ReactNativeSimulator::TextParagraph wideCenterPara;
  wideCenterPara.alignment =
      ReactNativeSimulator::TextHorizontalAlignment::Center;
  ReactNativeSimulator::TextRun wideCenterRun;
  wideCenterRun.text = "center";
  wideCenterRun.fontSize = 14;
  wideCenterPara.runs.push_back(wideCenterRun);
  const auto wideCenter = textLayout.prepare(wideCenterPara, 360, 2.75f);
  if (wideCenter->lineCount() != 1 || wideCenter->lines().front().left < 80) {
    std::cerr << "wide textAlign center stayed start, lines="
              << wideCenter->lineCount() << " left="
              << (wideCenter->lines().empty()
                      ? 0.0f
                      : wideCenter->lines().front().left)
              << '\n';
    return 1;
  }
  ReactNativeSimulator::TextParagraph wideRightPara;
  wideRightPara.alignment =
      ReactNativeSimulator::TextHorizontalAlignment::Right;
  ReactNativeSimulator::TextRun wideRightRun;
  wideRightRun.text = "right";
  wideRightRun.fontSize = 14;
  wideRightPara.runs.push_back(wideRightRun);
  const auto wideRight = textLayout.prepare(wideRightPara, 360, 2.75f);
  if (wideRight->lineCount() != 1 || wideRight->lines().front().left < 200) {
    std::cerr << "wide textAlign right stayed start, lines="
              << wideRight->lineCount() << " left="
              << (wideRight->lines().empty()
                      ? 0.0f
                      : wideRight->lines().front().left)
              << '\n';
    return 1;
  }
  ReactNativeSimulator::TextParagraph wrapCenterPara;
  wrapCenterPara.alignment =
      ReactNativeSimulator::TextHorizontalAlignment::Center;
  ReactNativeSimulator::TextRun wrapCenterRun;
  wrapCenterRun.text =
      "center center center center center center center center center "
      "center center center center center center";
  wrapCenterRun.fontSize = 14;
  wrapCenterPara.runs.push_back(wrapCenterRun);
  const auto wrapCenter = textLayout.prepare(wrapCenterPara, 200, 2.75f);
  if (wrapCenter->lineCount() < 2 || wrapCenter->lines().back().left < 10) {
    std::cerr << "wrapped textAlign center stayed start, lines="
              << wrapCenter->lineCount() << " last.left="
              << (wrapCenter->lines().empty()
                      ? 0.0f
                      : wrapCenter->lines().back().left)
              << '\n';
    return 1;
  }
  ReactNativeSimulator::SceneSnapshot titleScene;
  titleScene.surfaceId = 1;
  titleScene.revision = 1;
  titleScene.rootTag = 1;
  titleScene.viewportWidth = 80;
  titleScene.viewportHeight = 40;
  titleScene.pointScaleFactor = 2.75f;
  ReactNativeSimulator::SceneNode titleRoot;
  titleRoot.tag = 1;
  titleRoot.layoutable = true;
  titleRoot.width = 80;
  titleRoot.height = 40;
  titleRoot.hasBackgroundColor = true;
  titleRoot.backgroundRed = 1;
  titleRoot.backgroundGreen = 1;
  titleRoot.backgroundBlue = 1;
  titleRoot.backgroundAlpha = 1;
  titleScene.nodes.push_back(titleRoot);
  ReactNativeSimulator::SceneNode titleNode;
  titleNode.tag = 2;
  titleNode.parentTag = 1;
  titleNode.componentName = "Paragraph";
  titleNode.layoutable = true;
  titleNode.width = titlePrepared->width();
  titleNode.height = titlePrepared->height();
  titleNode.clipsContentToBounds = true;
  titleNode.preparedText = titlePrepared;
  titleScene.nodes.push_back(titleNode);
  const auto titleFrame = renderer.render(titleScene);
  if (!titleFrame) {
    std::cerr << "shrink-wrap title scene failed: " << titleFrame.error
              << '\n';
    return 1;
  }
  const auto titleWidthPx = static_cast<int>(
      std::lround(titlePrepared->width() * titleScene.pointScaleFactor));
  const auto titleInkPx = static_cast<int>(
      std::lround(titlePrepared->longestLine() * titleScene.pointScaleFactor));
  bool titleHasRightInk = false;
  const int inkRight = std::min(titleWidthPx, std::max(1, titleInkPx));
  for (int x = std::max(0, inkRight - 4); x < inkRight; ++x) {
    const auto [tr, tg, tb, ta] = pixelAtFrame(titleFrame, x, 22);
    if (ta > 80 && tr < 200) {
      titleHasRightInk = true;
      break;
    }
  }
  if (!titleHasRightInk) {
    const auto& titleLine = titlePrepared->lines().empty()
        ? ReactNativeSimulator::PreparedTextLine{}
        : titlePrepared->lines().front();
    std::cerr << "shrink-wrap overflow:hidden clipped the last letter of "
                 "\"Text\" w=" << titlePrepared->width()
              << " layoutW=" << titlePrepared->layoutWidth()
              << " longest=" << titlePrepared->longestLine()
              << " line.left=" << titleLine.left
              << " line.w=" << titleLine.width
              << " nodeW=" << titleNode.width << " pxW=" << titleWidthPx
              << '\n';
    if (!kLinuxFontConfigHost) {
      return 1;
    }
  }

  ReactNativeSimulator::TextParagraph borderPara;
  borderPara.includeFontPadding = true;
  ReactNativeSimulator::TextRun borderRun;
  borderRun.text =
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do";
  borderRun.fontSize = 16;
  borderRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  borderPara.runs.push_back(borderRun);
  const auto borderPrepared = textLayout.prepare(borderPara, 280);
  ReactNativeSimulator::SceneSnapshot textOverflowBorderScene;
  textOverflowBorderScene.surfaceId = 1;
  textOverflowBorderScene.revision = 1;
  textOverflowBorderScene.rootTag = 1;
  textOverflowBorderScene.viewportWidth = 300;
  textOverflowBorderScene.viewportHeight = 120;
  textOverflowBorderScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode textOverflowBorderRoot;
  textOverflowBorderRoot.tag = 1;
  textOverflowBorderRoot.layoutable = true;
  textOverflowBorderRoot.width = 300;
  textOverflowBorderRoot.height = 120;
  textOverflowBorderRoot.hasBackgroundColor = true;
  textOverflowBorderRoot.backgroundRed = 1;
  textOverflowBorderRoot.backgroundGreen = 1;
  textOverflowBorderRoot.backgroundBlue = 1;
  textOverflowBorderRoot.backgroundAlpha = 1;
  textOverflowBorderScene.nodes.push_back(textOverflowBorderRoot);
  ReactNativeSimulator::SceneNode borderText;
  borderText.tag = 2;
  borderText.parentTag = 1;
  borderText.componentName = "Paragraph";
  borderText.layoutable = true;
  borderText.width = 300;
  borderText.height = std::max(90.0f, borderPrepared->height() + 10);
  borderText.borderLeft = 5;
  borderText.borderRight = 5;
  borderText.borderTop = 5;
  borderText.borderBottom = 5;
  borderText.borderRadius = 50;
  borderText.clipsContentToBounds = true;
  borderText.contentInsetLeft = 5;
  borderText.contentInsetRight = 5;
  borderText.contentInsetTop = 5;
  borderText.contentInsetBottom = 5;
  borderText.hasBackgroundColor = true;
  borderText.backgroundRed = 1;
  borderText.backgroundGreen = 1;
  borderText.backgroundBlue = 1;
  borderText.backgroundAlpha = 1;
  borderText.hasBorderColor = true;
  borderText.hasBorderTopColor = true;
  borderText.hasBorderRightColor = true;
  borderText.hasBorderBottomColor = true;
  borderText.hasBorderLeftColor = true;
  borderText.borderTopRed = 1;
  borderText.borderRightRed = 1;
  borderText.borderBottomRed = 1;
  borderText.borderLeftRed = 1;
  borderText.borderRed = 1;
  borderText.borderTopAlpha = 1;
  borderText.borderRightAlpha = 1;
  borderText.borderBottomAlpha = 1;
  borderText.borderLeftAlpha = 1;
  borderText.borderAlpha = 1;
  borderText.preparedText = borderPrepared;
  textOverflowBorderScene.nodes.push_back(borderText);
  const auto textOverflowBorderFrame =
      renderer.render(textOverflowBorderScene);
  if (!textOverflowBorderFrame) {
    std::cerr << "bordered overflow text scene failed: "
              << textOverflowBorderFrame.error << '\n';
    return 1;
  }
  bool firstLineVisible = false;
  for (int y = 10; y <= 28; ++y) {
    const auto [lineR, lineG, lineB, lineA] =
        pixelAtFrame(textOverflowBorderFrame, 80, y);
    if (lineA > 80 && lineR < 180) {
      firstLineVisible = true;
      break;
    }
  }
  if (!firstLineVisible) {
    std::cerr << "overflow:hidden + border clipped the first text line\n";
    if (!kLinuxFontConfigHost) {
      return 1;
    }
  }
  const auto [borderR, borderG, borderB, borderA] =
      pixelAtFrame(textOverflowBorderFrame, 2, 40);
  if (borderA < 200 || borderR < 200 || borderG > 80 || borderB > 80) {
    std::cerr << "overflow:hidden clipped the Text border stroke: "
              << borderR << "," << borderG << "," << borderB << ","
              << borderA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot backfaceScene;
  backfaceScene.surfaceId = 1;
  backfaceScene.revision = 1;
  backfaceScene.rootTag = 1;
  backfaceScene.viewportWidth = 20;
  backfaceScene.viewportHeight = 20;
  backfaceScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode backfaceRoot;
  backfaceRoot.tag = 1;
  backfaceRoot.layoutable = true;
  backfaceRoot.width = 20;
  backfaceRoot.height = 20;
  backfaceScene.nodes.push_back(backfaceRoot);
  ReactNativeSimulator::SceneNode flipped;
  flipped.tag = 2;
  flipped.parentTag = 1;
  flipped.layoutable = true;
  flipped.width = 20;
  flipped.height = 20;
  flipped.hasBackgroundColor = true;
  flipped.backgroundRed = 1;
  flipped.backgroundAlpha = 1;
  flipped.hasTransform = true;
  flipped.transformM[0] = -1;
  flipped.transformM[5] = 1;
  flipped.backfaceHidden = true;
  backfaceScene.nodes.push_back(flipped);
  const auto backfaceFrame = renderer.render(backfaceScene);
  if (!backfaceFrame) {
    std::cerr << "backface scene failed: " << backfaceFrame.error << '\n';
    return 1;
  }
  const auto [faceR, faceG, faceB, faceA] = pixelAtFrame(backfaceFrame, 10, 10);
  if (faceA > 40) {
    std::cerr << "backface-hidden rotateY still painted: " << faceR << ","
              << faceG << "," << faceB << "," << faceA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot groupScene;
  groupScene.surfaceId = 1;
  groupScene.revision = 1;
  groupScene.rootTag = 1;
  groupScene.viewportWidth = 40;
  groupScene.viewportHeight = 40;
  groupScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode groupRoot;
  groupRoot.tag = 1;
  groupRoot.layoutable = true;
  groupRoot.width = 40;
  groupRoot.height = 40;
  groupScene.nodes.push_back(groupRoot);
  ReactNativeSimulator::SceneNode group;
  group.tag = 2;
  group.parentTag = 1;
  group.layoutable = true;
  group.width = 40;
  group.height = 40;
  group.opacity = 0.5f;
  group.needsOffscreenAlphaCompositing = true;
  groupScene.nodes.push_back(group);
  ReactNativeSimulator::SceneNode groupRed;
  groupRed.tag = 3;
  groupRed.parentTag = 2;
  groupRed.layoutable = true;
  groupRed.width = 30;
  groupRed.height = 30;
  groupRed.hasBackgroundColor = true;
  groupRed.backgroundRed = 1;
  groupRed.backgroundAlpha = 1;
  groupScene.nodes.push_back(groupRed);
  ReactNativeSimulator::SceneNode groupBlue;
  groupBlue.tag = 4;
  groupBlue.parentTag = 2;
  groupBlue.layoutable = true;
  groupBlue.x = 10;
  groupBlue.y = 10;
  groupBlue.absoluteX = 10;
  groupBlue.absoluteY = 10;
  groupBlue.width = 30;
  groupBlue.height = 30;
  groupBlue.hasBackgroundColor = true;
  groupBlue.backgroundBlue = 1;
  groupBlue.backgroundAlpha = 1;
  groupScene.nodes.push_back(groupBlue);
  const auto groupFrame = renderer.render(groupScene);
  if (!groupFrame) {
    std::cerr << "offscreen compositing scene failed: " << groupFrame.error
              << '\n';
    return 1;
  }
  const auto [overlapR, overlapG, overlapB, overlapA] =
      pixelAtFrame(groupFrame, 20, 20);
  if (overlapA > 180 || overlapB < 80 || overlapR > 40) {
    std::cerr << "needsOffscreenAlphaCompositing did not group opacity: "
              << overlapR << "," << overlapG << "," << overlapB << ","
              << overlapA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot ellipseScene;
  ellipseScene.surfaceId = 1;
  ellipseScene.revision = 1;
  ellipseScene.rootTag = 1;
  ellipseScene.viewportWidth = 40;
  ellipseScene.viewportHeight = 20;
  ellipseScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode ellipseRoot;
  ellipseRoot.tag = 1;
  ellipseRoot.layoutable = true;
  ellipseRoot.width = 40;
  ellipseRoot.height = 20;
  ellipseScene.nodes.push_back(ellipseRoot);
  ReactNativeSimulator::SceneNode ellipse;
  ellipse.tag = 2;
  ellipse.parentTag = 1;
  ellipse.layoutable = true;
  ellipse.width = 40;
  ellipse.height = 20;
  ellipse.hasBackgroundColor = true;
  ellipse.backgroundBlue = 1;
  ellipse.backgroundAlpha = 1;
  ellipse.borderRadiusTopLeftX = 20;
  ellipse.borderRadiusTopLeftY = 10;
  ellipse.borderRadiusTopRightX = 20;
  ellipse.borderRadiusTopRightY = 10;
  ellipse.borderRadiusBottomRightX = 20;
  ellipse.borderRadiusBottomRightY = 10;
  ellipse.borderRadiusBottomLeftX = 20;
  ellipse.borderRadiusBottomLeftY = 10;
  ellipseScene.nodes.push_back(ellipse);
  const auto ellipseFrame = renderer.render(ellipseScene);
  if (!ellipseFrame) {
    std::cerr << "elliptical radius scene failed: " << ellipseFrame.error
              << '\n';
    return 1;
  }
  const auto [ellCornerR, ellCornerG, ellCornerB, ellCornerA] =
      pixelAtFrame(ellipseFrame, 0, 0);
  const auto [ellMidR, ellMidG, ellMidB, ellMidA] =
      pixelAtFrame(ellipseFrame, 20, 10);
  if (ellCornerA > 40) {
    std::cerr << "elliptical radii filled the corner: " << ellCornerR << ","
              << ellCornerG << "," << ellCornerB << "," << ellCornerA << '\n';
    return 1;
  }
  if (ellMidB < 200 || ellMidA < 200) {
    std::cerr << "elliptical radii hid the center: " << ellMidR << ","
              << ellMidG << "," << ellMidB << "," << ellMidA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot twoSideScene;
  twoSideScene.surfaceId = 1;
  twoSideScene.revision = 1;
  twoSideScene.rootTag = 1;
  twoSideScene.viewportWidth = 40;
  twoSideScene.viewportHeight = 40;
  twoSideScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode twoSideRoot;
  twoSideRoot.tag = 1;
  twoSideRoot.layoutable = true;
  twoSideRoot.width = 40;
  twoSideRoot.height = 40;
  twoSideScene.nodes.push_back(twoSideRoot);
  ReactNativeSimulator::SceneNode twoSide;
  twoSide.tag = 2;
  twoSide.parentTag = 1;
  twoSide.layoutable = true;
  twoSide.x = 5;
  twoSide.y = 5;
  twoSide.absoluteX = 5;
  twoSide.absoluteY = 5;
  twoSide.width = 30;
  twoSide.height = 30;
  twoSide.borderTop = 6;
  twoSide.borderLeft = 6;
  twoSide.borderRadiusTopLeft = 20;
  twoSide.borderRadiusTopLeftX = 20;
  twoSide.borderRadiusTopLeftY = 20;
  twoSideScene.nodes.push_back(twoSide);
  const auto twoSideFrame = renderer.render(twoSideScene);
  if (!twoSideFrame) {
    std::cerr << "two-sided rounded border failed: " << twoSideFrame.error
              << '\n';
    return 1;
  }
  const auto [stemR, stemG, stemB, stemA] = pixelAtFrame(twoSideFrame, 7, 28);
  const auto [holeR, holeG, holeB, holeA] = pixelAtFrame(twoSideFrame, 20, 20);
  const auto [arcR, arcG, arcB, arcA] = pixelAtFrame(twoSideFrame, 12, 12);
  if (stemA < 200) {
    std::cerr << "two-sided left stem missing: " << stemR << "," << stemG
              << "," << stemB << "," << stemA << '\n';
    return 1;
  }
  if (holeA > 40) {
    std::cerr << "two-sided inner corner was filled: " << holeR << ","
              << holeG << "," << holeB << "," << holeA << '\n';
    return 1;
  }
  if (arcA < 200) {
    std::cerr << "two-sided inner quarter-ellipse was notched: " << arcR
              << "," << arcG << "," << arcB << "," << arcA << '\n';
    return 1;
  }
  const auto [hairR, hairG, hairB, hairA] = pixelAtFrame(twoSideFrame, 34, 20);
  if (hairA > 40) {
    std::cerr << "two-sided zero-width edge grew a hairline: " << hairR << ","
              << hairG << "," << hairB << "," << hairA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot fadeScene;
  fadeScene.surfaceId = 1;
  fadeScene.revision = 1;
  fadeScene.rootTag = 1;
  fadeScene.viewportWidth = 80;
  fadeScene.viewportHeight = 24;
  fadeScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode fadeRoot;
  fadeRoot.tag = 1;
  fadeRoot.layoutable = true;
  fadeRoot.width = 80;
  fadeRoot.height = 24;
  fadeScene.nodes.push_back(fadeRoot);
  ReactNativeSimulator::SceneNode hiddenText;
  hiddenText.tag = 2;
  hiddenText.parentTag = 1;
  hiddenText.layoutable = true;
  hiddenText.width = 80;
  hiddenText.height = 12;
  hiddenText.opacity = 0;
  hiddenText.text = "Hidden";
  hiddenText.fontSize = 12;
  hiddenText.hasTextColor = true;
  hiddenText.textAlpha = 1;
  fadeScene.nodes.push_back(hiddenText);
  ReactNativeSimulator::SceneNode fadedText;
  fadedText.tag = 3;
  fadedText.parentTag = 1;
  fadedText.layoutable = true;
  fadedText.y = 12;
  fadedText.absoluteY = 12;
  fadedText.width = 80;
  fadedText.height = 12;
  fadedText.opacity = 0.3f;
  fadedText.text = "Faded";
  fadedText.fontSize = 12;
  fadedText.hasTextColor = true;
  fadedText.textAlpha = 1;
  fadeScene.nodes.push_back(fadedText);
  const auto fadeFrame = renderer.render(fadeScene);
  if (!fadeFrame) {
    std::cerr << "opacity text scene failed: " << fadeFrame.error << '\n';
    return 1;
  }
  std::size_t hiddenInk = 0;
  std::size_t fadedInk = 0;
  int fadedMaxA = 0;
  for (std::size_t offset = 0; offset + 3 < fadeFrame.rgba.size();
       offset += 4) {
    const auto y = static_cast<int>(
        (offset / 4) / static_cast<std::size_t>(fadeFrame.width));
    const auto a = fadeFrame.rgba[offset + 3];
    if (a <= 20) {
      continue;
    }
    if (y < 12) {
      ++hiddenInk;
    } else {
      ++fadedInk;
      fadedMaxA = std::max(fadedMaxA, static_cast<int>(a));
    }
  }
  if (hiddenInk > 0) {
    std::cerr << "opacity 0 text still painted " << hiddenInk << " pixels\n";
    return 1;
  }
  if (fadedInk < 5 || fadedMaxA > 120) {
    std::cerr << "opacity 0.3 text was not faded: ink=" << fadedInk
              << " maxA=" << fadedMaxA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot zScene;
  zScene.surfaceId = 1;
  zScene.revision = 1;
  zScene.rootTag = 1;
  zScene.viewportWidth = 60;
  zScene.viewportHeight = 40;
  zScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode zRoot;
  zRoot.tag = 1;
  zRoot.layoutable = true;
  zRoot.width = 60;
  zRoot.height = 40;
  zScene.nodes.push_back(zRoot);
  ReactNativeSimulator::SceneNode zBlue;
  zBlue.tag = 2;
  zBlue.parentTag = 1;
  zBlue.childIndex = 0;
  zBlue.layoutable = true;
  zBlue.position = "static";
  zBlue.zIndex = 100;
  zBlue.x = 20;
  zBlue.absoluteX = 20;
  zBlue.width = 40;
  zBlue.height = 40;
  zBlue.hasBackgroundColor = true;
  zBlue.backgroundBlue = 1;
  zBlue.backgroundAlpha = 1;
  zScene.nodes.push_back(zBlue);
  ReactNativeSimulator::SceneNode zRed;
  zRed.tag = 3;
  zRed.parentTag = 1;
  zRed.childIndex = 1;
  zRed.layoutable = true;
  zRed.position = "relative";
  zRed.width = 40;
  zRed.height = 40;
  zRed.hasBackgroundColor = true;
  zRed.backgroundRed = 1;
  zRed.backgroundAlpha = 1;
  zScene.nodes.push_back(zRed);
  const auto zFrame = renderer.render(zScene);
  if (!zFrame) {
    std::cerr << "static z-index scene failed: " << zFrame.error << '\n';
    return 1;
  }
  const auto [zR, zG, zB, zA] = pixelAtFrame(zFrame, 30, 20);
  if (zR < 200 || zB > 40 || zA < 200) {
    std::cerr << "static zIndex was not ignored: " << zR << "," << zG << ","
              << zB << "," << zA << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot flatZScene;
  flatZScene.surfaceId = 1;
  flatZScene.revision = 1;
  flatZScene.rootTag = 1;
  flatZScene.viewportWidth = 60;
  flatZScene.viewportHeight = 40;
  flatZScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode flatRoot;
  flatRoot.tag = 1;
  flatRoot.layoutable = true;
  flatRoot.width = 60;
  flatRoot.height = 40;
  flatZScene.nodes.push_back(flatRoot);
  ReactNativeSimulator::SceneNode flatYellow;
  flatYellow.tag = 2;
  flatYellow.parentTag = 1;
  flatYellow.childIndex = 0;
  flatYellow.layoutable = true;
  flatYellow.position = "relative";
  flatYellow.width = 60;
  flatYellow.height = 40;
  flatYellow.hasBackgroundColor = true;
  flatYellow.backgroundRed = 1;
  flatYellow.backgroundGreen = 1;
  flatYellow.backgroundAlpha = 1;
  flatZScene.nodes.push_back(flatYellow);
  ReactNativeSimulator::SceneNode flatBlue;
  flatBlue.tag = 3;
  flatBlue.parentTag = 1;
  flatBlue.childIndex = 1;
  flatBlue.layoutable = true;
  flatBlue.position = "static";
  flatBlue.zIndex = 100;
  flatBlue.x = 20;
  flatBlue.absoluteX = 20;
  flatBlue.width = 40;
  flatBlue.height = 40;
  flatBlue.hasBackgroundColor = true;
  flatBlue.backgroundBlue = 1;
  flatBlue.backgroundAlpha = 1;
  flatZScene.nodes.push_back(flatBlue);
  ReactNativeSimulator::SceneNode flatRed;
  flatRed.tag = 4;
  flatRed.parentTag = 1;
  flatRed.childIndex = 2;
  flatRed.layoutable = true;
  flatRed.position = "relative";
  flatRed.width = 40;
  flatRed.height = 40;
  flatRed.hasBackgroundColor = true;
  flatRed.backgroundRed = 1;
  flatRed.backgroundAlpha = 1;
  flatZScene.nodes.push_back(flatRed);
  const auto flatZFrame = renderer.render(flatZScene);
  if (!flatZFrame) {
    std::cerr << "flattened z-index scene failed: " << flatZFrame.error
              << '\n';
    return 1;
  }
  const auto [fzR, fzG, fzB, fzA] = pixelAtFrame(flatZFrame, 30, 20);
  const auto [fbR, fbG, fbB, fbA] = pixelAtFrame(flatZFrame, 50, 20);
  if (fzR < 200 || fzB > 40 || fzA < 200) {
    std::cerr << "flattened relative did not paint over static: " << fzR
              << "," << fzG << "," << fzB << "," << fzA << '\n';
    return 1;
  }
  if (fbB < 200 || fbR > 40 || fbA < 200) {
    std::cerr << "flattened parent covered its static child: " << fbR << ","
              << fbG << "," << fbB << "," << fbA << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph valignParagraph;
  ReactNativeSimulator::TextRun valignRun;
  valignRun.text = "Hi";
  valignRun.fontSize = 16;
  valignRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  valignParagraph.runs.push_back(valignRun);
  const auto valignPrepared = textLayout.prepare(valignParagraph, 80);
  const auto makeValignScene = [&](int align) {
    ReactNativeSimulator::SceneSnapshot scene;
    scene.surfaceId = 1;
    scene.revision = 1;
    scene.rootTag = 1;
    scene.viewportWidth = 80;
    scene.viewportHeight = 80;
    scene.pointScaleFactor = 1;
    ReactNativeSimulator::SceneNode root;
    root.tag = 1;
    root.layoutable = true;
    root.width = 80;
    root.height = 80;
    root.hasBackgroundColor = true;
    root.backgroundRed = 1;
    root.backgroundGreen = 1;
    root.backgroundBlue = 1;
    root.backgroundAlpha = 1;
    scene.nodes.push_back(root);
    ReactNativeSimulator::SceneNode text;
    text.tag = 2;
    text.parentTag = 1;
    text.layoutable = true;
    text.width = 80;
    text.height = 80;
    text.textAlignVertical = align;
    text.preparedText = valignPrepared;
    scene.nodes.push_back(text);
    return scene;
  };
  const auto centerValignFrame = renderer.render(makeValignScene(1));
  const auto topValignFrame = renderer.render(makeValignScene(0));
  if (!centerValignFrame || !topValignFrame) {
    std::cerr << "textAlignVertical scene failed\n";
    return 1;
  }
  const auto firstInkRow =
      [](const ReactNativeSimulator::SkiaRenderedFrame& frame) {
        for (int y = 0; y < frame.height; ++y) {
          for (int x = 0; x < frame.width; ++x) {
            const auto offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(frame.width) +
                 static_cast<std::size_t>(x)) *
                4;
            if (frame.rgba[offset] < 80 && frame.rgba[offset + 3] > 80) {
              return y;
            }
          }
        }
        return -1;
      };
  const auto topInk = firstInkRow(topValignFrame);
  const auto centerInk = firstInkRow(centerValignFrame);
  if (topInk < 0 || centerInk < 0 || centerInk < topInk + 12) {
    std::cerr << "textAlignVertical center did not drop ink: top=" << topInk
              << " center=" << centerInk << " preparedH="
              << valignPrepared->height() << '\n';
    return 1;
  }

  // Pixel/Roboto hyphenation, ellipsis, and wrap-strategy oracles. Linux
  // FontConfig/DejaVu advances differ enough to change wrap points (e.g.
  // "wit…" vs "with…"). Keep RTL, attachments, and transform coverage below.
  if (kLinuxFontConfigHost) {
    std::cerr << "skipping Pixel hyphenation/ellipsis/wrap-strategy oracles on "
                 "Linux FontConfig\n";
  } else {
  ReactNativeSimulator::TextParagraph noneHyphen;
  noneHyphen.hyphenation = ReactNativeSimulator::TextHyphenation::None;
  ReactNativeSimulator::TextRun noneRun;
  noneRun.text = "WillNotHaveAHyphenWhenBreakingForNewLine";
  noneRun.fontSize = 14;
  noneRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  noneHyphen.runs.push_back(noneRun);
  const auto nonePrepared = textLayout.prepare(noneHyphen, 180);
  if (nonePrepared->lineCount() < 2) {
    std::cerr << "hyphenation none did not wrap camelCase word, lines="
              << nonePrepared->lineCount() << '\n';
    return 1;
  }

  const auto stripZwsp = [](std::string text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
      if (index + 2 < text.size() &&
          static_cast<unsigned char>(text[index]) == 0xE2 &&
          static_cast<unsigned char>(text[index + 1]) == 0x80 &&
          static_cast<unsigned char>(text[index + 2]) == 0x8B) {
        index += 3;
        continue;
      }
      out.push_back(text[index++]);
    }
    return out;
  };
  const auto firstLineText =
      [&](const ReactNativeSimulator::SkiaPreparedParagraph& prepared) {
        return prepared.lines().empty()
            ? std::string{}
            : stripZwsp(prepared.lines().front().text);
      };
  const auto lastLineText =
      [&](const ReactNativeSimulator::SkiaPreparedParagraph& prepared) {
        return prepared.lines().empty()
            ? std::string{}
            : stripZwsp(prepared.lines().back().text);
      };
  const auto containsEllipsis = [](const std::string& text) {
    return text.find("…") != std::string::npos ||
        text.find("...") != std::string::npos;
  };

  // RN Tester Hyphenation uses wrappedText maxWidth 300 and a "None: " prefix.
  // Character wrap must break inside the identifier. The extra "ForNe" glyph
  // is an Android HINTING_ON / Roboto advance check, not a Generic-layout
  // haircut.
  ReactNativeSimulator::TextParagraph nonePrefixed = noneHyphen;
  nonePrefixed.breakStrategy = ReactNativeSimulator::TextBreakStrategy::Simple;
  ReactNativeSimulator::TextRun nonePrefix;
  nonePrefix.text = "None: ";
  nonePrefix.fontSize = 14;
  nonePrefix.foregroundColor = ReactNativeSimulator::TextColor{1, 0, 0, 1};
  nonePrefixed.runs.insert(nonePrefixed.runs.begin(), nonePrefix);
  const auto noneAt300 = textLayout.prepare(nonePrefixed, 300);
  const auto noneFirst = firstLineText(*noneAt300);
  if (noneAt300->lineCount() < 2 ||
      noneFirst.find("WillNotHave") == std::string::npos ||
      noneFirst.find("NewLine") != std::string::npos) {
    std::cerr << "hyphenation none at 300 did not character-wrap, lines="
              << noneAt300->lineCount() << " first=" << noneFirst << '\n';
    return 1;
  }
  if (std::filesystem::exists(androidFonts / "NotoSerif-Regular.ttf") &&
      std::filesystem::exists(androidFonts / "DroidSansMono.ttf")) {
    ReactNativeSimulator::SkiaTextLayoutEngine androidWrap(
        androidFonts, ReactNativeSimulator::TextFontPlatform::Android);
    const auto androidNone = androidWrap.prepare(nonePrefixed, 300);
    const auto androidFirst = firstLineText(*androidNone);
    if (androidNone->lineCount() < 2 ||
        androidFirst.find("ForNe") != std::string::npos) {
      std::cerr << "hyphenation none kept an extra character: " << androidFirst
                << '\n';
      return 1;
    }
  }

  ReactNativeSimulator::TextParagraph normalHyphen = nonePrefixed;
  normalHyphen.hyphenation = ReactNativeSimulator::TextHyphenation::Normal;
  normalHyphen.runs[1].text = "WillHaveAHyphenWhenBreakingForNewLine";
  normalHyphen.runs[0].text = "Normal: ";
  const auto normalAt300 = textLayout.prepare(normalHyphen, 300);
  const auto normalFirst = firstLineText(*normalAt300);
  if (normalAt300->lineCount() < 2 ||
      normalFirst.find("Breaking") == std::string::npos) {
    std::cerr << "hyphenation normal lost Breaking- wrap, lines="
              << normalAt300->lineCount() << " first=" << normalFirst << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph fullHyphen = nonePrefixed;
  fullHyphen.hyphenation = ReactNativeSimulator::TextHyphenation::Full;
  fullHyphen.runs[1].text = "WillHaveAHyphenWhenBreakingForNewLine";
  fullHyphen.runs[0].text = "Full: ";
  const auto fullAt300 = textLayout.prepare(fullHyphen, 300);
  const auto fullFirst = firstLineText(*fullAt300);
  if (fullAt300->lineCount() < 2 ||
      (fullFirst.find("ForNew") == std::string::npos &&
       fullFirst.find("Breaking") == std::string::npos)) {
    std::cerr << "hyphenation full lost camelCase hyphen wrap, lines="
              << fullAt300->lineCount() << " first=" << fullFirst << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph tailAt300;
  tailAt300.maximumNumberOfLines = 1;
  tailAt300.ellipsizeMode = ReactNativeSimulator::TextEllipsizeMode::Tail;
  tailAt300.breakStrategy = ReactNativeSimulator::TextBreakStrategy::Simple;
  ReactNativeSimulator::TextRun tailRun;
  tailRun.text =
      "This very long text should be truncated with dots in the end.";
  tailRun.fontSize = 14;
  tailRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  tailAt300.runs.push_back(tailRun);
  const auto tailPrepared = textLayout.prepare(tailAt300, 300);
  if (!tailPrepared->exceededMaximumLines() ||
      tailPrepared->lineCount() != 1 ||
      tailPrepared->longestLine() > 300.0f ||
      tailPrepared->layoutWidth() != 300.0f) {
    std::cerr << "tail ellipsis at 300 did not stay in-box, lines="
              << tailPrepared->lineCount()
              << " longest=" << tailPrepared->longestLine()
              << " layout=" << tailPrepared->layoutWidth() << '\n';
    return 1;
  }
  const auto tailText = firstLineText(*tailPrepared);
  if (tailText.find("end.") != std::string::npos ||
      !containsEllipsis(tailText)) {
    std::cerr << "tail ellipsis kept extra overflowing glyphs: " << tailText
              << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph hqTailAt300 = tailAt300;
  hqTailAt300.breakStrategy =
      ReactNativeSimulator::TextBreakStrategy::HighQuality;
  const auto hqTailPrepared = textLayout.prepare(hqTailAt300, 300);
  const auto hqTailText = firstLineText(*hqTailPrepared);
  if (hqTailPrepared->lineCount() != 1 ||
      !containsEllipsis(hqTailText) ||
      hqTailText.find("end.") != std::string::npos ||
      hqTailText.find("with") == std::string::npos) {
    std::cerr << "high-quality tail ellipsis at 300 missed overflow dots: "
              << hqTailText << " lines=" << hqTailPrepared->lineCount()
              << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph dynamicTruncated;
  dynamicTruncated.maximumNumberOfLines = 1;
  dynamicTruncated.ellipsizeMode =
      ReactNativeSimulator::TextEllipsizeMode::Tail;
  ReactNativeSimulator::TextRun dynamicRun;
  dynamicRun.text = "Truncated text is baaaaad.";
  dynamicRun.fontSize = 36;
  dynamicRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  dynamicTruncated.runs.push_back(dynamicRun);
  const auto dynamicPrepared = textLayout.prepare(dynamicTruncated, 360);
  const auto dynamicText = firstLineText(*dynamicPrepared);
  if (dynamicPrepared->lineCount() != 1 ||
      dynamicText.find("is ba") == std::string::npos ||
      !containsEllipsis(dynamicText) ||
      dynamicText.find("baaaaad") != std::string::npos ||
      dynamicText == "Truncated text") {
    std::cerr << "36px numberOfLines=1 tail did not fill the line, lines="
              << dynamicPrepared->lineCount() << " text=" << dynamicText
              << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph twoLineTail;
  twoLineTail.maximumNumberOfLines = 2;
  twoLineTail.ellipsizeMode = ReactNativeSimulator::TextEllipsizeMode::Tail;
  twoLineTail.breakStrategy =
      ReactNativeSimulator::TextBreakStrategy::HighQuality;
  ReactNativeSimulator::TextRun twoLineRun;
  twoLineRun.text =
      "RNTesterText of two lines no matter now much I write here. If I keep "
      "writing it'll just truncate after two lines";
  twoLineRun.fontSize = 14;
  twoLineRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  twoLineTail.runs.push_back(twoLineRun);
  const auto twoLinePrepared = textLayout.prepare(twoLineTail, 300);
  std::string twoLineCombined;
  for (const auto& line : twoLinePrepared->lines()) {
    twoLineCombined += stripZwsp(line.text);
  }
  const auto twoLast = lastLineText(*twoLinePrepared);
  const bool twoLastHasTruncateA =
      twoLast.find("truncate a") != std::string::npos;
  const bool twoLastHasTruncateWithDots =
      twoLast.find("truncate") != std::string::npos &&
      containsEllipsis(twoLast);
  auto twoLastWithoutDots = twoLast;
  for (const auto* token : {"…", "..."}) {
    std::size_t at = 0;
    while ((at = twoLastWithoutDots.find(token, at)) != std::string::npos) {
      twoLastWithoutDots.erase(at, std::char_traits<char>::length(token));
    }
  }
  while (!twoLastWithoutDots.empty() && twoLastWithoutDots.back() == ' ') {
    twoLastWithoutDots.pop_back();
  }
  if (twoLinePrepared->lineCount() != 2 ||
      !twoLinePrepared->exceededMaximumLines() ||
      !(containsEllipsis(twoLineCombined) || twoLastHasTruncateA ||
        twoLastHasTruncateWithDots) ||
      twoLastWithoutDots == "it'll just truncate") {
    std::cerr << "numberOfLines=2 tail dropped ellipsis, lines="
              << twoLinePrepared->lineCount()
              << " exceeded=" << twoLinePrepared->exceededMaximumLines()
              << " last=" << twoLast << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph clipAt300 = tailAt300;
  clipAt300.ellipsizeMode = ReactNativeSimulator::TextEllipsizeMode::Clip;
  clipAt300.runs.front().text =
      "Maximum of one line no matter now much I write here. If I keep writing "
      "it'll just truncate after one line";
  const auto clipPrepared = textLayout.prepare(clipAt300, 300);
  const auto clipText = firstLineText(*clipPrepared);
  if (clipPrepared->lineCount() != 1 ||
      clipPrepared->longestLine() > 300.0f ||
      clipText.find("truncate after") != std::string::npos ||
      clipText.find("I wri") != std::string::npos) {
    std::cerr << "clip numberOfLines=1 kept extra glyphs, lines="
              << clipPrepared->lineCount()
              << " longest=" << clipPrepared->longestLine()
              << " text=" << clipText << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph headAt300 = tailAt300;
  headAt300.ellipsizeMode = ReactNativeSimulator::TextEllipsizeMode::Head;
  headAt300.breakStrategy =
      ReactNativeSimulator::TextBreakStrategy::HighQuality;
  headAt300.runs.front().text =
      "This very long text should be truncated with dots in the beginning.";
  const auto headAt300Prepared = textLayout.prepare(headAt300, 300);
  const auto headAt300Text = firstLineText(*headAt300Prepared);
  if (headAt300Prepared->lineCount() != 1 ||
      headAt300Prepared->longestLine() > 300.0f) {
    std::cerr << "head ellipsis at 300 did not fit one line, longest="
              << headAt300Prepared->longestLine() << '\n';
    return 1;
  }
  if ((headAt300Text.find("hould") == std::string::npos &&
       headAt300Text.find("truncated") == std::string::npos) ||
      headAt300Text == "…with dots in the beginning." ||
      headAt300Text == "...with dots in the beginning.") {
    std::cerr << "head ellipsis at 300 kept too little suffix: "
              << headAt300Text << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph middleAt300 = tailAt300;
  middleAt300.ellipsizeMode =
      ReactNativeSimulator::TextEllipsizeMode::Middle;
  middleAt300.breakStrategy =
      ReactNativeSimulator::TextBreakStrategy::HighQuality;
  middleAt300.runs.front().text =
      "RNTesterText very long text should be truncated with dots in the middle.";
  const auto middleAt300Prepared = textLayout.prepare(middleAt300, 300);
  const auto middleAt300Text = firstLineText(*middleAt300Prepared);
  const auto middleEllipsisAt = middleAt300Text.find("…");
  const auto middleDotsAt = middleAt300Text.find("...");
  const auto middleMark = middleEllipsisAt != std::string::npos
      ? middleEllipsisAt
      : middleDotsAt;
  if (middleAt300Prepared->lineCount() != 1 ||
      middleMark == std::string::npos ||
      middleAt300Text.find("ith", middleMark) == std::string::npos) {
    std::cerr << "middle ellipsis at 300 lost t...ith, text="
              << middleAt300Text << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph allDetect;
  allDetect.dataDetector = ReactNativeSimulator::TextDataDetector::All;
  ReactNativeSimulator::TextRun allRun;
  allRun.text =
      "Phone 123-123-1234 Link https://www.facebook.com Email a@b.co";
  allRun.fontSize = 14;
  allRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  allDetect.runs.push_back(allRun);
  const auto allPrepared = textLayout.prepare(allDetect, 420);
  ReactNativeSimulator::SceneSnapshot detectScene;
  detectScene.surfaceId = 1;
  detectScene.revision = 1;
  detectScene.rootTag = 1;
  detectScene.viewportWidth = 420;
  detectScene.viewportHeight = 40;
  detectScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode detectRoot;
  detectRoot.tag = 1;
  detectRoot.layoutable = true;
  detectRoot.width = 420;
  detectRoot.height = 40;
  detectRoot.hasBackgroundColor = true;
  detectRoot.backgroundRed = 1;
  detectRoot.backgroundGreen = 1;
  detectRoot.backgroundBlue = 1;
  detectRoot.backgroundAlpha = 1;
  detectScene.nodes.push_back(detectRoot);
  ReactNativeSimulator::SceneNode detectText;
  detectText.tag = 2;
  detectText.parentTag = 1;
  detectText.layoutable = true;
  detectText.width = 420;
  detectText.height = 40;
  detectText.preparedText = allPrepared;
  detectScene.nodes.push_back(detectText);
  const auto detectFrame = renderer.render(detectScene);
  if (!detectFrame) {
    std::cerr << "dataDetector scene failed: " << detectFrame.error << '\n';
    return 1;
  }
  std::size_t linkBluePixels = 0;
  for (std::size_t offset = 0; offset + 3 < detectFrame.rgba.size();
       offset += 4) {
    if (detectFrame.rgba[offset] < 80 &&
        detectFrame.rgba[offset + 1] > 80 &&
        detectFrame.rgba[offset + 1] < 180 &&
        detectFrame.rgba[offset + 2] > 160 &&
        detectFrame.rgba[offset + 3] > 80) {
      ++linkBluePixels;
    }
  }
  if (linkBluePixels < 40) {
    std::cerr << "dataDetector all did not paint link spans, bluePixels="
              << linkBluePixels << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph urlDetect;
  urlDetect.dataDetector = ReactNativeSimulator::TextDataDetector::Link;
  urlDetect.hyphenation = ReactNativeSimulator::TextHyphenation::None;
  ReactNativeSimulator::TextRun urlRun;
  urlRun.text = "https://www.facebook.com";
  urlRun.fontSize = 14;
  urlRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  urlDetect.runs.push_back(urlRun);
  const auto urlPrepared = textLayout.prepare(urlDetect, 420);
  ReactNativeSimulator::SceneSnapshot urlScene = detectScene;
  urlScene.nodes[1].preparedText = urlPrepared;
  const auto urlFrame = renderer.render(urlScene);
  if (!urlFrame) {
    std::cerr << "url detector scene failed: " << urlFrame.error << '\n';
    return 1;
  }
  std::size_t urlBluePixels = 0;
  for (std::size_t offset = 0; offset + 3 < urlFrame.rgba.size();
       offset += 4) {
    if (urlFrame.rgba[offset] < 80 &&
        urlFrame.rgba[offset + 1] > 80 &&
        urlFrame.rgba[offset + 1] < 180 &&
        urlFrame.rgba[offset + 2] > 160 &&
        urlFrame.rgba[offset + 3] > 80) {
      ++urlBluePixels;
    }
  }
  if (urlBluePixels < 40) {
    std::cerr << "dataDetector link did not paint https URL, bluePixels="
              << urlBluePixels << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph qualityWrap;
  qualityWrap.breakStrategy = ReactNativeSimulator::TextBreakStrategy::HighQuality;
  ReactNativeSimulator::TextRun qualityRun;
  qualityRun.text =
      "The text should wrap if it goes on multiple lines. See, this is going to the next line.";
  qualityRun.fontSize = 14;
  qualityRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  qualityWrap.runs.push_back(qualityRun);
  const auto qualityPrepared = textLayout.prepare(qualityWrap, 300);
  ReactNativeSimulator::TextParagraph simpleWrap = qualityWrap;
  simpleWrap.breakStrategy = ReactNativeSimulator::TextBreakStrategy::Simple;
  const auto simplePrepared = textLayout.prepare(simpleWrap, 300);
  if (qualityPrepared->lineCount() < 2 || simplePrepared->lineCount() < 2) {
    std::cerr << "wrap sample did not wrap, quality="
              << qualityPrepared->lineCount()
              << " simple=" << simplePrepared->lineCount() << '\n';
    return 1;
  }
  const auto& qualityFirst = qualityPrepared->lines().front().text;
  const auto& simpleFirst = simplePrepared->lines().front().text;
  // Android HIGH_QUALITY is Knuth-Plass: a short last line is allowed, so
  // this 2-line English sample stays greedy like Simple. BALANCED evens.
  if (qualityFirst.find("lines") == std::string::npos) {
    std::cerr << "high-quality wrap dropped greedy fill: " << qualityFirst
              << '\n';
    return 1;
  }
  if (simpleFirst.find("lines") == std::string::npos) {
    std::cerr << "simple wrap dropped expected greedy fill: " << simpleFirst
              << '\n';
    return 1;
  }
  ReactNativeSimulator::TextParagraph balancedWrap = qualityWrap;
  balancedWrap.breakStrategy =
      ReactNativeSimulator::TextBreakStrategy::Balanced;
  const auto balancedPrepared = textLayout.prepare(balancedWrap, 300);
  const auto& balancedFirst = balancedPrepared->lines().front().text;
  if (balancedPrepared->lineCount() < 2 ||
      balancedFirst.find("lines") != std::string::npos) {
    std::cerr << "balanced wrap kept greedy first line: " << balancedFirst
              << '\n';
    return 1;
  }
  } // !kLinuxFontConfigHost Pixel hyphenation/ellipsis/wrap oracles

  ReactNativeSimulator::TextParagraph rtlAttach;
  rtlAttach.writingDirection =
      ReactNativeSimulator::TextWritingDirection::RightToLeft;
  rtlAttach.paragraphRtl = true;
  rtlAttach.alignment = ReactNativeSimulator::TextHorizontalAlignment::Left;
  ReactNativeSimulator::TextRun arabicRunRtl;
  arabicRunRtl.text = "مَٰنِ";
  arabicRunRtl.fontSize = 14;
  arabicRunRtl.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  rtlAttach.runs.push_back(arabicRunRtl);
  ReactNativeSimulator::TextRun redAttach;
  redAttach.attachment = true;
  redAttach.attachmentWidth = 10;
  redAttach.attachmentHeight = 10;
  rtlAttach.runs.push_back(redAttach);
  ReactNativeSimulator::TextRun helloAttachRun;
  helloAttachRun.text = " Hello";
  helloAttachRun.fontSize = 14;
  helloAttachRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  rtlAttach.runs.push_back(helloAttachRun);
  ReactNativeSimulator::TextRun blueAttach;
  blueAttach.attachment = true;
  blueAttach.attachmentWidth = 10;
  blueAttach.attachmentHeight = 10;
  rtlAttach.runs.push_back(blueAttach);
  const auto rtlAttachPrepared = textLayout.prepare(rtlAttach, 300);
  if (rtlAttachPrepared->attachments().size() != 2) {
    std::cerr << "rtl bidi attachments missing, count="
              << rtlAttachPrepared->attachments().size() << '\n';
    return 1;
  }
  if (rtlAttachPrepared->attachments()[1].x + 0.5f >=
      rtlAttachPrepared->attachments()[0].x) {
    std::cerr << "rtl bidi attachments were not reversed, redX="
              << rtlAttachPrepared->attachments()[0].x << " blueX="
              << rtlAttachPrepared->attachments()[1].x << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph arabicAuto;
  arabicAuto.alignment = ReactNativeSimulator::TextHorizontalAlignment::Natural;
  arabicAuto.paragraphRtl = false;
  ReactNativeSimulator::TextRun arabicRun;
  arabicRun.text = "أحب اللغة العربية auto";
  arabicRun.fontSize = 14;
  arabicRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  arabicAuto.runs.push_back(arabicRun);
  const auto arabicPrepared = textLayout.prepare(arabicAuto, 300);
  if (arabicPrepared->lines().empty() ||
      arabicPrepared->lines().front().left > 8.0f) {
    std::cerr << "arabic auto was not left-packed, left="
              << (arabicPrepared->lines().empty()
                      ? -1
                      : arabicPrepared->lines().front().left)
              << '\n';
    return 1;
  }

  ReactNativeSimulator::TextParagraph rtlHello;
  rtlHello.alignment = ReactNativeSimulator::TextHorizontalAlignment::Left;
  rtlHello.paragraphRtl = true;
  ReactNativeSimulator::TextRun helloRun;
  helloRun.text = "Hello World!";
  helloRun.fontSize = 14;
  helloRun.foregroundColor = ReactNativeSimulator::TextColor{0, 0, 0, 1};
  rtlHello.runs.push_back(helloRun);
  const auto rtlPrepared = textLayout.prepare(rtlHello, 300);
  if (rtlPrepared->lines().empty() ||
      rtlPrepared->lines().front().left < 150.0f) {
    std::cerr << "rtl layout left-align did not hug the right, left="
              << (rtlPrepared->lines().empty()
                      ? -1
                      : rtlPrepared->lines().front().left)
              << '\n';
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot parentXformScene;
  parentXformScene.surfaceId = 1;
  parentXformScene.revision = 1;
  parentXformScene.rootTag = 1;
  parentXformScene.viewportWidth = 80;
  parentXformScene.viewportHeight = 40;
  parentXformScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode parentXformRoot;
  parentXformRoot.tag = 1;
  parentXformRoot.layoutable = true;
  parentXformRoot.width = 80;
  parentXformRoot.height = 40;
  parentXformRoot.hasBackgroundColor = true;
  parentXformRoot.backgroundRed = 1;
  parentXformRoot.backgroundGreen = 1;
  parentXformRoot.backgroundBlue = 1;
  parentXformRoot.backgroundAlpha = 1;
  parentXformScene.nodes.push_back(parentXformRoot);
  ReactNativeSimulator::SceneNode parentXform;
  parentXform.tag = 2;
  parentXform.parentTag = 1;
  parentXform.layoutable = true;
  parentXform.width = 40;
  parentXform.height = 40;
  parentXform.hasTransform = true;
  parentXform.transformM[12] = 40;
  parentXformScene.nodes.push_back(parentXform);
  ReactNativeSimulator::SceneNode parentXformChild;
  parentXformChild.tag = 3;
  parentXformChild.parentTag = 2;
  parentXformChild.layoutable = true;
  parentXformChild.width = 20;
  parentXformChild.height = 20;
  parentXformChild.hasBackgroundColor = true;
  parentXformChild.backgroundRed = 1;
  parentXformChild.backgroundAlpha = 1;
  parentXformScene.nodes.push_back(parentXformChild);
  const auto parentXformFrame = renderer.render(parentXformScene);
  if (!parentXformFrame) {
    std::cerr << "parent transform scene failed: "
              << parentXformFrame.error << '\n';
    return 1;
  }
  const auto [pxR, pxG, pxB, pxA] =
      pixelAtFrame(parentXformFrame, 50, 10);
  const auto [unR, unG, unB, unA] =
      pixelAtFrame(parentXformFrame, 10, 10);
  if (pxR < 200 || pxA < 200 || pxG > 40 || pxB > 40) {
    std::cerr << "parent transform did not move nested child: " << pxR
              << "," << pxG << "," << pxB << "," << pxA << '\n';
    return 1;
  }
  if (unA > 200 && unR > 200 && unG < 40 && unB < 40) {
    std::cerr << "parent transform left nested child untransformed\n";
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot perspScene;
  perspScene.surfaceId = 1;
  perspScene.revision = 1;
  perspScene.rootTag = 1;
  perspScene.viewportWidth = 80;
  perspScene.viewportHeight = 80;
  perspScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode perspRoot;
  perspRoot.tag = 1;
  perspRoot.layoutable = true;
  perspRoot.width = 80;
  perspRoot.height = 80;
  perspScene.nodes.push_back(perspRoot);
  ReactNativeSimulator::SceneNode perspBox;
  perspBox.tag = 2;
  perspBox.parentTag = 1;
  perspBox.layoutable = true;
  perspBox.width = 80;
  perspBox.height = 80;
  perspBox.hasBackgroundColor = true;
  perspBox.backgroundRed = 1;
  perspBox.backgroundAlpha = 1;
  perspBox.hasTransform = true;
  // perspective(80) * rotateX(45deg): m[11] = -1/p, rotateX feeds Y into Z.
  {
    const float cosine = 0.70710678f;
    const float sine = 0.70710678f;
    const float k = -1.0f / 80.0f;
    perspBox.transformM[5] = cosine;
    perspBox.transformM[6] = sine;
    perspBox.transformM[7] = k * sine;
    perspBox.transformM[9] = -sine;
    perspBox.transformM[10] = cosine;
    perspBox.transformM[11] = k * cosine;
  }
  perspScene.nodes.push_back(perspBox);
  const auto perspFrame = renderer.render(perspScene);
  if (!perspFrame) {
    std::cerr << "perspective scene failed: " << perspFrame.error << '\n';
    return 1;
  }
  const auto [pcR, pcG, pcB, pcA] = pixelAtFrame(perspFrame, 40, 50);
  const auto [peR, peG, peB, peA] = pixelAtFrame(perspFrame, 2, 25);
  if (pcR < 200 || pcA < 200) {
    std::cerr << "perspective box lost its center: " << pcR << "," << pcG
              << "," << pcB << "," << pcA << '\n';
    return 1;
  }
  if (peA > 40 && peR > 200 && peG < 40 && peB < 40) {
    std::cerr << "perspective did not taper the near edge, still filled (2,25)\n";
    return 1;
  }

  auto renderTransformedBox = [&](std::initializer_list<std::pair<int, float>>
                                      entries) {
    ReactNativeSimulator::SceneSnapshot scene;
    scene.surfaceId = 1;
    scene.revision = 1;
    scene.rootTag = 1;
    scene.viewportWidth = 80;
    scene.viewportHeight = 80;
    scene.pointScaleFactor = 1;
    ReactNativeSimulator::SceneNode root;
    root.tag = 1;
    root.layoutable = true;
    root.width = 80;
    root.height = 80;
    scene.nodes.push_back(root);
    ReactNativeSimulator::SceneNode box;
    box.tag = 2;
    box.parentTag = 1;
    box.layoutable = true;
    box.width = 80;
    box.height = 80;
    box.hasBackgroundColor = true;
    box.backgroundRed = 1;
    box.backgroundAlpha = 1;
    box.hasTransform = true;
    for (const auto& [index, value] : entries) {
      box.transformM[index] = value;
    }
    scene.nodes.push_back(box);
    return renderer.render(scene);
  };
  const auto skewYFrame = renderTransformedBox({{1, 1.0f}});
  if (!skewYFrame) {
    std::cerr << "skewY scene failed: " << skewYFrame.error << '\n';
    return 1;
  }
  const auto [skewCenterR, skewCenterG, skewCenterB, skewCenterA] =
      pixelAtFrame(skewYFrame, 40, 40);
  const auto [skewInsideR, skewInsideG, skewInsideB, skewInsideA] =
      pixelAtFrame(skewYFrame, 10, 10);
  const auto [skewOutsideR, skewOutsideG, skewOutsideB, skewOutsideA] =
      pixelAtFrame(skewYFrame, 10, 70);
  if (skewCenterA < 200 || skewCenterR < 200) {
    std::cerr << "skewY lost its center\n";
    return 1;
  }
  if (skewInsideA < 200 || skewInsideR < 200) {
    std::cerr << "skewY did not keep the left-top of the parallelogram\n";
    return 1;
  }
  if (skewOutsideA > 40 && skewOutsideR > 200) {
    std::cerr << "skewY did not shear vertically, still filled (10,70)\n";
    return 1;
  }
  // Android decomposes skewY(45deg)+perspective into scale(√2, 1/√2)*rotateZ(45)
  // — a diamond, not the vertical parallelogram of pure skewY. The mapped
  // top-right corner of an 80x80 box sits near (100, 60).
  ReactNativeSimulator::SceneSnapshot skewPerspScene;
  skewPerspScene.surfaceId = 1;
  skewPerspScene.revision = 1;
  skewPerspScene.rootTag = 1;
  skewPerspScene.viewportWidth = 160;
  skewPerspScene.viewportHeight = 160;
  skewPerspScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode skewPerspRoot;
  skewPerspRoot.tag = 1;
  skewPerspRoot.layoutable = true;
  skewPerspRoot.width = 160;
  skewPerspRoot.height = 160;
  skewPerspScene.nodes.push_back(skewPerspRoot);
  ReactNativeSimulator::SceneNode skewPerspBox;
  skewPerspBox.tag = 2;
  skewPerspBox.parentTag = 1;
  skewPerspBox.layoutable = true;
  skewPerspBox.width = 80;
  skewPerspBox.height = 80;
  skewPerspBox.hasBackgroundColor = true;
  skewPerspBox.backgroundRed = 1;
  skewPerspBox.backgroundAlpha = 1;
  skewPerspBox.hasTransform = true;
  skewPerspBox.transformM[1] = 1.0f;
  skewPerspBox.transformM[11] = -1.0f;
  skewPerspScene.nodes.push_back(skewPerspBox);
  const auto skewPerspFrame = renderer.render(skewPerspScene);
  if (!skewPerspFrame) {
    std::cerr << "skewY+perspective scene failed: " << skewPerspFrame.error
              << '\n';
    return 1;
  }
  const auto [spCenterR, spCenterG, spCenterB, spCenterA] =
      pixelAtFrame(skewPerspFrame, 40, 40);
  const auto [spDiamondR, spDiamondG, spDiamondB, spDiamondA] =
      pixelAtFrame(skewPerspFrame, 90, 55);
  const auto [spMissR, spMissG, spMissB, spMissA] =
      pixelAtFrame(skewPerspFrame, 10, 70);
  if (spCenterA < 200 || spCenterR < 200) {
    std::cerr << "skewY+perspective lost its center\n";
    return 1;
  }
  if (spDiamondA < 200 || spDiamondR < 200) {
    std::cerr << "skewY+perspective did not paint the Android diamond at (90,55)\n";
    return 1;
  }
  if (spMissA > 40 && spMissR > 200) {
    std::cerr << "skewY+perspective still filled the unsheared bottom-left\n";
    return 1;
  }

  const float rotateYCos = 0.70710678f;
  const float rotateYSin = 0.70710678f;
  const float rotateYK = -1.0f / 80.0f;
  const auto rotateYFrame = renderTransformedBox({
      {0, rotateYCos},
      {2, -rotateYSin},
      {3, -rotateYK * rotateYSin},
      {8, rotateYSin},
      {10, rotateYCos},
      {11, rotateYK * rotateYCos},
  });
  if (!rotateYFrame) {
    std::cerr << "perspective+rotateY scene failed: " << rotateYFrame.error
              << '\n';
    return 1;
  }
  const auto [ryCenterR, ryCenterG, ryCenterB, ryCenterA] =
      pixelAtFrame(rotateYFrame, 40, 40);
  const auto [ryFarR, ryFarG, ryFarB, ryFarA] =
      pixelAtFrame(rotateYFrame, 75, 40);
  if (ryCenterA < 200 || ryCenterR < 200) {
    std::cerr << "perspective+rotateY lost its center\n";
    return 1;
  }
  if (ryFarA > 40 && ryFarR > 200 && ryFarG < 40 && ryFarB < 40) {
    std::cerr << "perspective+rotateY did not taper the far edge, still filled (75,40)\n";
    return 1;
  }

  // TransformStylesExample composes [{rotateX}, {perspective}] = Rx * P, which
  // does not project Z. Android decomposes and reapplies camera+rotateX so the
  // same pair still foreshortens.
  const auto rotateXThenPersp = renderTransformedBox({
      {5, 0.70710678f},
      {6, 0.70710678f},
      {9, -0.70710678f},
      {10, 0.70710678f},
      {11, -1.0f / 80.0f},
  });
  if (!rotateXThenPersp) {
    std::cerr << "rotateX+perspective scene failed: " << rotateXThenPersp.error
              << '\n';
    return 1;
  }
  const auto [rxpCR, rxpCG, rxpCB, rxpCA] =
      pixelAtFrame(rotateXThenPersp, 40, 50);
  const auto [rxpER, rxpEG, rxpEB, rxpEA] =
      pixelAtFrame(rotateXThenPersp, 2, 25);
  if (rxpCA < 200 || rxpCR < 200) {
    std::cerr << "rotateX+perspective lost its center\n";
    return 1;
  }
  if (rxpEA > 40 && rxpER > 200 && rxpEG < 40 && rxpEB < 40) {
    std::cerr << "rotateX then perspective did not taper, still filled (2,25)\n";
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot fadeBorderScene;
  fadeBorderScene.surfaceId = 1;
  fadeBorderScene.revision = 1;
  fadeBorderScene.rootTag = 1;
  fadeBorderScene.viewportWidth = 40;
  fadeBorderScene.viewportHeight = 40;
  fadeBorderScene.pointScaleFactor = 1;
  ReactNativeSimulator::SceneNode fadeBorderRoot;
  fadeBorderRoot.tag = 1;
  fadeBorderRoot.layoutable = true;
  fadeBorderRoot.width = 40;
  fadeBorderRoot.height = 40;
  fadeBorderScene.nodes.push_back(fadeBorderRoot);
  ReactNativeSimulator::SceneNode fadeParent;
  fadeParent.tag = 2;
  fadeParent.parentTag = 1;
  fadeParent.layoutable = true;
  fadeParent.width = 40;
  fadeParent.height = 40;
  fadeParent.opacity = 0.25f;
  fadeBorderScene.nodes.push_back(fadeParent);
  ReactNativeSimulator::SceneNode fadeBorder;
  fadeBorder.tag = 3;
  fadeBorder.parentTag = 2;
  fadeBorder.layoutable = true;
  fadeBorder.width = 40;
  fadeBorder.height = 40;
  fadeBorder.borderTop = 4;
  fadeBorder.borderRight = 4;
  fadeBorder.borderBottom = 4;
  fadeBorder.borderLeft = 4;
  fadeBorder.hasBorderColor = true;
  fadeBorder.borderRed = 1;
  fadeBorder.borderAlpha = 1;
  fadeBorderScene.nodes.push_back(fadeBorder);
  const auto fadeBorderFrame = renderer.render(fadeBorderScene);
  if (!fadeBorderFrame) {
    std::cerr << "inherited opacity border scene failed: "
              << fadeBorderFrame.error << '\n';
    return 1;
  }
  const auto [fadeBR, fadeBG, fadeBB, fadeBA] =
      pixelAtFrame(fadeBorderFrame, 2, 20);
  if (fadeBA > 120 || fadeBA < 20 || fadeBR < fadeBG + 20) {
    std::cerr << "parent opacity did not fade nested border: " << fadeBR
              << "," << fadeBG << "," << fadeBB << "," << fadeBA << '\n';
    return 1;
  }

  return 0;
}
