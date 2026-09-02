#include "HeadlessRNModules.h"
#include "HostChrome.h"
#include "HostEnvironment.h"
#include "HostUi.h"
#include "HeadlessHttp.h"
#include "HeadlessI18n.h"
#include "HeadlessBlob.h"
#include "HeadlessImageAssets.h"
#include "HeadlessImageEditing.h"
#include "HeadlessIosModules.h"
#include "HeadlessBackPress.h"
#include "HeadlessKeyboard.h"
#include "HeadlessThinModules.h"
#include "HeadlessWebSocket.h"
#include "HostPlatform.h"

#include <react/nativemodule/core/ReactCommon/TurboModuleUtils.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace jsi = facebook::jsi;
namespace react = facebook::react;

namespace {
bool isAndroidProfile(const std::string& profile) {
  return profile.starts_with("android-");
}

jsi::Object makeDisplayMetrics(
    jsi::Runtime& runtime,
    float width,
    float height,
    float scale) {
  jsi::Object metrics(runtime);
  metrics.setProperty(runtime, "width", width);
  metrics.setProperty(runtime, "height", height);
  metrics.setProperty(runtime, "scale", scale);
  metrics.setProperty(runtime, "fontScale", 1);
  return metrics;
}

jsi::Object makePhysicalDisplayMetrics(
    jsi::Runtime& runtime,
    float width,
    float height,
    float scale) {
  jsi::Object metrics(runtime);
  metrics.setProperty(runtime, "width", width * scale);
  metrics.setProperty(runtime, "height", height * scale);
  metrics.setProperty(runtime, "scale", scale);
  metrics.setProperty(runtime, "fontScale", 1);
  metrics.setProperty(runtime, "densityDpi", 160.0f * scale);
  return metrics;
}

class PlatformConstantsAndroidRN73 final : public react::TurboModule {
 public:
  explicit PlatformConstantsAndroidRN73(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("PlatformConstants", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {
        0,
        [](jsi::Runtime& runtime,
           react::TurboModule&,
           const jsi::Value*,
           size_t) -> jsi::Value {
          jsi::Object version(runtime);
          version.setProperty(runtime, "major", 0);
          version.setProperty(runtime, "minor", 73);
          version.setProperty(runtime, "patch", 10);
          version.setProperty(runtime, "prerelease", jsi::Value::null());
          jsi::Object constants(runtime);
          constants.setProperty(runtime, "isTesting", false);
          constants.setProperty(runtime, "isDisableAnimations", false);
          constants.setProperty(
              runtime, "reactNativeVersion", std::move(version));
          constants.setProperty(runtime, "Version", 35);
          constants.setProperty(
              runtime, "Release", jsi::String::createFromAscii(runtime, "15"));
          constants.setProperty(
              runtime, "Serial", jsi::String::createFromAscii(runtime, "headless"));
          constants.setProperty(
              runtime,
              "Fingerprint",
              jsi::String::createFromUtf8(
                  runtime, "react-native-simulator/" + hostOsName()));
          constants.setProperty(
              runtime,
              "Model",
              jsi::String::createFromUtf8(
                  runtime, hostOsName() == "linux" ? "Linux" : "macOS"));
          constants.setProperty(
              runtime, "uiMode", jsi::String::createFromAscii(runtime, "normal"));
          constants.setProperty(
              runtime, "Brand", jsi::String::createFromAscii(runtime, "headless"));
          constants.setProperty(
              runtime,
              "Manufacturer",
              jsi::String::createFromAscii(runtime, "OpenAI"));
          return constants;
        }};
    methodMap_["getAndroidID"] = {
        0,
        [](jsi::Runtime& runtime,
           react::TurboModule&,
           const jsi::Value*,
           size_t) -> jsi::Value {
          return jsi::String::createFromAscii(
              runtime, "react-native-simulator");
        }};
  }
};

class PlatformConstantsAndroidRN87 final : public react::TurboModule {
 public:
  explicit PlatformConstantsAndroidRN87(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("PlatformConstants", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["getAndroidID"] = {0, &getAndroidID};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object version(runtime);
    version.setProperty(runtime, "major", 0);
    version.setProperty(runtime, "minor", 87);
    version.setProperty(runtime, "patch", 0);
    version.setProperty(runtime, "prerelease", jsi::Value::null());
    jsi::Object constants(runtime);
    constants.setProperty(runtime, "isTesting", false);
    constants.setProperty(runtime, "isDisableAnimations", false);
    constants.setProperty(runtime, "reactNativeVersion", std::move(version));
    constants.setProperty(runtime, "Version", 35);
    constants.setProperty(
        runtime, "Release", jsi::String::createFromAscii(runtime, "15"));
    constants.setProperty(
        runtime, "Serial", jsi::String::createFromAscii(runtime, "headless"));
    constants.setProperty(
        runtime,
        "Fingerprint",
        jsi::String::createFromUtf8(
            runtime, "react-native-simulator/" + hostOsName()));
    constants.setProperty(
        runtime,
        "Model",
        jsi::String::createFromUtf8(
            runtime, hostOsName() == "linux" ? "Linux" : "macOS"));
    constants.setProperty(
        runtime, "uiMode", jsi::String::createFromAscii(runtime, "normal"));
    constants.setProperty(
        runtime, "Brand", jsi::String::createFromAscii(runtime, "headless"));
    constants.setProperty(
        runtime,
        "Manufacturer",
        jsi::String::createFromAscii(runtime, "react-native-simulator"));
    return constants;
  }

  static jsi::Value getAndroidID(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::String::createFromAscii(runtime, "react-native-simulator");
  }

};

class PlatformConstantsIOSRN87 final : public react::TurboModule {
 public:
  explicit PlatformConstantsIOSRN87(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("PlatformConstants", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object version(runtime);
    version.setProperty(runtime, "major", 0);
    version.setProperty(runtime, "minor", 87);
    version.setProperty(runtime, "patch", 0);
    version.setProperty(runtime, "prerelease", jsi::Value::null());
    jsi::Object constants(runtime);
    constants.setProperty(runtime, "isTesting", false);
    constants.setProperty(runtime, "isDisableAnimations", false);
    constants.setProperty(runtime, "reactNativeVersion", std::move(version));
    constants.setProperty(runtime, "forceTouchAvailable", false);
    constants.setProperty(
        runtime, "osVersion", jsi::String::createFromUtf8(
            runtime, hostOsName() == "linux" ? "Linux" : "macOS"));
    constants.setProperty(
        runtime, "systemName", jsi::String::createFromAscii(runtime, "iOS"));
    constants.setProperty(
        runtime,
        "interfaceIdiom",
        jsi::String::createFromAscii(runtime, "phone"));
    constants.setProperty(runtime, "isMacCatalyst", false);
    return constants;
  }
};

class DeviceInfoModule final : public react::TurboModule {
 public:
  DeviceInfoModule(
      std::shared_ptr<react::CallInvoker> jsInvoker,
      HeadlessRNModuleHost host,
      bool androidShape)
      : TurboModule("DeviceInfo", std::move(jsInvoker)),
        host_(std::move(host)),
        androidShape_(androidShape) {
    methodMap_["getConstants"] = {0, &getConstants};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    const auto& self = static_cast<DeviceInfoModule&>(module);
    const auto& environment = hostEnvironment().state;
    const float width = environment.viewportWidth > 0
        ? environment.viewportWidth
        : self.host_.viewportWidth;
    const float height = environment.viewportHeight > 0
        ? environment.viewportHeight
        : self.host_.viewportHeight;
    const float scale = environment.pointScaleFactor > 0
        ? environment.pointScaleFactor
        : self.host_.pointScaleFactor;
    const float insetTop = environment.viewportWidth > 0
        ? environment.insetTop
        : self.host_.insetTop;
    const float insetBottom = environment.viewportWidth > 0
        ? environment.insetBottom
        : self.host_.insetBottom;
    const float screenHeight =
        height + std::max(0.0f, insetTop) + std::max(0.0f, insetBottom);
    jsi::Object dimensions(runtime);
    dimensions.setProperty(
        runtime,
        "window",
        makeDisplayMetrics(runtime, width, height, scale));
    dimensions.setProperty(
        runtime,
        "screen",
        makeDisplayMetrics(runtime, width, screenHeight, scale));
    if (self.androidShape_) {
      dimensions.setProperty(
          runtime,
          "windowPhysicalPixels",
          makePhysicalDisplayMetrics(runtime, width, height, scale));
      dimensions.setProperty(
          runtime,
          "screenPhysicalPixels",
          makePhysicalDisplayMetrics(runtime, width, screenHeight, scale));
    }
    jsi::Object constants(runtime);
    constants.setProperty(runtime, "Dimensions", std::move(dimensions));
    return constants;
  }

