#include "HeadlessHttp.h"
#include "ImagePixelSize.h"

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
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

std::mutex clipboardMutex;
std::string clipboardContents;

std::once_flag curlOnce;

void ensureCurl() {
  std::call_once(curlOnce, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

int nextHttpRequestId() {
  static std::atomic<int> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

struct PendingRequest {
  std::atomic<bool> cancelled{false};
};

std::mutex requestMutex;
std::unordered_map<int, std::shared_ptr<PendingRequest>> pendingRequests;

struct CurlBuffers {
  std::string body;
  std::string headers;
};

std::size_t writeBody(char* ptr, std::size_t size, std::size_t nmemb, void* user) {
  auto* buffers = static_cast<CurlBuffers*>(user);
  buffers->body.append(ptr, size * nmemb);
  return size * nmemb;
}

std::size_t writeHeaders(char* ptr, std::size_t size, std::size_t nmemb, void* user) {
  auto* buffers = static_cast<CurlBuffers*>(user);
  buffers->headers.append(ptr, size * nmemb);
  return size * nmemb;
}

int xferCallback(
    void* clientp,
    curl_off_t,
    curl_off_t,
    curl_off_t,
    curl_off_t) {
  auto* pending = static_cast<PendingRequest*>(clientp);
  return pending != nullptr && pending->cancelled.load() ? 1 : 0;
}

void parseHeaderBlock(
    const std::string& raw,
    std::vector<std::pair<std::string, std::string>>& headers) {
  std::size_t start = 0;
  while (start < raw.size()) {
    auto end = raw.find("\r\n", start);
    if (end == std::string::npos) {
      end = raw.size();
    }
    const auto line = raw.substr(start, end - start);
    start = end == raw.size() ? raw.size() : end + 2;
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    auto key = line.substr(0, colon);
    auto value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
      value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
      value.pop_back();
    }
    if (!key.empty()) {
      headers.emplace_back(std::move(key), std::move(value));
    }
  }
}

std::filesystem::path imageCacheFilePath(const std::string& uri) {
  return std::filesystem::temp_directory_path() /
      ("rnsim-image-" + std::to_string(std::hash<std::string>{}(uri)));
}

bool writeFileAtomically(const std::filesystem::path& path, std::string_view bytes) {
  const auto tmp = path.string() + ".tmp";
  std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  if (!output) {
    std::error_code error;
    std::filesystem::remove(tmp, error);
    return false;
  }
  std::error_code error;
  std::filesystem::rename(tmp, path, error);
  if (error) {
    std::filesystem::remove(tmp, error);
    return false;
  }
  return true;
}

std::optional<std::string> readFileBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::string bytes(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
  return bytes;
}

bool pathIsDecodableImage(const std::filesystem::path& path) {
  auto bytes = readFileBytes(path);
  if (!bytes || bytes->empty()) {
    return false;
  }
  return imagePixelSizeFromBytes(*bytes).has_value();
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

void headlessHttpSend(
    HeadlessHttpRequest request,
    std::function<void(HeadlessHttpResponse)> callback) {
  if (request.url.empty()) {
    HeadlessHttpResponse response;
    response.error = "empty url";
    callback(std::move(response));
    return;
  }
  ensureCurl();
  if (request.requestId == 0) {
    request.requestId = nextHttpRequestId();
  }
  auto pending = std::make_shared<PendingRequest>();
  {
    std::lock_guard<std::mutex> lock(requestMutex);
    pendingRequests[request.requestId] = pending;
  }
  std::thread([request = std::move(request),
               callback = std::move(callback),
               pending]() mutable {
    CURL* curl = curl_easy_init();
    HeadlessHttpResponse response;
    response.url = request.url;
    if (curl == nullptr) {
      response.error = "failed to init curl";
      callback(std::move(response));
      return;
    }
    CurlBuffers buffers;
    curl_slist* headerList = nullptr;
    for (const auto& header : request.headers) {
      const auto line = header.first + ": " + header.second;
      headerList = curl_slist_append(headerList, line.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(
        curl,
        CURLOPT_CUSTOMREQUEST,
        request.method.empty() ? "GET" : request.method.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffers);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeaders);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &buffers);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, pending.get());
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    if (request.timeoutMs > 0) {
      curl_easy_setopt(
          curl,
          CURLOPT_TIMEOUT_MS,
          static_cast<long>(request.timeoutMs));
    }
    if (!request.body.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
      curl_easy_setopt(
          curl,
          CURLOPT_POSTFIELDSIZE,
          static_cast<long>(request.body.size()));
    }
    if (headerList != nullptr) {
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }
    const auto result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    response.status = static_cast<int>(status);
    char* effectiveUrl = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
    if (effectiveUrl != nullptr) {
      response.url = effectiveUrl;
    }
    parseHeaderBlock(buffers.headers, response.headers);
    response.body = std::move(buffers.body);
    if (result != CURLE_OK) {
      response.error = curl_easy_strerror(result);
      response.timeout = result == CURLE_OPERATION_TIMEDOUT;
      if (pending->cancelled.load()) {
        response.error = "aborted";
      }
    } else if (response.status == 0) {
      response.status = 200;
    }
    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    {
      std::lock_guard<std::mutex> lock(requestMutex);
      pendingRequests.erase(request.requestId);
    }
    callback(std::move(response));
  }).detach();
}

void headlessHttpAbort(int requestId) {
  std::lock_guard<std::mutex> lock(requestMutex);
  const auto found = pendingRequests.find(requestId);
  if (found != pendingRequests.end()) {
    found->second->cancelled.store(true);
  }
}

void headlessCacheImageBytes(const std::string& uri, const std::string& bytes) {
  if (uri.empty() || bytes.empty() || !imagePixelSizeFromBytes(bytes)) {
    return;
  }
  const auto path = imageCacheFilePath(uri);
  if (!writeFileAtomically(path, bytes) || !pathIsDecodableImage(path)) {
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
  return imagePixelSizeFromBytes(bytes);
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
  headlessHttpSend(std::move(request), [uri](HeadlessHttpResponse response) {
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
  waiters.clear();
}

std::string headlessClipboardGet() {
  std::lock_guard<std::mutex> lock(clipboardMutex);
  return clipboardContents;
}

void headlessClipboardSet(const std::string& contents) {
  std::lock_guard<std::mutex> lock(clipboardMutex);
  clipboardContents = contents;
}
