#include "HeadlessBlob.h"

#include <react/nativemodule/core/ReactCommon/TurboModuleUtils.h>

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <uuid/uuid.h>
#include <vector>

namespace jsi = facebook::jsi;
namespace react = facebook::react;

void headlessWebSocketSendBinary(int socketId, const std::string& bytes);

namespace {
struct BlobStore {
  std::mutex mutex;
  std::unordered_map<std::string, std::string> blobs;
  bool networkingHandler{false};
  std::unordered_set<int> webSocketHandlers;
};

BlobStore& blobStore() {
  static BlobStore store;
  return store;
}

std::string makeBlobId() {
  uuid_t uuid;
  uuid_generate(uuid);
  char formatted[37];
  uuid_unparse_lower(uuid, formatted);
  return std::string(formatted);
}

struct BlobSlice {
  std::string blobId;
  int offset{0};
  int size{-1};
  std::string type;
};

bool blobSliceFromJs(
    jsi::Runtime& runtime,
    const jsi::Value& value,
    BlobSlice& slice) {
  if (!value.isObject()) {
    return false;
  }
  auto object = value.getObject(runtime);
  const auto id = object.getProperty(runtime, "blobId");
  if (!id.isString()) {
    return false;
  }
  slice.blobId = id.getString(runtime).utf8(runtime);
  const auto offset = object.getProperty(runtime, "offset");
  if (offset.isNumber()) {
    slice.offset = static_cast<int>(offset.getNumber());
  }
  const auto size = object.getProperty(runtime, "size");
  if (size.isNumber()) {
    slice.size = static_cast<int>(size.getNumber());
  }
  const auto type = object.getProperty(runtime, "type");
  if (type.isString()) {
    slice.type = type.getString(runtime).utf8(runtime);
  }
  return true;
}

constexpr const char* kInvalidBlob = "The specified blob is invalid";

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64Value(char character) {
  if (character >= 'A' && character <= 'Z') {
    return character - 'A';
  }
  if (character >= 'a' && character <= 'z') {
    return character - 'a' + 26;
  }
  if (character >= '0' && character <= '9') {
    return character - '0' + 52;
  }
  if (character == '+') {
    return 62;
  }
  if (character == '/') {
    return 63;
  }
  return -1;
}

int intArg(const jsi::Value* args, size_t count, size_t index, int fallback = 0) {
  if (index >= count || !args[index].isNumber()) {
    return fallback;
  }
  return static_cast<int>(args[index].getNumber());
}

std::string stringArg(
    jsi::Runtime& runtime,
    const jsi::Value* args,
    size_t count,
    size_t index) {
  if (index >= count || !args[index].isString()) {
    return {};
  }
  return args[index].getString(runtime).utf8(runtime);
}

class HeadlessBlobModule final : public react::TurboModule {
 public:
  explicit HeadlessBlobModule(std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("BlobModule", std::move(jsInvoker)) {
    methodMap_["getConstants"] = {0, &getConstants};
    methodMap_["addNetworkingHandler"] = {0, &addNetworkingHandler};
    methodMap_["addWebSocketHandler"] = {1, &addWebSocketHandler};
    methodMap_["removeWebSocketHandler"] = {1, &removeWebSocketHandler};
    methodMap_["sendOverSocket"] = {2, &sendOverSocket};
    methodMap_["createFromParts"] = {2, &createFromParts};
    methodMap_["release"] = {1, &release};
  }

 private:
  static jsi::Value getConstants(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    jsi::Object constants(runtime);
    constants.setProperty(
        runtime,
        "BLOB_URI_SCHEME",
        jsi::String::createFromAscii(runtime, "blob"));
    constants.setProperty(runtime, "BLOB_URI_HOST", jsi::Value::null());
    return constants;
  }

  static jsi::Value addNetworkingHandler(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    headlessBlobSetNetworkingHandler(true);
    return jsi::Value::undefined();
  }

  static jsi::Value addWebSocketHandler(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    headlessBlobAddWebSocketHandler(intArg(args, count, 0));
    return jsi::Value::undefined();
  }

  static jsi::Value removeWebSocketHandler(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    headlessBlobRemoveWebSocketHandler(intArg(args, count, 0));
    return jsi::Value::undefined();
  }

  static jsi::Value sendOverSocket(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    BlobSlice slice;
    if (count < 1 || !blobSliceFromJs(runtime, args[0], slice)) {
      return jsi::Value::undefined();
    }
    auto bytes = headlessBlobResolve(slice.blobId, slice.offset, slice.size);
    if (!bytes) {
      return jsi::Value::undefined();
    }
    headlessWebSocketSendBinary(intArg(args, count, 1), *bytes);
    return jsi::Value::undefined();
  }

  static jsi::Value createFromParts(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (count < 2 || !args[0].isObject() ||
        !args[0].getObject(runtime).isArray(runtime) || !args[1].isString()) {
      return jsi::Value::undefined();
    }
    auto parts = args[0].getObject(runtime).getArray(runtime);
    const auto withId = args[1].getString(runtime).utf8(runtime);
    std::string combined;
    const auto length = parts.size(runtime);
    for (size_t index = 0; index < length; ++index) {
      const auto item = parts.getValueAtIndex(runtime, index);
      if (!item.isObject()) {
        continue;
      }
      auto part = item.getObject(runtime);
      const auto typeValue = part.getProperty(runtime, "type");
      if (!typeValue.isString()) {
        throw jsi::JSError(runtime, "Invalid type for blob");
      }
      const auto type = typeValue.getString(runtime).utf8(runtime);
      const auto data = part.getProperty(runtime, "data");
      if (type == "blob") {
        BlobSlice slice;
        if (!blobSliceFromJs(runtime, data, slice)) {
          continue;
        }
        if (auto bytes =
                headlessBlobResolve(slice.blobId, slice.offset, slice.size)) {
          combined.append(*bytes);
        }
      } else if (type == "string") {
        if (data.isString()) {
          combined.append(data.getString(runtime).utf8(runtime));
        }
      } else {
        throw jsi::JSError(runtime, "Invalid type for blob: " + type);
      }
    }
    headlessBlobStoreWithId(withId, std::move(combined));
    return jsi::Value::undefined();
  }

  static jsi::Value release(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    headlessBlobRelease(stringArg(runtime, args, count, 0));
    return jsi::Value::undefined();
  }
};

class HeadlessFileReaderModule final : public react::TurboModule {
 public:
  explicit HeadlessFileReaderModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("FileReaderModule", std::move(jsInvoker)) {
    methodMap_["readAsText"] = {2, &readAsText};
    methodMap_["readAsDataURL"] = {1, &readAsDataURL};
  }