  HeadlessRNModuleHost host_;
  bool androidShape_{false};
};

class RNCSafeAreaContextModule final : public react::TurboModule {
 public:
  RNCSafeAreaContextModule(
      std::shared_ptr<react::CallInvoker> jsInvoker,
      HeadlessRNModuleHost host)
      : TurboModule("RNCSafeAreaContext", std::move(jsInvoker)),
        host_(std::move(host)) {
    methodMap_["getConstants"] = {0, &getConstants};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    const auto& self = static_cast<RNCSafeAreaContextModule&>(module);
    const auto& environment = hostEnvironment().state;
    const float width = environment.viewportWidth > 0
        ? environment.viewportWidth
        : self.host_.viewportWidth;
    const float height = environment.viewportHeight > 0
        ? environment.viewportHeight
        : self.host_.viewportHeight;
    jsi::Object insets(runtime);
    // Host status/nav chrome is reserved around the Fabric window, not inside
    // it. A root SafeAreaProvider therefore reports no notch/nav overlap.
    insets.setProperty(runtime, "top", 0);
    insets.setProperty(runtime, "right", 0);
    insets.setProperty(runtime, "bottom", 0);
    insets.setProperty(runtime, "left", 0);
    jsi::Object frame(runtime);
    frame.setProperty(runtime, "x", 0);
    frame.setProperty(runtime, "y", 0);
    frame.setProperty(runtime, "width", width);
    frame.setProperty(runtime, "height", height);
    jsi::Object metrics(runtime);
    metrics.setProperty(runtime, "insets", std::move(insets));
    metrics.setProperty(runtime, "frame", std::move(frame));
    jsi::Object constants(runtime);
    constants.setProperty(
        runtime, "initialWindowMetrics", std::move(metrics));
    return constants;
  }

  HeadlessRNModuleHost host_;
};

class SourceCodeModule final : public react::TurboModule {
 public:
  SourceCodeModule(
      std::shared_ptr<react::CallInvoker> jsInvoker,
      std::string scriptURL)
      : TurboModule("SourceCode", std::move(jsInvoker)),
        scriptURL_(std::move(scriptURL)) {
    methodMap_["getConstants"] = {0, &getConstants};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    const auto& self = static_cast<SourceCodeModule&>(module);
    jsi::Object constants(runtime);
    constants.setProperty(
        runtime,
        "scriptURL",
        jsi::String::createFromUtf8(runtime, self.scriptURL_));
    return constants;
  }

  std::string scriptURL_;
};

class NetworkingAndroidRN73 final : public react::TurboModule {
 public:
  explicit NetworkingAndroidRN73(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("Networking", std::move(jsInvoker)) {
    methodMap_["sendRequest"] = {9, &sendRequest};
    methodMap_["abortRequest"] = {1, &abortRequest};
    methodMap_["addListener"] = {1, &noop};
    methodMap_["removeListeners"] = {1, &noop};
    methodMap_["clearCookies"] = {1, &clearCookies};
  }

 private:
  static NetworkingAndroidRN73& self(react::TurboModule& module) {
    return static_cast<NetworkingAndroidRN73&>(module);
  }

  static jsi::Value noop(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Value::undefined();
  }

  static jsi::Value clearCookies(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isObject() &&
        args[0].getObject(runtime).isFunction(runtime)) {
      args[0].getObject(runtime).getFunction(runtime).call(runtime, false);
    }
    return jsi::Value::undefined();
  }

  static jsi::Value abortRequest(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isNumber()) {
      headlessHttpAbort(static_cast<int>(args[0].getNumber()));
    }
    return jsi::Value::undefined();
  }

  static std::string stringProp(
      jsi::Runtime& runtime,
      const jsi::Object& object,
      const char* name) {
    const auto value = object.getProperty(runtime, name);
    if (!value.isString()) {
      return {};
    }
    return value.getString(runtime).utf8(runtime);
  }

  static jsi::Value sendRequest(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    if (count < 3 || !args[2].isNumber()) {
      return jsi::Value::undefined();
    }
    HeadlessHttpRequest request;
    request.method = args[0].isString()
        ? args[0].getString(runtime).utf8(runtime)
        : "GET";
    request.url = args[1].isString()
        ? args[1].getString(runtime).utf8(runtime)
        : "";
    request.requestId = static_cast<int>(args[2].getNumber());
    if (count > 3 && args[3].isObject() &&
        args[3].getObject(runtime).isArray(runtime)) {
      auto headers = args[3].getObject(runtime).getArray(runtime);
      const auto size = headers.size(runtime);
      for (size_t index = 0; index < size; ++index) {
        const auto item = headers.getValueAtIndex(runtime, index);
        if (!item.isObject() || !item.getObject(runtime).isArray(runtime)) {
          continue;
        }
        auto pair = item.getObject(runtime).getArray(runtime);
        if (pair.size(runtime) < 2 || !pair.getValueAtIndex(runtime, 0).isString() ||
            !pair.getValueAtIndex(runtime, 1).isString()) {
          continue;
        }
        request.headers.emplace_back(
            pair.getValueAtIndex(runtime, 0).getString(runtime).utf8(runtime),
            pair.getValueAtIndex(runtime, 1).getString(runtime).utf8(runtime));
      }
    }
    if (count > 4 && args[4].isObject()) {
      auto data = args[4].getObject(runtime);
      request.body = stringProp(runtime, data, "string");
      if (request.body.empty()) {
        const auto base64 = stringProp(runtime, data, "base64");
        if (!base64.empty()) {
          request.body = headlessBlobBase64Decode(base64);
        }
      }
      if (request.body.empty()) {
        const auto blobValue = data.getProperty(runtime, "blob");
        if (blobValue.isObject()) {
          auto blob = blobValue.getObject(runtime);
          std::string blobId;
          const auto idValue = blob.getProperty(runtime, "blobId");
          if (idValue.isString()) {
            blobId = idValue.getString(runtime).utf8(runtime);
          }
          const int offset =
              blob.getProperty(runtime, "offset").isNumber()
              ? static_cast<int>(
                    blob.getProperty(runtime, "offset").getNumber())
              : 0;
          const int size = blob.getProperty(runtime, "size").isNumber()
              ? static_cast<int>(blob.getProperty(runtime, "size").getNumber())
              : -1;
          if (auto bytes = headlessBlobResolve(blobId, offset, size)) {
            request.body = std::move(*bytes);
          }
        }
      }
    }
    const auto responseType = count > 5 && args[5].isString()
        ? args[5].getString(runtime).utf8(runtime)
        : std::string("text");
    if (count > 7 && args[7].isNumber()) {
      request.timeoutMs = args[7].getNumber();
    }
    auto* networking = &self(module);
    auto jsInvoker = networking->jsInvoker_;
    const int requestId = request.requestId;
    headlessHttpSend(
        std::move(request),
        [networking, jsInvoker, responseType, requestId](
            HeadlessHttpResponse response) {
          jsInvoker->invokeAsync(
              [networking,
               responseType,
               requestId,
               response = std::move(response)](jsi::Runtime&) mutable {
                networking->emitResponse(
                    requestId, responseType, std::move(response));
              });
        });
    return jsi::Value::undefined();
  }

