#import "HeadlessHttp.h"

#import <AppKit/NSPasteboard.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
constexpr const char* kImageUserAgent = "okhttp/4.12.0";
constexpr const char* kImageAccept =
    "image/webp,image/png,image/jpeg,*/*;q=0.8";
constexpr auto kImageRetryCooldown = std::chrono::seconds(30);

std::mutex imageCacheMutex;
std::unordered_map<std::string, std::filesystem::path> imageCache;
std::unordered_set<std::string> imageInFlight;
std::unordered_map<std::string, std::chrono::steady_clock::time_point>
    imageNextRetry;
std::unordered_map<
    std::string,
    std::vector<std::function<void(std::filesystem::path, std::string)>>>
    imageWaiters;

int nextHttpRequestId() {
  static std::atomic<int> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

NSMutableDictionary<NSNumber*, NSURLSessionDataTask*>* tasks() {
  static NSMutableDictionary* dictionary;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    dictionary = [NSMutableDictionary dictionary];
  });
  return dictionary;
}

NSString* nsString(const std::string& value) {
  return [[NSString alloc] initWithBytes:value.data()
                                  length:value.size()
                                encoding:NSUTF8StringEncoding] ?: @"";
}

std::string stdString(NSString* value) {
  if (value == nil) {
    return {};
  }
  const char* utf8 = value.UTF8String;
  return utf8 == nullptr ? std::string{} : std::string(utf8);
}
} // namespace

void headlessHttpSend(
    HeadlessHttpRequest request,
    std::function<void(HeadlessHttpResponse)> callback) {
  if (request.url.empty()) {
    HeadlessHttpResponse response;
    response.error = "empty url";
    callback(std::move(response));
    return;
  }
  NSURL* url = [NSURL URLWithString:nsString(request.url)];
  if (url == nil) {
    HeadlessHttpResponse response;
    response.error = "invalid url";
    callback(std::move(response));
    return;
  }
  NSMutableURLRequest* urlRequest =
      [NSMutableURLRequest requestWithURL:url];
  urlRequest.HTTPMethod = nsString(request.method.empty() ? "GET" : request.method);
  if (request.timeoutMs > 0) {
    urlRequest.timeoutInterval = request.timeoutMs / 1000.0;
  }
  for (const auto& header : request.headers) {
    [urlRequest setValue:nsString(header.second)
        forHTTPHeaderField:nsString(header.first)];
  }
  if (!request.body.empty()) {
    urlRequest.HTTPBody = [NSData dataWithBytes:request.body.data()
                                         length:request.body.size()];
  }
  const int requestId = request.requestId;
  NSURLSessionDataTask* task = [[NSURLSession sharedSession]
      dataTaskWithRequest:urlRequest
        completionHandler:^(
            NSData* data, NSURLResponse* urlResponse, NSError* error) {
          @synchronized(tasks()) {
            [tasks() removeObjectForKey:@(requestId)];
          }
          HeadlessHttpResponse response;
          response.url = request.url;
          if ([urlResponse isKindOfClass:[NSHTTPURLResponse class]]) {
            NSHTTPURLResponse* http = (NSHTTPURLResponse*)urlResponse;
            response.status = static_cast<int>(http.statusCode);
            if (http.URL.absoluteString != nil) {
              response.url = stdString(http.URL.absoluteString);
            }
            NSDictionary* headers = http.allHeaderFields;
            for (NSString* key in headers) {
              response.headers.emplace_back(
                  stdString(key), stdString([headers[key] description]));
            }
          }
          if (data != nil && data.length > 0) {
            response.body.assign(
                static_cast<const char*>(data.bytes),
                static_cast<const char*>(data.bytes) + data.length);
          }
          if (error != nil) {
            response.error = stdString(error.localizedDescription);
            response.timeout = error.code == NSURLErrorTimedOut;
            if (response.status == 0) {
              response.status = 0;
            }
          } else if (response.status == 0) {
            response.status = 200;
          }
          callback(std::move(response));
        }];
  @synchronized(tasks()) {
    tasks()[@(requestId)] = task;
  }
  [task resume];
}

void headlessHttpAbort(int requestId) {
  @synchronized(tasks()) {
    NSURLSessionDataTask* task = tasks()[@(requestId)];
    [task cancel];
    [tasks() removeObjectForKey:@(requestId)];
  }
}

