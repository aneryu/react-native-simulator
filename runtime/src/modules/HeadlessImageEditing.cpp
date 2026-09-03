#include "HeadlessImageEditing.h"
#include "HeadlessHttp.h"
#include "HeadlessImageAssets.h"
#include "HeadlessBlob.h"
#include "ImageBuffer.h"
#include "ImagePixelSize.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace jsi = facebook::jsi;
namespace react = facebook::react;

namespace {
std::shared_ptr<jsi::Function> functionArg(
    jsi::Runtime& runtime,
    const jsi::Value* args,
    size_t count,
    size_t index) {
  if (index >= count || !args[index].isObject() ||
      !args[index].getObject(runtime).isFunction(runtime)) {
    return nullptr;
  }
  return std::make_shared<jsi::Function>(
      args[index].getObject(runtime).getFunction(runtime));
}

void invokeStringCallback(
    const std::shared_ptr<react::CallInvoker>& jsInvoker,
    const std::shared_ptr<jsi::Function>& callback,
    std::string value) {
  if (!jsInvoker || !callback) {
    return;
  }
  jsInvoker->invokeAsync(
      [callback, value = std::move(value)](jsi::Runtime& runtime) {
        callback->call(
            runtime, jsi::String::createFromUtf8(runtime, value));
      });
}

std::optional<std::string> readFileBytes(const std::filesystem::path& path) {
  if (path.empty()) {
    return std::nullopt;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  return std::string(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
}

bool writeFileBytes(const std::filesystem::path& path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(output);
}

std::string uniqueTempName(const std::string& prefix, const std::string& extension) {
  std::random_device device;
  std::ostringstream name;
  name << prefix << '-' << std::hex << device() << device() << '.' << extension;
  return name.str();
}

std::string fileUriForPath(const std::filesystem::path& path) {
  return "file://" + path.string();
}

void loadImageBytes(
    const std::string& uri,
    const std::filesystem::path& assetRoot,
    std::function<void(std::string, std::string)> callback) {
  if (uri.empty()) {
    callback({}, "empty uri");
    return;
  }
  auto local = resolveHeadlessLocalImage(uri, assetRoot);
  if (local.empty()) {
    local = headlessCachedImagePath(uri);
  }
  if (!local.empty()) {
    auto bytes = readFileBytes(local);
    if (!bytes) {
      callback({}, "failed to read image");
      return;
    }
    callback(std::move(*bytes), {});
    return;
  }
  if (uri.starts_with("http://") || uri.starts_with("https://")) {
    headlessFetchImage(
        uri,
        [callback = std::move(callback)](
            std::filesystem::path path, std::string error) {
          if (path.empty()) {
            callback({}, error.empty() ? "failed to download image" : error);
            return;
          }
          auto bytes = readFileBytes(path);
          if (!bytes) {
            callback({}, "failed to read image");
            return;
          }
          callback(std::move(*bytes), {});
        });
    return;
  }
  callback({}, "unable to load image");
}

struct CropData {
  double offsetX{0};
  double offsetY{0};
  double width{0};
  double height{0};
  bool hasDisplaySize{false};
  double displayWidth{0};
  double displayHeight{0};
  std::string resizeMode{"contain"};
};

double numberProp(
    jsi::Runtime& runtime,
    const jsi::Object& object,
    const char* name) {
  const auto value = object.getProperty(runtime, name);
  return value.isNumber() ? value.getNumber() : 0;
}

CropData parseCropData(jsi::Runtime& runtime, const jsi::Value& value) {
  CropData data;
  if (!value.isObject()) {
    return data;
  }
  auto object = value.getObject(runtime);
  const auto offset = object.getProperty(runtime, "offset");
  if (offset.isObject()) {
    auto offsetObject = offset.getObject(runtime);
    data.offsetX = numberProp(runtime, offsetObject, "x");
    data.offsetY = numberProp(runtime, offsetObject, "y");
  }
  const auto size = object.getProperty(runtime, "size");
  if (size.isObject()) {
    auto sizeObject = size.getObject(runtime);
    data.width = numberProp(runtime, sizeObject, "width");
    data.height = numberProp(runtime, sizeObject, "height");
  }
  const auto displaySize = object.getProperty(runtime, "displaySize");
  if (displaySize.isObject()) {
    auto displayObject = displaySize.getObject(runtime);
    data.displayWidth = numberProp(runtime, displayObject, "width");
    data.displayHeight = numberProp(runtime, displayObject, "height");
    data.hasDisplaySize = data.displayWidth > 0 && data.displayHeight > 0;
  }
  const auto resizeMode = object.getProperty(runtime, "resizeMode");
  if (resizeMode.isString()) {
    data.resizeMode = resizeMode.getString(runtime).utf8(runtime);
  }
  return data;
}

std::string cropAndWritePng(
    const std::string& bytes,
    const CropData& cropData,
    std::string& error) {
  auto decoded = decodePngToRgba(bytes);
  if (!decoded) {
    error = "Unable to decode image";
    return {};
  }
  if (!(cropData.width > 0) || !(cropData.height > 0)) {
    error = "Invalid crop size";
    return {};
  }
  const double x0 = std::max(0.0, cropData.offsetX);
  const double y0 = std::max(0.0, cropData.offsetY);
  const double x1 = std::min(
      static_cast<double>(decoded->width), cropData.offsetX + cropData.width);
  const double y1 = std::min(
      static_cast<double>(decoded->height), cropData.offsetY + cropData.height);
  const int cropX = static_cast<int>(std::lround(x0));
  const int cropY = static_cast<int>(std::lround(y0));
  const int cropWidth = static_cast<int>(std::lround(x1 - x0));
  const int cropHeight = static_cast<int>(std::lround(y1 - y0));
  if (cropWidth <= 0 || cropHeight <= 0) {
    error = "Invalid crop size";
    return {};
  }
  auto output = cropRgbaImage(*decoded, cropX, cropY, cropWidth, cropHeight);
  if (cropData.hasDisplaySize) {
    const int destWidth =
        std::max(1, static_cast<int>(std::lround(cropData.displayWidth)));
    const int destHeight =
        std::max(1, static_cast<int>(std::lround(cropData.displayHeight)));
    output = resizeRgbaImage(output, destWidth, destHeight, cropData.resizeMode);
    if (output.rgba.empty()) {
      error = "Unable to crop image";
      return {};
    }
  }
  auto encoded = encodeRgbaToPng(output);
  if (!encoded) {
    error = "Unable to write cropped image";
    return {};
  }
  const auto path = std::filesystem::temp_directory_path() /
      uniqueTempName("rnsim-edit", "png");
  if (!writeFileBytes(path, *encoded)) {
    error = "Unable to write cropped image";
    return {};
  }
  return fileUriForPath(path);
}

class HeadlessImageEditingModule final : public react::TurboModule {
 public:
  HeadlessImageEditingModule(
      std::shared_ptr<react::CallInvoker> jsInvoker,
      std::filesystem::path assetDirectory)
      : TurboModule("ImageEditingManager", std::move(jsInvoker)),
        assetDirectory_(std::move(assetDirectory)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["cropImage"] = {4, &cropImage};
  }

 private:
  std::filesystem::path assetDirectory_;

  static HeadlessImageEditingModule& self(react::TurboModule& module) {
    return static_cast<HeadlessImageEditingModule&>(module);
  }

  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Object(runtime);
  }

  static jsi::Value cropImage(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    std::string uri;
    if (count > 0 && args[0].isString()) {
      uri = args[0].getString(runtime).utf8(runtime);
    }
    CropData cropData;
    if (count > 1) {
      cropData = parseCropData(runtime, args[1]);
    }
    auto success = functionArg(runtime, args, count, 2);
    auto error = functionArg(runtime, args, count, 3);
    auto jsInvoker = self(module).jsInvoker_;
    auto assetDirectory = self(module).assetDirectory_;
    loadImageBytes(
        uri,
        assetDirectory,
        [jsInvoker, success, error, cropData](
            std::string bytes, std::string loadError) {
          if (!loadError.empty()) {
            invokeStringCallback(jsInvoker, error, std::move(loadError));
            return;
          }
          std::string writeError;
          std::string fileUri = cropAndWritePng(bytes, cropData, writeError);
          if (fileUri.empty()) {
            invokeStringCallback(
                jsInvoker,
                error,
                writeError.empty() ? "Unable to crop image" : writeError);
            return;
          }
          invokeStringCallback(jsInvoker, success, std::move(fileUri));
        });
    return jsi::Value::undefined();
  }
};

struct ImageStoreState {
  std::mutex mutex;
  std::unordered_map<std::string, std::filesystem::path> tags;
};

class HeadlessImageStoreModule final : public react::TurboModule {
 public:
  HeadlessImageStoreModule(
      std::shared_ptr<react::CallInvoker> jsInvoker,
      std::filesystem::path assetDirectory)
      : TurboModule("ImageStoreManager", std::move(jsInvoker)),
        assetDirectory_(std::move(assetDirectory)),
        store_(std::make_shared<ImageStoreState>()) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["getBase64ForTag"] = {3, &getBase64ForTag};
    methodMap_["hasImageForTag"] = {2, &hasImageForTag};
    methodMap_["removeImageForTag"] = {1, &removeImageForTag};
    methodMap_["addImageFromBase64"] = {3, &addImageFromBase64};
  }

 private:
  std::filesystem::path assetDirectory_;
  std::shared_ptr<ImageStoreState> store_;

  static HeadlessImageStoreModule& self(react::TurboModule& module) {
    return static_cast<HeadlessImageStoreModule&>(module);
  }

  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Object(runtime);
  }