  void emitResponse(
      int requestId,
      std::string responseType,
      HeadlessHttpResponse response) {
    emitDeviceEvent(
        "didReceiveNetworkResponse",
        [requestId, status = response.status, url = response.url,
         headers = response.headers](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object headerObject(runtime);
          for (const auto& header : headers) {
            headerObject.setProperty(
                runtime,
                header.first.c_str(),
                jsi::String::createFromUtf8(runtime, header.second));
          }
          jsi::Array payload(runtime, 4);
          payload.setValueAtIndex(runtime, 0, requestId);
          payload.setValueAtIndex(runtime, 1, status);
          payload.setValueAtIndex(runtime, 2, std::move(headerObject));
          payload.setValueAtIndex(
              runtime,
              3,
              jsi::String::createFromUtf8(runtime, url));
          args.emplace_back(std::move(payload));
        });
    if (response.error.empty()) {
      auto body = std::move(response.body);
      emitDeviceEvent(
          "didReceiveNetworkData",
          [requestId, body = std::move(body), responseType](
              jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
            jsi::Array payload(runtime, 2);
            payload.setValueAtIndex(runtime, 0, requestId);
            if (responseType == "blob" &&
                headlessBlobNetworkingHandlerEnabled()) {
              const auto blobId = headlessBlobStore(body);
              jsi::Object blob(runtime);
              blob.setProperty(
                  runtime,
                  "blobId",
                  jsi::String::createFromUtf8(runtime, blobId));
              blob.setProperty(runtime, "offset", 0);
              blob.setProperty(
                  runtime, "size", static_cast<int>(body.size()));
              payload.setValueAtIndex(runtime, 1, std::move(blob));
            } else if (responseType == "base64") {
              payload.setValueAtIndex(
                  runtime,
                  1,
                  jsi::String::createFromUtf8(
                      runtime, headlessBlobBase64Encode(body)));
            } else {
              payload.setValueAtIndex(
                  runtime,
                  1,
                  jsi::String::createFromUtf8(runtime, body));
            }
            args.emplace_back(std::move(payload));
          });
    }
    emitDeviceEvent(
        "didCompleteNetworkResponse",
        [requestId, error = response.error, timeout = response.timeout](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Array payload(runtime, 3);
          payload.setValueAtIndex(runtime, 0, requestId);
          payload.setValueAtIndex(
              runtime,
              1,
              jsi::String::createFromUtf8(runtime, error));
          payload.setValueAtIndex(runtime, 2, timeout);
          args.emplace_back(std::move(payload));
        });
  }
};

class ImageLoaderAndroidRN73 final : public react::TurboModule {
 public:
  ImageLoaderAndroidRN73(
      std::shared_ptr<react::CallInvoker> jsInvoker,
      std::filesystem::path assetDirectory)
      : TurboModule("ImageLoader", std::move(jsInvoker)),
        assetDirectory_(std::move(assetDirectory)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["abortRequest"] = {1, &abortRequest};
    methodMap_["getSize"] = {1, &getSize};
    methodMap_["getSizeWithHeaders"] = {2, &getSize};
    methodMap_["prefetchImage"] = {2, &prefetchImage};
    methodMap_["queryCache"] = {1, &queryCache};
  }

 private:
  std::filesystem::path assetDirectory_;

  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Object(runtime);
  }

  static jsi::Value abortRequest(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Value::undefined();
  }

  static jsi::Value getSize(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    std::string uri;
    if (count > 0 && args[0].isString()) {
      uri = args[0].getString(runtime).utf8(runtime);
    }
    const auto& loader = static_cast<ImageLoaderAndroidRN73&>(module);
    const auto path = resolveHeadlessLocalImage(uri, loader.assetDirectory_);
    if (const auto size = headlessLocalImagePixelSize(path)) {
      const int width = size->first;
      const int height = size->second;
      return react::createPromiseAsJSIValue(
          runtime,
          [width, height](
              jsi::Runtime& runtime, std::shared_ptr<react::Promise> promise) {
            jsi::Object dimensions(runtime);
            dimensions.setProperty(runtime, "width", width);
            dimensions.setProperty(runtime, "height", height);
            promise->resolve(std::move(dimensions));
          });
    }
    const auto cached = headlessCachedImagePath(uri);
    if (const auto size = headlessLocalImagePixelSize(cached)) {
      const int width = size->first;
      const int height = size->second;
      return react::createPromiseAsJSIValue(
          runtime,
          [width, height](
              jsi::Runtime& runtime, std::shared_ptr<react::Promise> promise) {
            jsi::Object dimensions(runtime);
            dimensions.setProperty(runtime, "width", width);
            dimensions.setProperty(runtime, "height", height);
            promise->resolve(std::move(dimensions));
          });
    }
    if (uri.starts_with("http://") || uri.starts_with("https://")) {
      auto jsInvoker = loader.jsInvoker_;
      return react::createPromiseAsJSIValue(
          runtime,
          [uri, jsInvoker](
              jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
            headlessFetchImage(
                uri,
                [promise, jsInvoker](
                    std::filesystem::path path, std::string) mutable {
                  const auto size = headlessLocalImagePixelSize(path);
                  jsInvoker->invokeAsync(
                      [promise = std::move(promise), size](
                          jsi::Runtime& runtime) {
                        if (!size) {
                          promise->reject("Failed to get size for image");
                          return;
                        }
                        jsi::Object dimensions(runtime);
                        dimensions.setProperty(runtime, "width", size->first);
                        dimensions.setProperty(runtime, "height", size->second);
                        promise->resolve(std::move(dimensions));
                      });
                });
          });
    }
    return react::createPromiseAsJSIValue(
        runtime,
        [](jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          promise->reject("Failed to get size for image");
        });
  }

  static jsi::Value prefetchImage(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    std::string uri;
    if (count > 0 && args[0].isString()) {
      uri = args[0].getString(runtime).utf8(runtime);
    }
    auto jsInvoker = static_cast<ImageLoaderAndroidRN73&>(module).jsInvoker_;
    return react::createPromiseAsJSIValue(
        runtime,
        [uri, jsInvoker](
            jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          if (uri.empty()) {
            promise->resolve(false);
            return;
          }
          headlessPrefetchImage(uri, [promise, jsInvoker, uri]() mutable {
            const bool cached = !headlessCachedImagePath(uri).empty();
            jsInvoker->invokeAsync(
                [promise = std::move(promise), cached](jsi::Runtime&) {
                  promise->resolve(cached);
                });
          });
        });
  }

