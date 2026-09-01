#pragma once

#include <react/renderer/textlayoutmanager/TextLayoutManager.h>

#include <filesystem>
#include <string>

#if RNS_ENABLE_SKIA
#include <react/renderer/textlayoutmanager/TextMeasureCache.h>
#include <react/utils/SimpleThreadSafeCache.h>

#include "SkiaTextLayoutEngine.h"

#include <mutex>
#include <vector>
#endif

namespace facebook::react {

// Keeps ParagraphShadowNode on React Native's normal measurement path while
// using the simulator's Skia paragraph service in Skia-enabled builds.
class HeadlessTextLayoutManager final : public TextLayoutManager {
 public:
  explicit HeadlessTextLayoutManager(
      const std::shared_ptr<const ContextContainer>& contextContainer,
      const std::filesystem::path& fontDirectory = {},
      const std::string& platform = "");

  TextMeasurement measure(
      const AttributedStringBox& attributedStringBox,
      const ParagraphAttributes& paragraphAttributes,
      const TextLayoutContext& layoutContext,
      const LayoutConstraints& layoutConstraints) const override;

  LinesMeasurements measureLines(
      const AttributedStringBox& attributedStringBox,
      const ParagraphAttributes& paragraphAttributes,
      const Size& size) const override;

#if RNS_ENABLE_SKIA
  std::shared_ptr<const ReactNativeSimulator::SkiaPreparedParagraph> prepare(
      const AttributedString& attributedString,
      const ParagraphAttributes& paragraphAttributes,
      const TextLayoutContext& layoutContext,
      const LayoutConstraints& layoutConstraints) const;

  std::shared_ptr<const ReactNativeSimulator::SkiaPreparedParagraph>
  prepareForPaint(
      const AttributedString& attributedString,
      const ParagraphAttributes& paragraphAttributes,
      const TextLayoutContext& layoutContext,
      const LayoutConstraints& layoutConstraints,
      ReactNativeSimulator::TextDataDetector dataDetector =
          ReactNativeSimulator::TextDataDetector::None) const;
#endif

 private:
#if RNS_ENABLE_SKIA
  ReactNativeSimulator::SkiaTextLayoutEngine skiaTextLayoutEngine_;
  mutable SimpleThreadSafeCache<
      PreparedTextCacheKey,
      std::shared_ptr<const ReactNativeSimulator::SkiaPreparedParagraph>,
      kSimpleThreadSafeCacheSizeCap>
      preparedTextCache_;
  struct MeasuredParagraph {
    AttributedString attributedString;
    ParagraphAttributes paragraphAttributes;
    Float pointScaleFactor{1};
    std::shared_ptr<const ReactNativeSimulator::SkiaPreparedParagraph>
        prepared;
  };
  mutable std::mutex measuredParagraphsMutex_;
  mutable std::vector<MeasuredParagraph> measuredParagraphs_;
  mutable Float lastPointScaleFactor_{1};
#endif
};

} // namespace facebook::react