  static jsi::Value getBase64ForTag(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    std::string uri;
    if (count > 0 && args[0].isString()) {
      uri = args[0].getString(runtime).utf8(runtime);
    }
    auto success = functionArg(runtime, args, count, 1);
    auto error = functionArg(runtime, args, count, 2);
    auto jsInvoker = self(module).jsInvoker_;
    auto assetDirectory = self(module).assetDirectory_;
    auto store = self(module).store_;
    std::filesystem::path stored;
    {
      std::lock_guard<std::mutex> lock(store->mutex);
      const auto found = store->tags.find(uri);
      if (found != store->tags.end()) {
        stored = found->second;
      }
    }
    auto encodeAndCallback = [jsInvoker, success, error](std::string bytes) {
      invokeStringCallback(
          jsInvoker, success, headlessBlobBase64Encode(bytes));
    };
    if (!stored.empty()) {
      auto bytes = readFileBytes(stored);
      if (!bytes) {
        invokeStringCallback(jsInvoker, error, "failed to read image");
        return jsi::Value::undefined();
      }
      encodeAndCallback(std::move(*bytes));
      return jsi::Value::undefined();
    }
    loadImageBytes(
        uri,
        assetDirectory,
        [jsInvoker, success, error, encodeAndCallback](
            std::string bytes, std::string loadError) {
          if (!loadError.empty()) {
            invokeStringCallback(jsInvoker, error, std::move(loadError));
            return;
          }
          encodeAndCallback(std::move(bytes));
        });
    return jsi::Value::undefined();
  }