namespace {
std::filesystem::path imageCacheFilePath(const std::string& uri) {
  NSString* directory = NSTemporaryDirectory();
  NSString* name = [NSString stringWithFormat:@"rnsim-image-%zu",
                                              std::hash<std::string>{}(uri)];
  NSString* path = [directory stringByAppendingPathComponent:name];
  return stdString(path);
}

bool pathIsDecodableImage(const std::filesystem::path& path) {
  if (path.empty()) {
    return false;
  }
  NSURL* url = [NSURL fileURLWithPath:nsString(path.string()) isDirectory:NO];
  if (url == nil) {
    return false;
  }
  CGImageSourceRef source =
      CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
  if (source == nullptr) {
    return false;
  }
  CFDictionaryRef properties =
      CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
  CFRelease(source);
  if (properties == nullptr) {
    return false;
  }
  int width = 0;
  int height = 0;
  auto widthValue = static_cast<CFNumberRef>(
      CFDictionaryGetValue(properties, kCGImagePropertyPixelWidth));
  auto heightValue = static_cast<CFNumberRef>(
      CFDictionaryGetValue(properties, kCGImagePropertyPixelHeight));
  if (widthValue != nullptr) {
    CFNumberGetValue(widthValue, kCFNumberIntType, &width);
  }
  if (heightValue != nullptr) {
    CFNumberGetValue(heightValue, kCFNumberIntType, &height);
  }
  CFRelease(properties);
  return width > 0 && height > 0;
}

std::filesystem::path cachedDecodableImagePath(const std::string& uri) {
  if (uri.empty()) {
    return {};
  }
  const auto diskPath = imageCacheFilePath(uri);
  std::filesystem::path mapped;
  {
    std::lock_guard<std::mutex> lock(imageCacheMutex);
    const auto found = imageCache.find(uri);
    if (found != imageCache.end()) {
      mapped = found->second;
    }
  }
  if (!mapped.empty() && pathIsDecodableImage(mapped)) {
    return mapped;
  }
  if (pathIsDecodableImage(diskPath)) {
    std::lock_guard<std::mutex> lock(imageCacheMutex);
    imageCache[uri] = diskPath;
    return diskPath;
  }
  std::error_code existsError;
  if (std::filesystem::exists(diskPath, existsError)) {
    std::error_code error;
    std::filesystem::remove(diskPath, error);
  }
  if (!mapped.empty()) {
    std::lock_guard<std::mutex> lock(imageCacheMutex);
    imageCache.erase(uri);
  }
  return {};
}

void completeImageWaiters(
    const std::string& uri,
    std::filesystem::path path,
    std::string error) {
  std::vector<std::function<void(std::filesystem::path, std::string)>> waiters;
  {
    std::lock_guard<std::mutex> lock(imageCacheMutex);
    imageInFlight.erase(uri);
    auto found = imageWaiters.find(uri);
    if (found != imageWaiters.end()) {
      waiters = std::move(found->second);
      imageWaiters.erase(found);
    }
    if (error.empty() && !path.empty()) {
      imageCache[uri] = path;
      imageNextRetry.erase(uri);
    } else {
      imageNextRetry[uri] =
          std::chrono::steady_clock::now() + kImageRetryCooldown;
    }
  }
  for (auto& waiter : waiters) {
    waiter(path, error);
  }
}
} // namespace

void headlessCacheImageBytes(const std::string& uri, const std::string& bytes) {
  if (uri.empty() || bytes.empty() || !headlessImagePixelSizeFromBytes(bytes)) {
    return;
  }
  const auto path = imageCacheFilePath(uri);
  NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
  if (![data writeToFile:nsString(path.string()) atomically:YES]) {
    return;
  }
  if (!pathIsDecodableImage(path)) {
    std::error_code error;
    std::filesystem::remove(path, error);
    return;
  }
  std::lock_guard<std::mutex> lock(imageCacheMutex);
  imageCache[uri] = path;
}

std::filesystem::path headlessCachedImagePath(const std::string& uri) {
  return cachedDecodableImagePath(uri);
}

