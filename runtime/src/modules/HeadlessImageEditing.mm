#import "HeadlessImageEditing.h"
#import "HeadlessHttp.h"
#import "HeadlessImageAssets.h"

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace jsi = facebook::jsi;
namespace react = facebook::react;

namespace {
NSString* nsString(const std::string& value) {
  NSString* string = [[NSString alloc] initWithBytes:value.data()
                                              length:value.size()
                                            encoding:NSUTF8StringEncoding];
  return string == nil ? @"" : string;
}

std::string stdString(NSString* value) {
  if (value == nil) {
    return {};
  }
  const char* utf8 = value.UTF8String;
  return utf8 == nullptr ? std::string{} : std::string(utf8);
}

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
  NSData* data = [NSData dataWithContentsOfFile:nsString(path.string())];
  if (data == nil || data.length == 0) {
    return std::nullopt;
  }
  return std::string(
      static_cast<const char*>(data.bytes),
      static_cast<const char*>(data.bytes) + data.length);
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

CGImageRef decodeImage(const std::string& bytes) {
  if (bytes.empty()) {
    return nullptr;
  }
  NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
  CGImageSourceRef source =
      CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr);
  if (source == nullptr) {
    return nullptr;
  }
  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  CFRelease(source);
  return image;
}

bool writeImageAsPng(CGImageRef image, NSString* path) {
  if (image == nullptr || path.length == 0) {
    return false;
  }
  NSURL* url = [NSURL fileURLWithPath:path];
  if (url == nil) {
    return false;
  }
  CGImageDestinationRef destination = CGImageDestinationCreateWithURL(
      (__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
  if (destination == nullptr) {
    return false;
  }
  CGImageDestinationAddImage(destination, image, nullptr);
  const bool ok = CGImageDestinationFinalize(destination);
  CFRelease(destination);
  return ok;
}

std::string fileUriForPath(NSString* path) {
  return std::string("file://") + stdString(path);
}

CGImageRef renderImage(CGImageRef image, int width, int height) {
  if (image == nullptr || width <= 0 || height <= 0) {
    return nullptr;
  }
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  if (colorSpace == nullptr) {
    return nullptr;
  }
  CGContextRef context = CGBitmapContextCreate(
      nullptr,
      static_cast<size_t>(width),
      static_cast<size_t>(height),
      8,
      0,
      colorSpace,
      kCGBitmapByteOrder32Big |
          static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast));
  CGColorSpaceRelease(colorSpace);
  if (context == nullptr) {
    return nullptr;
  }
  CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
  CGContextTranslateCTM(context, 0, height);
  CGContextScaleCTM(context, 1, -1);
  CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
  CGImageRef result = CGBitmapContextCreateImage(context);
  CGContextRelease(context);
  return result;
}

CGImageRef resizeImage(
    CGImageRef image,
    int destWidth,
    int destHeight,
    const std::string& resizeMode) {
  const int srcWidth = static_cast<int>(CGImageGetWidth(image));
  const int srcHeight = static_cast<int>(CGImageGetHeight(image));
  if (srcWidth <= 0 || srcHeight <= 0 || destWidth <= 0 || destHeight <= 0) {
    return nullptr;
  }
  if (resizeMode == "center" && srcWidth <= destWidth &&
      srcHeight <= destHeight) {
    return CGImageRetain(image);
  }
  if (resizeMode == "cover") {
    const double scale = std::max(
        static_cast<double>(destWidth) / srcWidth,
        static_cast<double>(destHeight) / srcHeight);
    const double cropWidth = destWidth / scale;
    const double cropHeight = destHeight / scale;
    const double cropX = (srcWidth - cropWidth) / 2.0;
    const double cropY = (srcHeight - cropHeight) / 2.0;
    CGImageRef cropped = CGImageCreateWithImageInRect(
        image, CGRectMake(cropX, cropY, cropWidth, cropHeight));
    if (cropped == nullptr) {
      return nullptr;
    }
    CGImageRef rendered = renderImage(cropped, destWidth, destHeight);
    CFRelease(cropped);
    return rendered;
  }
  // Repeat is not tiled; treated as stretch.
  if (resizeMode == "stretch" || resizeMode == "repeat") {
    return renderImage(image, destWidth, destHeight);
  }
  const double scale = std::min(
      static_cast<double>(destWidth) / srcWidth,
      static_cast<double>(destHeight) / srcHeight);
  const int outWidth =
      std::max(1, static_cast<int>(std::lround(srcWidth * scale)));
  const int outHeight =
      std::max(1, static_cast<int>(std::lround(srcHeight * scale)));
  return renderImage(image, outWidth, outHeight);
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
  CGImageRef image = decodeImage(bytes);
  if (image == nullptr) {
    error = "Unable to decode image";
    return {};
  }
  const int imageWidth = static_cast<int>(CGImageGetWidth(image));
  const int imageHeight = static_cast<int>(CGImageGetHeight(image));
  if (!(cropData.width > 0) || !(cropData.height > 0)) {
    CFRelease(image);
    error = "Invalid crop size";
    return {};
  }
  const double x0 = std::max(0.0, cropData.offsetX);
  const double y0 = std::max(0.0, cropData.offsetY);
  const double x1 = std::min(
      static_cast<double>(imageWidth), cropData.offsetX + cropData.width);
  const double y1 = std::min(
      static_cast<double>(imageHeight), cropData.offsetY + cropData.height);
  const double clampedWidth = x1 - x0;
  const double clampedHeight = y1 - y0;
  if (!(clampedWidth > 0) || !(clampedHeight > 0)) {
    CFRelease(image);
    error = "Invalid crop size";
    return {};
  }
  CGImageRef cropped = CGImageCreateWithImageInRect(
      image, CGRectMake(x0, y0, clampedWidth, clampedHeight));
  CFRelease(image);
  if (cropped == nullptr) {
    error = "Unable to crop image";
    return {};
  }
  CGImageRef output = cropped;
  if (cropData.hasDisplaySize) {
    const int destWidth =
        std::max(1, static_cast<int>(std::lround(cropData.displayWidth)));
    const int destHeight =
        std::max(1, static_cast<int>(std::lround(cropData.displayHeight)));
    output = resizeImage(cropped, destWidth, destHeight, cropData.resizeMode);
    CFRelease(cropped);
    if (output == nullptr) {
      error = "Unable to crop image";
      return {};
    }
  }
  NSString* directory = NSTemporaryDirectory();
  if (directory == nil) {
    CFRelease(output);
    error = "Unable to write cropped image";
    return {};
  }
  NSString* name = [NSString
      stringWithFormat:@"rnsim-edit-%@.png", [[NSUUID UUID] UUIDString]];
  NSString* path = [directory stringByAppendingPathComponent:name];
  const bool written = writeImageAsPng(output, path);
  CFRelease(output);
  if (!written) {
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
      NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
      NSString* encoded = [data base64EncodedStringWithOptions:0];
      if (encoded == nil) {
        invokeStringCallback(jsInvoker, error, "failed to encode image");
        return;
      }
      invokeStringCallback(jsInvoker, success, stdString(encoded));
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
    NSData* data = [[NSData alloc]
        initWithBase64EncodedString:nsString(base64)
                            options:0];
    if (data == nil || data.length == 0) {
      invokeStringCallback(
          jsInvoker, error, "Failed to add image from base64String");
      return jsi::Value::undefined();
    }
    CGImageSourceRef source =
        CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr);
    if (source == nullptr) {
      invokeStringCallback(
          jsInvoker, error, "Failed to add image from base64String");
      return jsi::Value::undefined();
    }
    CFStringRef type = CGImageSourceGetType(source);
    const bool jpeg = type != nullptr &&
        CFStringCompare(
            type, CFSTR("public.jpeg"), kCFCompareCaseInsensitive) ==
            kCFCompareEqualTo;
    const bool png = type != nullptr &&
        CFStringCompare(
            type, CFSTR("public.png"), kCFCompareCaseInsensitive) ==
            kCFCompareEqualTo;
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (image == nullptr) {
      invokeStringCallback(
          jsInvoker, error, "Failed to add image from base64String");
      return jsi::Value::undefined();
    }
    NSString* directory = NSTemporaryDirectory();
    if (directory == nil) {
      CFRelease(image);
      invokeStringCallback(
          jsInvoker, error, "Failed to add image from base64String");
      return jsi::Value::undefined();
    }
    NSString* extension = jpeg ? @"jpeg" : @"png";
    NSString* name = [NSString
        stringWithFormat:@"rnsim-store-%@.%@",
                         [[NSUUID UUID] UUIDString],
                         extension];
    NSString* path = [directory stringByAppendingPathComponent:name];
    bool written = false;
    if (jpeg || png) {
      written = [data writeToFile:path atomically:YES];
    } else {
      written = writeImageAsPng(image, path);
    }
    CFRelease(image);
    if (!written) {
      invokeStringCallback(
          jsInvoker, error, "Failed to add image from base64String");
      return jsi::Value::undefined();
    }
    const auto tag = fileUriForPath(path);
    {
      std::lock_guard<std::mutex> lock(store->mutex);
      store->tags[tag] = stdString(path);
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
