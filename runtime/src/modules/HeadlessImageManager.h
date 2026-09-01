#pragma once

#include <react/renderer/imagemanager/ImageManager.h>
#include <ReactCommon/RuntimeExecutor.h>

#include <atomic>
#include <filesystem>
#include <memory>

class HeadlessImageManager final : public facebook::react::ImageManager {
 public:
  HeadlessImageManager(
      const std::shared_ptr<const facebook::react::ContextContainer>&
          contextContainer,
      std::filesystem::path assetRoot,
      facebook::react::RuntimeExecutor runtimeExecutor);

  ~HeadlessImageManager() override;

  facebook::react::ImageRequest requestImage(
      const facebook::react::ImageSource& imageSource,
      facebook::react::SurfaceId surfaceId,
      const facebook::react::ImageRequestParams& imageRequestParams,
      facebook::react::Tag tag) const override;

 private:
  std::filesystem::path assetRoot_;
  facebook::react::RuntimeExecutor runtimeExecutor_{};
  std::shared_ptr<std::atomic<bool>> alive_;
};