std::optional<std::pair<int, int>> headlessImagePixelSizeFromBytes(
    const std::string& bytes) {
  if (bytes.empty()) {
    return std::nullopt;
  }
  NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
  CGImageSourceRef source = CGImageSourceCreateWithData(
      (__bridge CFDataRef)data, nullptr);
  if (source == nullptr) {
    return std::nullopt;
  }
  CFDictionaryRef properties =
      CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
  CFRelease(source);
  if (properties == nullptr) {
    return std::nullopt;
  }
  int width = 0;
  int height = 0;
  auto widthValue = static_cast<CFNumberRef>(
      CFDictionaryGetValue(properties, kCGImagePropertyPixelWidth));
  auto heightValue = static_cast<CFNumberRef>(
      CFDictionaryGetValue(properties, kCGImagePropertyPixelHeight));
  if (widthValue != nullptr) {
    CFNumberGetValue(widthValue, kCFNumberIntType, &width);
  }
  if (heightValue != nullptr) {
    CFNumberGetValue(heightValue, kCFNumberIntType, &height);
  }
  CFRelease(properties);
  if (width <= 0 || height <= 0) {
    return std::nullopt;
  }
  return std::pair{width, height};
}

void headlessFetchImage(
    const std::string& uri,
    std::function<void(std::filesystem::path, std::string)> onDone) {
  if (!onDone) {
    onDone = [](std::filesystem::path, std::string) {};
  }
  if (uri.empty() ||
      !(uri.starts_with("http://") || uri.starts_with("https://"))) {
    onDone({}, "unsupported uri");
    return;
  }
  if (const auto cached = cachedDecodableImagePath(uri); !cached.empty()) {
    onDone(cached, {});
    return;
  }
  bool coolingDown = false;
  {
    std::lock_guard<std::mutex> lock(imageCacheMutex);
    const auto now = std::chrono::steady_clock::now();
    const auto retryAt = imageNextRetry.find(uri);
    if (retryAt != imageNextRetry.end() && now < retryAt->second) {
      coolingDown = true;
    } else {
      imageWaiters[uri].push_back(onDone);
      if (!imageInFlight.insert(uri).second) {
        return;
      }
    }
  }
  if (coolingDown) {
    // Never invoke waiters while holding imageCacheMutex: prefetch's
    // onDone calls headlessCachedImagePath, which takes the same lock.
    onDone({}, "image fetch cooling down");
    return;
  }

  HeadlessHttpRequest request;
  request.method = "GET";
  request.url = uri;
  request.timeoutMs = 15000;
  request.headers.emplace_back("User-Agent", kImageUserAgent);
  request.headers.emplace_back("Accept", kImageAccept);
  request.requestId = nextHttpRequestId();
  headlessHttpSend(
      std::move(request), [uri](HeadlessHttpResponse response) {
        const auto stale = cachedDecodableImagePath(uri);
        if (!response.error.empty() || response.status >= 400 ||
            response.body.empty()) {
          if (!stale.empty()) {
            completeImageWaiters(uri, stale, {});
            return;
          }
          completeImageWaiters(
              uri,
              {},
              response.error.empty() ? "Could not load image" : response.error);
          return;
        }
        headlessCacheImageBytes(uri, response.body);
        const auto cached = cachedDecodableImagePath(uri);
        if (!cached.empty()) {
          completeImageWaiters(uri, cached, {});
          return;
        }
        if (!stale.empty()) {
          completeImageWaiters(uri, stale, {});
          return;
        }
        completeImageWaiters(uri, {}, "Could not load image");
      });
}

void headlessPrefetchImage(
    const std::string& uri,
    std::function<void()> onDone) {
  if (uri.empty() ||
      !(uri.starts_with("http://") || uri.starts_with("https://"))) {
    if (onDone) {
      onDone();
    }
    return;
  }
  headlessFetchImage(uri, [onDone](std::filesystem::path, std::string) {
    if (onDone) {
      onDone();
    }
  });
}

void headlessImageRequestsReset() {
  std::unordered_map<
      std::string,
      std::vector<std::function<void(std::filesystem::path, std::string)>>>
      waiters;
  {
    std::lock_guard<std::mutex> lock(imageCacheMutex);
    imageInFlight.clear();
    waiters = std::move(imageWaiters);
    imageWaiters.clear();
  }
  // Destroy callbacks while ReactInstance still owns the JSI runtime. Image
  // loader callbacks may retain react::Promise objects, which cannot safely be
  // left in process-lifetime storage and destroyed during static teardown.
  waiters.clear();
}

std::string headlessClipboardGet() {
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  NSString* string = [pasteboard stringForType:NSPasteboardTypeString];
  return stdString(string);
}

void headlessClipboardSet(const std::string& contents) {
  NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
  [pasteboard clearContents];
  [pasteboard setString:nsString(contents) forType:NSPasteboardTypeString];
}
