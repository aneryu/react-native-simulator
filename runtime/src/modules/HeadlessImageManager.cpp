#include "HeadlessImageManager.h"

#include "HeadlessHttp.h"
#include "HeadlessImageAssets.h"

#include <jsi/jsi.h>
#include <react/renderer/imagemanager/ImageResponse.h>

#include <filesystem>
#include <functional>
#include <string>
#include <utility>

namespace react = facebook::react;

HeadlessImageManager::HeadlessImageManager(
    const std::shared_ptr<const react::ContextContainer>& contextContainer,
    std::filesystem::path assetRoot,
    react::RuntimeExecutor runtimeExecutor)
    : ImageManager(contextContainer),
      assetRoot_(std::move(assetRoot)),
      runtimeExecutor_(std::move(runtimeExecutor)),
      alive_(std::make_shared<std::atomic<bool>>(true)) {}

HeadlessImageManager::~HeadlessImageManager() {
  if (alive_) {
    alive_->store(false);
  }
}

react::ImageRequest HeadlessImageManager::requestImage(
    const react::ImageSource& imageSource,
    react::SurfaceId /*surfaceId*/,
    const react::ImageRequestParams& /*imageRequestParams*/,
    react::Tag /*tag*/) const {
  auto request = react::ImageRequest(imageSource, nullptr, {}, {});
  auto coordinator = request.getSharedObserverCoordinator();
  const auto uri = imageSource.uri;
  const auto assetRoot = assetRoot_;
  auto completePath = [coordinator](const std::filesystem::path& path) {
    coordinator->nativeImageResponseComplete(react::ImageResponse(
        std::make_shared<std::string>(path.string()), nullptr));
  };
  auto fail = [coordinator](std::string message) {
    coordinator->nativeImageResponseFailed(
        react::ImageLoadError(std::make_shared<std::string>(std::move(message))));
  };

  auto local = resolveHeadlessLocalImage(uri, assetRoot);
  if (local.empty()) {
    local = headlessCachedImagePath(uri);
  }
  if (!local.empty()) {
    completePath(local);
    return request;
  }
  if (uri.starts_with("http://") || uri.starts_with("https://")) {
    auto runtimeExecutor = runtimeExecutor_;
    auto alive = alive_;
    headlessFetchImage(
        uri,
        [alive, runtimeExecutor, coordinator, completePath, fail](
            std::filesystem::path path, std::string error) {
          auto work = [coordinator,
                       completePath,
                       fail,
                       path = std::move(path),
                       error = std::move(error)]() {
            if (path.empty()) {
              fail(error.empty() ? "Could not load image" : error);
              return;
            }
            coordinator->nativeImageResponseProgress(1.0f, 1, 1);
            completePath(path);
          };
          if (!alive || !alive->load()) {
            return;
          }
          if (!runtimeExecutor) {
            work();
            return;
          }
          runtimeExecutor([alive, work = std::move(work)](
                              facebook::jsi::Runtime&) {
            if (!alive || !alive->load()) {
              return;
            }
            work();
          });
        });
    return request;
  }
  fail("Could not load image");
  return request;
}