  static jsi::Value queryCache(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    std::vector<std::string> uris;
    if (count > 0 && args[0].isObject() &&
        args[0].getObject(runtime).isArray(runtime)) {
      auto array = args[0].getObject(runtime).getArray(runtime);
      const auto size = array.size(runtime);
      for (size_t index = 0; index < size; ++index) {
        const auto value = array.getValueAtIndex(runtime, index);
        if (value.isString()) {
          uris.push_back(value.getString(runtime).utf8(runtime));
        }
      }
    }
    const auto assetDirectory =
        static_cast<ImageLoaderAndroidRN73&>(module).assetDirectory_;
    return react::createPromiseAsJSIValue(
        runtime,
        [uris = std::move(uris), assetDirectory](
            jsi::Runtime& runtime, std::shared_ptr<react::Promise> promise) {
          jsi::Object map(runtime);
          for (const auto& uri : uris) {
            const auto local = resolveHeadlessLocalImage(uri, assetDirectory);
            if (!local.empty() || !headlessCachedImagePath(uri).empty()) {
              map.setProperty(
                  runtime,
                  uri.c_str(),
                  jsi::String::createFromAscii(runtime, "disk"));
            }
          }
          promise->resolve(std::move(map));
        });
  }
};

class KeyboardObserverModule final : public react::TurboModule {
 public:
  explicit KeyboardObserverModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("KeyboardObserver", std::move(jsInvoker)) {
    methodMap_["addListener"] = {1, &noop};
    methodMap_["removeListeners"] = {1, &noop};
  }

  void emitVisibility(bool show) {
    const auto height = show ? headlessKeyboard().keyboardHeight : 0.0f;
    const auto screenY = headlessKeyboard().viewportHeight - height;
    const auto width = headlessKeyboard().viewportWidth;
    emitDeviceEvent(
        show ? "keyboardDidShow" : "keyboardDidHide",
        [height, screenY, width](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object coords(runtime);
          coords.setProperty(runtime, "screenX", 0);
          coords.setProperty(runtime, "screenY", screenY);
          coords.setProperty(runtime, "width", width);
          coords.setProperty(runtime, "height", height);
          jsi::Object event(runtime);
          event.setProperty(runtime, "duration", 0);
          event.setProperty(
              runtime,
              "easing",
              jsi::String::createFromAscii(runtime, "keyboard"));
          event.setProperty(runtime, "endCoordinates", std::move(coords));
          args.emplace_back(std::move(event));
        });
  }

 private:
  static jsi::Value noop(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Value::undefined();
  }
};

class EventedNoopModule final : public react::TurboModule {
 public:
  EventedNoopModule(
      std::string name,
      std::shared_ptr<react::CallInvoker> jsInvoker,
      std::initializer_list<std::pair<const char*, size_t>> methods)
      : TurboModule(std::move(name), std::move(jsInvoker)) {
    for (const auto& [method, argumentCount] : methods) {
      addNoop(method, argumentCount);
    }
    addNoop("addListener", 1);
    addNoop("removeListeners", 1);
  }

 private:
  void addNoop(const char* name, size_t argumentCount) {
    methodMap_[name] = {
        argumentCount,
        [](jsi::Runtime&,
           react::TurboModule&,
           const jsi::Value*,
           size_t) -> jsi::Value { return jsi::Value::undefined(); }};
  }
};

class StatusBarManagerAndroid final : public react::TurboModule {
 public:
  explicit StatusBarManagerAndroid(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("StatusBarManager", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    addNoop("setColor", 2);
    addNoop("setTranslucent", 1);
    methodMap_["setStyle"] = {1, &setStyle};
    methodMap_["setHidden"] = {1, &setHidden};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object constants(runtime);
    constants.setProperty(runtime, "HEIGHT", hostChrome().statusBarHeight);
    constants.setProperty(runtime, "DEFAULT_BACKGROUND_COLOR", 0);
    return constants;
  }

  static jsi::Value setStyle(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isString()) {
      hostChrome().setStatusBarStyle(
          args[0].getString(runtime).utf8(runtime));
    }
    return jsi::Value::undefined();
  }

  static jsi::Value setHidden(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isBool()) {
      hostChrome().setStatusBarHidden(args[0].getBool());
    }
    return jsi::Value::undefined();
  }

  void addNoop(const char* name, size_t argumentCount) {
    methodMap_[name] = {
        argumentCount,
        [](jsi::Runtime&,
           react::TurboModule&,
           const jsi::Value*,
           size_t) -> jsi::Value { return jsi::Value::undefined(); }};
  }
};

class SegmentFetcherModule final : public react::TurboModule {
 public:
  explicit SegmentFetcherModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("SegmentFetcher", std::move(jsInvoker)) {
    methodMap_["fetchSegment"] = {3, &fetchSegment};
    methodMap_["getSegment"] = {3, &getSegment};
  }

 private:
  static jsi::Value fetchSegment(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 2 && args[2].isObject() &&
        args[2].getObject(runtime).isFunction(runtime)) {
      args[2].getObject(runtime).getFunction(runtime).call(
          runtime, jsi::Value::null());
    }
    return jsi::Value::undefined();
  }

  static jsi::Value getSegment(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 2 && args[2].isObject() &&
        args[2].getObject(runtime).isFunction(runtime)) {
      args[2].getObject(runtime).getFunction(runtime).call(
          runtime,
          jsi::Value::null(),
          jsi::String::createFromAscii(runtime, ""));
    }
    return jsi::Value::undefined();
  }
};

class ClipboardModule final : public react::TurboModule {
 public:
  explicit ClipboardModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("Clipboard", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["getString"] = {0, &getString};
    methodMap_["setString"] = {1, &setString};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Object(runtime);
  }

  static jsi::Value getString(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    auto contents = headlessClipboardGet();
    return react::createPromiseAsJSIValue(
        runtime,
        [contents = std::move(contents)](
            jsi::Runtime& runtime, std::shared_ptr<react::Promise> promise) {
          promise->resolve(jsi::String::createFromUtf8(runtime, contents));
        });
  }

  static jsi::Value setString(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isString()) {
      headlessClipboardSet(args[0].getString(runtime).utf8(runtime));
    } else {
      headlessClipboardSet({});
    }
    return jsi::Value::undefined();
  }
};

class ToastAndroidModule final : public react::TurboModule {
 public:
  explicit ToastAndroidModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ToastAndroid", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["show"] = {2, &show};
    methodMap_["showWithGravity"] = {3, &showWithGravity};
    methodMap_["showWithGravityAndOffset"] = {5, &showWithGravityAndOffset};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object constants(runtime);
    constants.setProperty(runtime, "SHORT", 2000);
    constants.setProperty(runtime, "LONG", 3500);
    constants.setProperty(runtime, "TOP", 49);
    constants.setProperty(runtime, "BOTTOM", 81);
    constants.setProperty(runtime, "CENTER", 17);
    return constants;
  }

  static void present(
      jsi::Runtime& runtime,
      const jsi::Value* args,
      size_t count) {
    std::string message;
    int duration = 2000;
    int gravity = 81;
    float xOffset = 0;
    float yOffset = 0;
    if (count > 0 && args[0].isString()) {
      message = args[0].getString(runtime).utf8(runtime);
    }
    if (count > 1 && args[1].isNumber()) {
      duration = static_cast<int>(args[1].getNumber());
    }
    if (count > 2 && args[2].isNumber()) {
      gravity = static_cast<int>(args[2].getNumber());
    }
    if (count > 3 && args[3].isNumber()) {
      xOffset = static_cast<float>(args[3].getNumber());
    }
    if (count > 4 && args[4].isNumber()) {
      yOffset = static_cast<float>(args[4].getNumber());
    }
    hostChrome().showToast(
        std::move(message), duration, gravity, xOffset, yOffset);
  }

  static jsi::Value show(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    present(runtime, args, count);
    return jsi::Value::undefined();
  }

  static jsi::Value showWithGravity(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    present(runtime, args, count);
    return jsi::Value::undefined();
  }

  static jsi::Value showWithGravityAndOffset(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    present(runtime, args, count);
    return jsi::Value::undefined();
  }
};