  static jsi::Value hasImageForTag(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    std::string uri;
    if (count > 0 && args[0].isString()) {
      uri = args[0].getString(runtime).utf8(runtime);
    }
    bool has = false;
    {
      std::lock_guard<std::mutex> lock(self(module).store_->mutex);
      has = self(module).store_->tags.contains(uri);
    }
    if (count > 1 && args[1].isObject() &&
        args[1].getObject(runtime).isFunction(runtime)) {
      args[1].getObject(runtime).getFunction(runtime).call(runtime, has);
    }
    return jsi::Value::undefined();
  }

  static jsi::Value removeImageForTag(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    std::string uri;
    if (count > 0 && args[0].isString()) {
      uri = args[0].getString(runtime).utf8(runtime);
    }
    std::filesystem::path path;
    {
      std::lock_guard<std::mutex> lock(self(module).store_->mutex);
      auto& tags = self(module).store_->tags;
      const auto found = tags.find(uri);
      if (found != tags.end()) {
        path = found->second;
        tags.erase(found);
      }
    }
    if (!path.empty()) {
      std::error_code error;
      std::filesystem::remove(path, error);
    }
    return jsi::Value::undefined();
  }

  static jsi::Value addImageFromBase64(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    std::string base64;
    if (count > 0 && args[0].isString()) {
      base64 = args[0].getString(runtime).utf8(runtime);
    }
    auto success = functionArg(runtime, args, count, 1);
    auto error = functionArg(runtime, args, count, 2);
    auto jsInvoker = self(module).jsInvoker_;
    auto store = self(module).store_;
    auto bytes = headlessBlobBase64Decode(base64);
    if (bytes.empty() || !imagePixelSizeFromBytes(bytes)) {
      invokeStringCallback(
          jsInvoker, error, "Failed to add image from base64String");
      return jsi::Value::undefined();
    }
    const auto extension = guessImageExtension(bytes);
    const auto path = std::filesystem::temp_directory_path() /
        uniqueTempName("rnsim-store", extension);
    bool written = false;
    if (bytesLookLikePng(bytes) || bytesLookLikeJpeg(bytes)) {
      written = writeFileBytes(path, bytes);
    } else if (auto decoded = decodePngToRgba(bytes)) {
      if (auto encoded = encodeRgbaToPng(*decoded)) {
        written = writeFileBytes(path, *encoded);
      }
    }
    if (!written) {
      invokeStringCallback(
          jsInvoker, error, "Failed to add image from base64String");
      return jsi::Value::undefined();
    }
    const auto tag = fileUriForPath(path);
    {
      std::lock_guard<std::mutex> lock(store->mutex);
      store->tags[tag] = path;
    }
    invokeStringCallback(jsInvoker, success, tag);
    return jsi::Value::undefined();
  }
};
} // namespace

std::shared_ptr<facebook::react::TurboModule> createHeadlessImageEditingModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
    std::filesystem::path assetDirectory) {
  return std::make_shared<HeadlessImageEditingModule>(
      std::move(jsInvoker), std::move(assetDirectory));
}

std::shared_ptr<facebook::react::TurboModule> createHeadlessImageStoreModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
    std::filesystem::path assetDirectory) {
  return std::make_shared<HeadlessImageStoreModule>(
      std::move(jsInvoker), std::move(assetDirectory));
}