 private:
  static std::optional<std::string> resolveData(
      jsi::Runtime& runtime,
      const jsi::Value* args,
      size_t count,
      BlobSlice& slice) {
    if (count < 1 || !blobSliceFromJs(runtime, args[0], slice)) {
      return std::nullopt;
    }
    return headlessBlobResolve(slice.blobId, slice.offset, slice.size);
  }

  static jsi::Value readAsText(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    BlobSlice slice;
    auto bytes = resolveData(runtime, args, count, slice);
    return react::createPromiseAsJSIValue(
        runtime,
        [bytes = std::move(bytes)](
            jsi::Runtime& runtime, std::shared_ptr<react::Promise> promise) {
          if (!bytes) {
            promise->reject(kInvalidBlob);
            return;
          }
          promise->resolve(jsi::String::createFromUtf8(runtime, *bytes));
        });
  }

  static jsi::Value readAsDataURL(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    BlobSlice slice;
    auto bytes = resolveData(runtime, args, count, slice);
    return react::createPromiseAsJSIValue(
        runtime,
        [bytes = std::move(bytes), type = std::move(slice.type)](
            jsi::Runtime& runtime, std::shared_ptr<react::Promise> promise) {
          if (!bytes) {
            promise->reject(kInvalidBlob);
            return;
          }
          std::string dataUrl = "data:";
          dataUrl += type.empty() ? "application/octet-stream" : type;
          dataUrl += ";base64,";
          dataUrl += headlessBlobBase64Encode(*bytes);
          promise->resolve(jsi::String::createFromUtf8(runtime, dataUrl));
        });
  }
};
} // namespace

std::string headlessBlobStore(std::string bytes) {
  auto blobId = makeBlobId();
  headlessBlobStoreWithId(blobId, std::move(bytes));
  return blobId;
}

void headlessBlobStoreWithId(const std::string& blobId, std::string bytes) {
  auto& store = blobStore();
  std::lock_guard<std::mutex> lock(store.mutex);
  store.blobs[blobId] = std::move(bytes);
}

std::optional<std::string> headlessBlobResolve(
    const std::string& blobId,
    int offset,
    int size) {
  auto& store = blobStore();
  std::lock_guard<std::mutex> lock(store.mutex);
  const auto found = store.blobs.find(blobId);
  if (found == store.blobs.end()) {
    return std::nullopt;
  }
  const auto& data = found->second;
  if (offset < 0) {
    return std::nullopt;
  }
  const auto start = static_cast<size_t>(offset);
  if (start > data.size()) {
    return std::nullopt;
  }
  size_t length = 0;
  if (size < 0) {
    length = data.size() - start;
  } else {
    length = static_cast<size_t>(size);
    if (start + length > data.size()) {
      return std::nullopt;
    }
  }
  return data.substr(start, length);
}

void headlessBlobRelease(const std::string& blobId) {
  auto& store = blobStore();
  std::lock_guard<std::mutex> lock(store.mutex);
  store.blobs.erase(blobId);
}

void headlessBlobReset() {
  auto& store = blobStore();
  std::lock_guard<std::mutex> lock(store.mutex);
  store.blobs.clear();
  store.networkingHandler = false;
  store.webSocketHandlers.clear();
}

void headlessBlobSetNetworkingHandler(bool enabled) {
  auto& store = blobStore();
  std::lock_guard<std::mutex> lock(store.mutex);
  store.networkingHandler = enabled;
}

bool headlessBlobNetworkingHandlerEnabled() {
  auto& store = blobStore();
  std::lock_guard<std::mutex> lock(store.mutex);
  return store.networkingHandler;
}

void headlessBlobAddWebSocketHandler(int socketId) {
  auto& store = blobStore();
  std::lock_guard<std::mutex> lock(store.mutex);
  store.webSocketHandlers.insert(socketId);
}

void headlessBlobRemoveWebSocketHandler(int socketId) {
  auto& store = blobStore();
  std::lock_guard<std::mutex> lock(store.mutex);
  store.webSocketHandlers.erase(socketId);
}

bool headlessBlobWebSocketHandlerEnabled(int socketId) {
  auto& store = blobStore();
  std::lock_guard<std::mutex> lock(store.mutex);
  return store.webSocketHandlers.find(socketId) != store.webSocketHandlers.end();
}

std::string headlessBlobBase64Encode(std::string_view bytes) {
  std::string encoded;
  const auto length = bytes.size();
  encoded.reserve(((length + 2) / 3) * 4);
  size_t index = 0;
  while (index + 2 < length) {
    const auto b0 = static_cast<unsigned char>(bytes[index]);
    const auto b1 = static_cast<unsigned char>(bytes[index + 1]);
    const auto b2 = static_cast<unsigned char>(bytes[index + 2]);
    encoded.push_back(kBase64Alphabet[b0 >> 2]);
    encoded.push_back(kBase64Alphabet[((b0 & 0x03) << 4) | (b1 >> 4)]);
    encoded.push_back(kBase64Alphabet[((b1 & 0x0f) << 2) | (b2 >> 6)]);
    encoded.push_back(kBase64Alphabet[b2 & 0x3f]);
    index += 3;
  }
  if (index < length) {
    const auto b0 = static_cast<unsigned char>(bytes[index]);
    encoded.push_back(kBase64Alphabet[b0 >> 2]);
    if (index + 1 < length) {
      const auto b1 = static_cast<unsigned char>(bytes[index + 1]);
      encoded.push_back(kBase64Alphabet[((b0 & 0x03) << 4) | (b1 >> 4)]);
      encoded.push_back(kBase64Alphabet[(b1 & 0x0f) << 2]);
      encoded.push_back('=');
    } else {
      encoded.push_back(kBase64Alphabet[(b0 & 0x03) << 4]);
      encoded.push_back('=');
      encoded.push_back('=');
    }
  }
  return encoded;
}

std::string headlessBlobBase64Decode(std::string_view base64) {
  std::string filtered;
  filtered.reserve(base64.size());
  for (const char character : base64) {
    if (character == '\n' || character == '\r' || character == ' ' ||
        character == '\t') {
      continue;
    }
    filtered.push_back(character);
  }
  if (filtered.empty() || filtered.size() % 4 != 0) {
    return {};
  }
  size_t padding = 0;
  if (filtered.back() == '=') {
    ++padding;
  }
  if (filtered.size() >= 2 && filtered[filtered.size() - 2] == '=') {
    ++padding;
  }
  std::string decoded;
  decoded.reserve((filtered.size() / 4) * 3 - padding);
  for (size_t index = 0; index < filtered.size(); index += 4) {
    int values[4];
    for (int part = 0; part < 4; ++part) {
      const char character = filtered[index + static_cast<size_t>(part)];
      if (character == '=') {
        if (index != filtered.size() - 4 || part < 2) {
          return {};
        }
        values[part] = 0;
        continue;
      }
      values[part] = base64Value(character);
      if (values[part] < 0) {
        return {};
      }
    }
    decoded.push_back(
        static_cast<char>((values[0] << 2) | (values[1] >> 4)));
    if (filtered[index + 2] != '=') {
      decoded.push_back(
          static_cast<char>(((values[1] & 0x0f) << 4) | (values[2] >> 2)));
    }
    if (filtered[index + 3] != '=') {
      decoded.push_back(
          static_cast<char>(((values[2] & 0x03) << 6) | values[3]));
    }
  }
  return decoded;
}

std::shared_ptr<react::TurboModule> createHeadlessBlobModule(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<HeadlessBlobModule>(std::move(jsInvoker));
}

std::shared_ptr<react::TurboModule> createHeadlessFileReaderModule(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<HeadlessFileReaderModule>(std::move(jsInvoker));
}