class I18nManagerModule final : public react::TurboModule {
 public:
  explicit I18nManagerModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("I18nManager", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["allowRTL"] = {1, &allowRTL};
    methodMap_["forceRTL"] = {1, &forceRTL};
    methodMap_["swapLeftAndRightInRTL"] = {1, &swapLeftAndRightInRTL};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object constants(runtime);
    constants.setProperty(runtime, "isRTL", headlessI18n().isRTL());
    constants.setProperty(
        runtime,
        "doLeftAndRightSwapInRTL",
        headlessI18n().doLeftAndRightSwapInRTL);
    constants.setProperty(
        runtime,
        "localeIdentifier",
        jsi::String::createFromAscii(runtime, "en_US"));
    return constants;
  }

  static jsi::Value allowRTL(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isBool()) {
      headlessI18n().allowRTL = args[0].getBool();
    }
    return jsi::Value::undefined();
  }

  static jsi::Value forceRTL(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isBool()) {
      headlessI18n().forceRTL = args[0].getBool();
    }
    return jsi::Value::undefined();
  }

  static jsi::Value swapLeftAndRightInRTL(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count > 0 && args[0].isBool()) {
      headlessI18n().doLeftAndRightSwapInRTL = args[0].getBool();
    }
    return jsi::Value::undefined();
  }
};

class VibrationModule final : public react::TurboModule {
 public:
  explicit VibrationModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("Vibration", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["vibrate"] = {1, &vibrate};
    methodMap_["vibrateByPattern"] = {2, &vibrate};
    methodMap_["cancel"] = {0, &cancel};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Object(runtime);
  }

  static jsi::Value vibrate(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    int durationMs = 400;
    if (count > 0 && args[0].isNumber()) {
      durationMs = static_cast<int>(args[0].getNumber());
    }
    if (durationMs < 1) {
      durationMs = 1;
    }
    hostUi().presentVibration({.durationMs = durationMs});
    return jsi::Value::undefined();
  }

  static jsi::Value cancel(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    hostUi().dismissVibration();
    return jsi::Value::undefined();
  }
};

std::string hostUiString(
    jsi::Runtime& runtime,
    const jsi::Object& object,
    const char* name) {
  const auto value = object.getProperty(runtime, name);
  if (!value.isString()) {
    return {};
  }
  return value.getString(runtime).utf8(runtime);
}

bool hostUiBool(
    jsi::Runtime& runtime,
    const jsi::Object& object,
    const char* name,
    bool fallback) {
  const auto value = object.getProperty(runtime, name);
  if (value.isBool()) {
    return value.getBool();
  }
  return fallback;
}

void completeOpenUrlPromise(
    const std::shared_ptr<react::CallInvoker>& jsInvoker,
    std::shared_ptr<react::Promise> promise,
    bool opened,
    std::string error) {
  jsInvoker->invokeAsync(
      [promise = std::move(promise), opened, error = std::move(error)](
          jsi::Runtime&) {
        if (opened) {
          promise->resolve(true);
        } else {
          promise->reject(error);
        }
      });
}

class IntentAndroidModule final : public react::TurboModule {
 public:
  explicit IntentAndroidModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("IntentAndroid", std::move(jsInvoker)) {
    methodMap_["getInitialURL"] = {0, &getInitialURL};
    methodMap_["canOpenURL"] = {1, &canOpenURL};
    methodMap_["openURL"] = {1, &openURL};
    methodMap_["openSettings"] = {0, &openSettings};
    methodMap_["sendIntent"] = {2, &sendIntent};
  }

 private:
  static jsi::Value getInitialURL(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return react::createPromiseAsJSIValue(
        runtime,
        [](jsi::Runtime& runtime, std::shared_ptr<react::Promise> promise) {
          if (const char* url = std::getenv("RNSIM_INITIAL_URL")) {
            promise->resolve(jsi::String::createFromUtf8(runtime, url));
            return;
          }
          promise->resolve(jsi::Value::null());
        });
  }

  static IntentAndroidModule& self(react::TurboModule& module) {
    return static_cast<IntentAndroidModule&>(module);
  }

  static jsi::Value canOpenURL(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return react::createPromiseAsJSIValue(
        runtime,
        [](jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          promise->resolve(false);
        });
  }

  static jsi::Value openURL(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    std::string url;
    if (count > 0 && args[0].isString()) {
      url = args[0].getString(runtime).utf8(runtime);
    }
    auto jsInvoker = self(module).jsInvoker_;
    return react::createPromiseAsJSIValue(
        runtime,
        [url = std::move(url), jsInvoker](
            jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          hostUi().presentOpenUrl(
              {.url = url, .settings = false},
              [jsInvoker, promise](bool opened, std::string error) {
                completeOpenUrlPromise(
                    jsInvoker, promise, opened, std::move(error));
              });
        });
  }

  static jsi::Value openSettings(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    auto jsInvoker = self(module).jsInvoker_;
    return react::createPromiseAsJSIValue(
        runtime,
        [jsInvoker](jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          hostUi().presentOpenUrl(
              {.url = {}, .settings = true},
              [jsInvoker, promise](bool opened, std::string error) {
                completeOpenUrlPromise(
                    jsInvoker, promise, opened, std::move(error));
              });
        });
  }

  static jsi::Value sendIntent(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    std::string action;
    if (count > 0 && args[0].isString()) {
      action = args[0].getString(runtime).utf8(runtime);
    }
    auto jsInvoker = self(module).jsInvoker_;
    return react::createPromiseAsJSIValue(
        runtime,
        [action = std::move(action), jsInvoker](
            jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          hostUi().presentOpenUrl(
              {.url = action, .settings = false},
              [jsInvoker, promise](bool opened, std::string error) {
                completeOpenUrlPromise(
                    jsInvoker, promise, opened, std::move(error));
              });
        });
  }
};

class LinkingManagerModule final : public react::TurboModule {
 public:
  explicit LinkingManagerModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("LinkingManager", std::move(jsInvoker)) {
    methodMap_["getInitialURL"] = {0, &getInitialURL};
    methodMap_["canOpenURL"] = {1, &canOpenURL};
    methodMap_["openURL"] = {1, &openURL};
    methodMap_["openSettings"] = {0, &openSettings};
    addNoop("addListener", 1);
    addNoop("removeListeners", 1);
  }

 private:
  static jsi::Value getInitialURL(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return react::createPromiseAsJSIValue(
        runtime,
        [](jsi::Runtime& runtime, std::shared_ptr<react::Promise> promise) {
          if (const char* url = std::getenv("RNSIM_INITIAL_URL")) {
            promise->resolve(jsi::String::createFromUtf8(runtime, url));
            return;
          }
          promise->resolve(jsi::Value::null());
        });
  }

  static jsi::Value canOpenURL(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return react::createPromiseAsJSIValue(
        runtime,
        [](jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          promise->resolve(false);
        });
  }

  static LinkingManagerModule& self(react::TurboModule& module) {
    return static_cast<LinkingManagerModule&>(module);
  }

  static jsi::Value openURL(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    std::string url;
    if (count > 0 && args[0].isString()) {
      url = args[0].getString(runtime).utf8(runtime);
    }
    auto jsInvoker = self(module).jsInvoker_;
    return react::createPromiseAsJSIValue(
        runtime,
        [url = std::move(url), jsInvoker](
            jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          hostUi().presentOpenUrl(
              {.url = url, .settings = false},
              [jsInvoker, promise](bool opened, std::string error) {
                completeOpenUrlPromise(
                    jsInvoker, promise, opened, std::move(error));
              });
        });
  }

  static jsi::Value openSettings(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value*,
      size_t) {
    auto jsInvoker = self(module).jsInvoker_;
    return react::createPromiseAsJSIValue(
        runtime,
        [jsInvoker](jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          hostUi().presentOpenUrl(
              {.url = {}, .settings = true},
              [jsInvoker, promise](bool opened, std::string error) {
                completeOpenUrlPromise(
                    jsInvoker, promise, opened, std::move(error));
              });
        });
  }

  void addNoop(const char* name, size_t argumentCount) {
    methodMap_[name] = {
        argumentCount,
        [](jsi::Runtime&,
           react::TurboModule&,
           const jsi::Value*,
           size_t) -> jsi::Value { return jsi::Value::undefined(); }};
  }
};

class PermissionsAndroidModule final : public react::TurboModule {
 public:
  explicit PermissionsAndroidModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("PermissionsAndroid", std::move(jsInvoker)) {
    methodMap_["checkPermission"] = {1, &checkPermission};
    methodMap_["requestPermission"] = {1, &requestPermission};
    methodMap_["shouldShowRequestPermissionRationale"] = {
        1, &shouldShowRationale};
    methodMap_["requestMultiplePermissions"] = {
        1, &requestMultiplePermissions};
  }

 private:
  static PermissionsAndroidModule& self(react::TurboModule& module) {
    return static_cast<PermissionsAndroidModule&>(module);
  }

  static std::vector<std::string> permissionList(
      jsi::Runtime& runtime,
      const jsi::Value* args,
      size_t count) {
    std::vector<std::string> permissions;
    if (count == 0) {
      return permissions;
    }
    if (args[0].isString()) {
      permissions.push_back(args[0].getString(runtime).utf8(runtime));
      return permissions;
    }
    if (args[0].isObject() && args[0].getObject(runtime).isArray(runtime)) {
      auto array = args[0].getObject(runtime).getArray(runtime);
      const auto length = array.size(runtime);
      permissions.reserve(length);
      for (size_t index = 0; index < length; ++index) {
        auto item = array.getValueAtIndex(runtime, index);
        if (item.isString()) {
          permissions.push_back(item.getString(runtime).utf8(runtime));
        }
      }
    }
    return permissions;
  }

  static jsi::Value checkPermission(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    std::string permission;
    if (count > 0 && args[0].isString()) {
      permission = args[0].getString(runtime).utf8(runtime);
    }
    const bool granted = hostUi().checkPermissionGranted(permission);
    return react::createPromiseAsJSIValue(
        runtime,
        [granted](jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          promise->resolve(granted);
        });
  }

  static jsi::Value requestPermission(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    auto permissions = permissionList(runtime, args, count);
    auto jsInvoker = self(module).jsInvoker_;
    return react::createPromiseAsJSIValue(
        runtime,
        [permissions = std::move(permissions), jsInvoker](
            jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          hostUi().presentPermission(
              {.permissions = permissions},
              [jsInvoker, promise](
                  std::vector<std::pair<std::string, std::string>> results) {
                std::string status = "granted";
                if (!results.empty()) {
                  status = results.front().second;
                }
                jsInvoker->invokeAsync(
                    [promise, status](jsi::Runtime& runtime) {
                      promise->resolve(jsi::String::createFromUtf8(
                          runtime, status));
                    });
              });
        });
  }

  static jsi::Value shouldShowRationale(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return react::createPromiseAsJSIValue(
        runtime,
        [](jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          promise->resolve(false);
        });
  }

  static jsi::Value requestMultiplePermissions(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    auto permissions = permissionList(runtime, args, count);
    auto jsInvoker = self(module).jsInvoker_;
    return react::createPromiseAsJSIValue(
        runtime,
        [permissions = std::move(permissions), jsInvoker](
            jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          hostUi().presentPermission(
              {.permissions = permissions},
              [jsInvoker, promise](
                  std::vector<std::pair<std::string, std::string>> results) {
                jsInvoker->invokeAsync(
                    [promise, results = std::move(results)](
                        jsi::Runtime& runtime) {
                      jsi::Object map(runtime);
                      for (const auto& [permission, status] : results) {
                        map.setProperty(
                            runtime,
                            permission.c_str(),
                            jsi::String::createFromUtf8(runtime, status));
                      }
                      promise->resolve(std::move(map));
                    });
              });
        });
  }
};

class DialogManagerAndroidModule final : public react::TurboModule {
 public:
  explicit DialogManagerAndroidModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("DialogManagerAndroid", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["showAlert"] = {3, &showAlert};
  }

 private:
  static DialogManagerAndroidModule& self(react::TurboModule& module) {
    return static_cast<DialogManagerAndroidModule&>(module);
  }

  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object constants(runtime);
    constants.setProperty(
        runtime,
        "buttonClicked",
        jsi::String::createFromAscii(runtime, "buttonClicked"));
    constants.setProperty(
        runtime,
        "dismissed",
        jsi::String::createFromAscii(runtime, "dismissed"));
    constants.setProperty(runtime, "buttonPositive", -1);
    constants.setProperty(runtime, "buttonNegative", -2);
    constants.setProperty(runtime, "buttonNeutral", -3);
    return constants;
  }

  static jsi::Value showAlert(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    HostUiAlert spec;
    if (count > 0 && args[0].isObject()) {
      auto config = args[0].getObject(runtime);
      spec.title = hostUiString(runtime, config, "title");
      spec.message = hostUiString(runtime, config, "message");
      spec.buttonPositive = hostUiString(runtime, config, "buttonPositive");
      spec.buttonNegative = hostUiString(runtime, config, "buttonNegative");
      spec.buttonNeutral = hostUiString(runtime, config, "buttonNeutral");
      spec.cancelable = hostUiBool(runtime, config, "cancelable", true);
      const auto items = config.getProperty(runtime, "items");
      if (items.isObject() && items.getObject(runtime).isArray(runtime)) {
        auto array = items.getObject(runtime).getArray(runtime);
        const auto length = array.size(runtime);
        spec.items.reserve(length);
        for (size_t index = 0; index < length; ++index) {
          auto item = array.getValueAtIndex(runtime, index);
          if (item.isString()) {
            spec.items.push_back(item.getString(runtime).utf8(runtime));
          }
        }
      }
    }
    if (spec.buttonPositive.empty() && spec.items.empty()) {
      spec.buttonPositive = "OK";
    }
    std::shared_ptr<jsi::Function> onAction;
    if (count > 2 && args[2].isObject() &&
        args[2].getObject(runtime).isFunction(runtime)) {
      onAction = std::make_shared<jsi::Function>(
          args[2].getObject(runtime).getFunction(runtime));
    }
    auto jsInvoker = self(module).jsInvoker_;
    hostUi().presentAlert(
        std::move(spec),
        [jsInvoker, onAction](HostUiAlertResult result) {
          if (!onAction) {
            return;
          }
          jsInvoker->invokeAsync(
              [onAction, result](jsi::Runtime& runtime) {
                if (result.dismissed) {
                  onAction->call(
                      runtime,
                      jsi::String::createFromAscii(runtime, "dismissed"));
                  return;
                }
                onAction->call(
                    runtime,
                    jsi::String::createFromAscii(runtime, "buttonClicked"),
                    result.buttonKey);
              });
        });
    return jsi::Value::undefined();
  }
};

class ShareModule final : public react::TurboModule {
 public:
  explicit ShareModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("ShareModule", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["share"] = {2, &share};
  }

 private:
  static ShareModule& self(react::TurboModule& module) {
    return static_cast<ShareModule&>(module);
  }

  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Object(runtime);
  }

  static jsi::Value share(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    HostUiShare spec;
    if (count > 0 && args[0].isObject()) {
      auto content = args[0].getObject(runtime);
      spec.title = hostUiString(runtime, content, "title");
      spec.message = hostUiString(runtime, content, "message");
    }
    if (count > 1 && args[1].isString()) {
      spec.dialogTitle = args[1].getString(runtime).utf8(runtime);
    }
    auto jsInvoker = self(module).jsInvoker_;
    return react::createPromiseAsJSIValue(
        runtime,
        [spec = std::move(spec), jsInvoker](
            jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          hostUi().presentShare(
              spec,
              [jsInvoker, promise](HostUiShareResult result) {
                jsInvoker->invokeAsync(
                    [promise, result](jsi::Runtime& runtime) {
                      jsi::Object object(runtime);
                      object.setProperty(
                          runtime,
                          "action",
                          jsi::String::createFromAscii(
                              runtime,
                              result.shared ? "sharedAction"
                                            : "dismissedAction"));
                      promise->resolve(std::move(object));
                    });
              });
        });
  }
};

class HeadlessJsTaskSupportModule final : public react::TurboModule {
 public:
  explicit HeadlessJsTaskSupportModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("HeadlessJsTaskSupport", std::move(jsInvoker)) {
    addNoop("notifyTaskFinished", 1);
    methodMap_["notifyTaskRetry"] = {1, &notifyTaskRetry};
  }

 private:
  static jsi::Value notifyTaskRetry(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return react::createPromiseAsJSIValue(
        runtime,
        [](jsi::Runtime&, std::shared_ptr<react::Promise> promise) {
          promise->resolve(false);
        });
  }

  void addNoop(const char* name, size_t argumentCount) {
    methodMap_[name] = {
        argumentCount,
        [](jsi::Runtime&,
           react::TurboModule&,
           const jsi::Value*,
           size_t) -> jsi::Value { return jsi::Value::undefined(); }};
  }
};
} // namespace

std::shared_ptr<react::TurboModule> getHeadlessRNModule(
    jsi::Runtime&,
    const std::string& name,
    const std::string& profile,
    const std::shared_ptr<react::CallInvoker>& jsInvoker,
    const HeadlessRNModuleHost& host) {
  if (profile == "android-rn73" && name == "PlatformConstants") {
    return std::make_shared<PlatformConstantsAndroidRN73>(jsInvoker);
  }
  if (profile == "android-rn87" && name == "PlatformConstants") {
    return std::make_shared<PlatformConstantsAndroidRN87>(jsInvoker);
  }
  if (profile == "ios-rn87" && name == "PlatformConstants") {
    return std::make_shared<PlatformConstantsIOSRN87>(jsInvoker);
  }
  if (name == "DeviceInfo") {
    return std::make_shared<DeviceInfoModule>(
        jsInvoker, host, isAndroidProfile(profile));
  }
  if (name == "SourceCode") {
    return std::make_shared<SourceCodeModule>(jsInvoker, host.scriptURL);
  }
  if (name == "AppState") {
    return createHeadlessAppStateModule(jsInvoker);
  }
  if (name == "WebSocketModule") {
    return createHeadlessWebSocketModule(jsInvoker);
  }
  if (name == "BlobModule") {
    return createHeadlessBlobModule(jsInvoker);
  }
  if (name == "FileReaderModule") {
    return createHeadlessFileReaderModule(jsInvoker);
  }
  if (name == "SegmentFetcher") {
    return std::make_shared<SegmentFetcherModule>(jsInvoker);
  }
  if (name == "Clipboard") {
    return std::make_shared<ClipboardModule>(jsInvoker);
  }
  if (name == "Vibration") {
    return std::make_shared<VibrationModule>(jsInvoker);
  }
  if (name == "Appearance") {
    return createHeadlessAppearanceModule(jsInvoker);
  }
  if (name == "I18nManager") {
    return std::make_shared<I18nManagerModule>(jsInvoker);
  }
  if (name == "ImageEditingManager") {
    return createHeadlessImageEditingModule(jsInvoker, host.assetDirectory);
  }
  if (name == "ImageStoreManager") {
    return createHeadlessImageStoreModule(jsInvoker, host.assetDirectory);
  }
  if (name == "FrameRateLogger") {
    return createHeadlessFrameRateLogger(jsInvoker);
  }
  if (name == "ModalManager") {
    return createHeadlessModalManager(jsInvoker);
  }
  if (name == "DevLoadingView") {
    return createHeadlessDevLoadingView(jsInvoker);
  }
  if (name == "RedBox") {
    return createHeadlessRedBox(jsInvoker);
  }
  if (isAndroidProfile(profile) && name == "ReactDevToolsSettingsManager") {
    return createHeadlessReactDevToolsSettingsManager(jsInvoker);
  }
  if (name == "ReactDevToolsRuntimeSettingsModule") {
    return createHeadlessReactDevToolsRuntimeSettingsModule(jsInvoker);
  }
  if (name == "RNCSafeAreaContext") {
    return std::make_shared<RNCSafeAreaContextModule>(jsInvoker, host);
  }
  if (isAndroidProfile(profile) && name == "StatusBarManager") {
    return std::make_shared<StatusBarManagerAndroid>(jsInvoker);
  }
  if (isAndroidProfile(profile) && name == "ToastAndroid") {
    return std::make_shared<ToastAndroidModule>(jsInvoker);
  }
  if (name == "Networking") {
    return std::make_shared<NetworkingAndroidRN73>(jsInvoker);
  }
  if (name == "ImageLoader") {
    return std::make_shared<ImageLoaderAndroidRN73>(
        jsInvoker, host.assetDirectory);
  }
  if (name == "KeyboardObserver") {
    auto module = std::make_shared<KeyboardObserverModule>(jsInvoker);
    headlessKeyboard().viewportWidth = host.viewportWidth;
    headlessKeyboard().viewportHeight = host.viewportHeight;
    headlessKeyboard().emit =
        [weak = std::weak_ptr<KeyboardObserverModule>(module)](bool show) {
          if (auto locked = weak.lock()) {
            locked->emitVisibility(show);
          }
        };
    return module;
  }
  if (name == "AccessibilityInfo") {
    return createHeadlessAccessibilityInfoModule(jsInvoker);
  }
  if (name == "HeadlessJsTaskSupport") {
    return std::make_shared<HeadlessJsTaskSupportModule>(jsInvoker);
  }
  if (isAndroidProfile(profile) && name == "IntentAndroid") {
    return std::make_shared<IntentAndroidModule>(jsInvoker);
  }
  if (isAndroidProfile(profile) && name == "PermissionsAndroid") {
    return std::make_shared<PermissionsAndroidModule>(jsInvoker);
  }
  if (isAndroidProfile(profile) && name == "DialogManagerAndroid") {
    return std::make_shared<DialogManagerAndroidModule>(jsInvoker);
  }
  if (isAndroidProfile(profile) && name == "ShareModule") {
    return std::make_shared<ShareModule>(jsInvoker);
  }
  if (isAndroidProfile(profile) && name == "DeviceEventManager") {
    return createHeadlessDeviceEventManager(jsInvoker);
  }
  if (isAndroidProfile(profile) && name == "SoundManager") {
    return std::make_shared<EventedNoopModule>(
        "SoundManager",
        jsInvoker,
        std::initializer_list<std::pair<const char*, size_t>>{
            {"playTouchSound", 0}});
  }
  if ((isAndroidProfile(profile) || profile == "ios-rn87") &&
      name == "LinkingManager") {
    return std::make_shared<LinkingManagerModule>(jsInvoker);
  }
  if (profile == "ios-rn87" && name == "AlertManager") {
    return createHeadlessAlertManagerModule(jsInvoker);
  }
  if (profile == "ios-rn87" && name == "ActionSheetManager") {
    return createHeadlessActionSheetManagerModule(jsInvoker);
  }
  if (profile == "ios-rn87" && name == "SettingsManager") {
    return createHeadlessSettingsManagerModule(jsInvoker);
  }
  if (profile == "ios-rn87" && name == "StatusBarManager") {
    return createHeadlessStatusBarManagerIOS(jsInvoker);
  }
  if (profile == "ios-rn87" && name == "AccessibilityManager") {
    return createHeadlessAccessibilityManagerModule(jsInvoker);
  }
  if (profile == "ios-rn87" && name == "PushNotificationManager") {
    return createHeadlessPushNotificationManagerModule(jsInvoker);
  }
  return nullptr;
}

std::vector<std::string> getHeadlessRNModuleNames(const std::string& profile) {
  std::vector<std::string> result{
      "NativeReactNativeFeatureFlagsCxx",
      "NativeMicrotasksCxx",
      "NativeDOMCxx",
      "NativeAnimatedModule",
      "NativeIdleCallbacksCxx",
      "NativePerformanceCxx",
      "NativeIntersectionObserverCxx",
      "NativeMutationObserverCxx",
      "ExceptionsManager",
      "UIManager",
      "LogBox",
      "DevSettings",
      "DeviceInfo",
      "SourceCode",
      "AppState",
      "WebSocketModule",
      "BlobModule",
      "FileReaderModule",
      "SegmentFetcher",
      "Clipboard",
      "Vibration",
      "Appearance",
      "I18nManager",
      "Networking",
      "ImageLoader",
      "ImageEditingManager",
      "ImageStoreManager",
      "KeyboardObserver",
      "AccessibilityInfo",
      "HeadlessJsTaskSupport",
      "FrameRateLogger",
      "ModalManager",
      "DevLoadingView",
      "RedBox",
      "ReactDevToolsRuntimeSettingsModule",
      "RNCSafeAreaContext"};
  if (profile == "android-rn73") {
    result.insert(
        result.end(),
        {"PlatformConstants",
         "StatusBarManager",
         "ToastAndroid",
         "IntentAndroid",
         "PermissionsAndroid",
         "DialogManagerAndroid",
         "ShareModule",
         "DeviceEventManager",
         "SoundManager",
         "LinkingManager"});
  } else if (profile == "android-rn87") {
    result.insert(
        result.end(),
        {"PlatformConstants",
         "StatusBarManager",
         "ToastAndroid",
         "IntentAndroid",
         "PermissionsAndroid",
         "DialogManagerAndroid",
         "ShareModule",
         "DeviceEventManager",
         "SoundManager",
         "LinkingManager",
         "ReactDevToolsSettingsManager"});
  } else if (profile == "ios-rn87") {
    result.insert(
        result.end(),
        {"PlatformConstants",
         "LinkingManager",
         "AlertManager",
         "ActionSheetManager",
         "SettingsManager",
         "StatusBarManager",
         "AccessibilityManager",
         "PushNotificationManager"});
  }
  return result;
}

std::vector<HeadlessModuleCapability> getHeadlessRNModuleCapabilities(
    const std::string& profile) {
  std::vector<HeadlessModuleCapability> result{
      {"NativeReactNativeFeatureFlagsCxx", "real-headless"},
      {"NativeMicrotasksCxx", "real-headless"},
      {"NativeDOMCxx", "real-headless"},
      {"NativeAnimatedModule", "shared-animation-backend"},
      {"NativeIdleCallbacksCxx", "runtime-scheduler-idle"},
      {"NativePerformanceCxx", "host-performance-timeline"},
      {"NativeIntersectionObserverCxx", "rn-intersection-observer"},
      {"NativeMutationObserverCxx", "rn-mutation-observer"},
      {"ExceptionsManager", "headless-adapter"},
      {"UIManager", "fabric-layout-animation"},
      {"LogBox", "headless-adapter"},
      {"DevSettings", "interactive-reload"},
      {"DeviceInfo", "headless-adapter"},
      {"SourceCode", "headless-adapter"},
      {"AppState", "host-environment-events"},
      {"WebSocketModule", "urlsession-websocket"},
      {"BlobModule", "in-memory-blob-store"},
      {"FileReaderModule", "in-memory-blob-store"},
      {"SegmentFetcher", "headless-adapter"},
      {"Clipboard", "nspasteboard"},
      {"Vibration", "imgui-or-mock"},
      {"Appearance", "host-environment-events"},
      {"I18nManager", "headless-adapter"},
      {"Networking", "urlsession-http"},
      {"ImageLoader", "local-and-http-size"},
      {"ImageEditingManager", "imageio-crop-and-file-store"},
      {"ImageStoreManager", "imageio-crop-and-file-store"},
      {"KeyboardObserver", "headless-keyboard-metrics"},
      {"AccessibilityInfo", "host-environment-events"},
      {"HeadlessJsTaskSupport", "headless-adapter"},
      {"FrameRateLogger", "headless-adapter"},
      {"ModalManager", "headless-adapter"},
      {"DevLoadingView", "headless-adapter"},
      {"RedBox", "headless-adapter"},
      {"ReactDevToolsRuntimeSettingsModule", "in-memory-dev-settings"},
      {"RNCSafeAreaContext", "window-relative-insets"},
  };
  if (profile == "android-rn73") {
    result.insert(
        result.end(),
        {{"PlatformConstants", "fixed-fixture"},
         {"StatusBarManager", "skia-status-bar"},
         {"ToastAndroid", "skia-toast"},
         {"IntentAndroid", "imgui-or-mock"},
         {"PermissionsAndroid", "imgui-or-mock"},
         {"DialogManagerAndroid", "imgui-or-mock"},
         {"ShareModule", "imgui-or-mock"},
         {"DeviceEventManager", "hardware-back-press"},
         {"SoundManager", "headless-adapter"},
         {"LinkingManager", "imgui-or-mock"}});
  } else if (profile == "android-rn87") {
    result.insert(
        result.end(),
        {{"PlatformConstants", "headless-platform-adapter"},
         {"StatusBarManager", "skia-status-bar"},
         {"ToastAndroid", "skia-toast"},
         {"IntentAndroid", "imgui-or-mock"},
         {"PermissionsAndroid", "imgui-or-mock"},
         {"DialogManagerAndroid", "imgui-or-mock"},
         {"ShareModule", "imgui-or-mock"},
         {"DeviceEventManager", "hardware-back-press"},
         {"SoundManager", "headless-adapter"},
         {"LinkingManager", "imgui-or-mock"},
         {"ReactDevToolsSettingsManager", "in-memory-dev-settings"}});
  } else if (profile == "ios-rn87") {
    result.insert(
        result.end(),
        {{"PlatformConstants", "headless-platform-adapter"},
         {"LinkingManager", "imgui-or-mock"},
         {"AlertManager", "imgui-or-mock"},
         {"ActionSheetManager", "imgui-or-mock"},
         {"SettingsManager", "in-memory-settings"},
         {"StatusBarManager", "skia-status-bar"},
         {"AccessibilityManager", "host-environment-events"},
         {"PushNotificationManager", "headless-adapter"}});
  }
  return result;
}
