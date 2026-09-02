#include "HeadlessWebSocket.h"
#include "HeadlessBlob.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <cctype>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jsi = facebook::jsi;
namespace react = facebook::react;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace ssl = boost::asio::ssl;
using tcp = asio::ip::tcp;

namespace {
class HeadlessWebSocketModule;

struct WsUrl {
  std::string host;
  std::string port;
  std::string target;
  bool tls{false};
};

std::optional<WsUrl> parseWsUrl(const std::string& url) {
  std::string_view rest = url;
  bool tls = false;
  if (rest.starts_with("wss://")) {
    tls = true;
    rest.remove_prefix(6);
  } else if (rest.starts_with("ws://")) {
    rest.remove_prefix(5);
  } else {
    return std::nullopt;
  }
  const auto pathStart = rest.find('/');
  const auto authority = std::string(
      rest.substr(0, pathStart == std::string_view::npos ? rest.size() : pathStart));
  if (authority.empty() || authority.find('@') != std::string::npos) {
    return std::nullopt;
  }
  std::string host = authority;
  std::string port = tls ? "443" : "80";
  if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
    host = host.substr(1, host.size() - 2);
  } else if (const auto colon = authority.rfind(':');
             colon != std::string::npos && authority.find(']') == std::string::npos) {
    host = authority.substr(0, colon);
    port = authority.substr(colon + 1);
  }
  if (host.empty() || port.empty()) {
    return std::nullopt;
  }
  return WsUrl{
      .host = std::move(host),
      .port = std::move(port),
      .target = pathStart == std::string_view::npos
          ? "/"
          : std::string(rest.substr(pathStart)),
      .tls = tls,
  };
}

std::string originFor(const WsUrl& parsed) {
  const auto scheme = parsed.tls ? "https" : "http";
  if ((parsed.tls && parsed.port == "443") ||
      (!parsed.tls && parsed.port == "80")) {
    return std::string(scheme) + "://" + parsed.host;
  }
  return std::string(scheme) + "://" + parsed.host + ":" + parsed.port;
}

std::string trimCopy(std::string_view value) {
  size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return std::string(value.substr(begin, end - begin));
}

std::string joinedProtocols(const std::vector<std::string>& protocols) {
  std::string joined;
  for (const auto& protocol : protocols) {
    auto value = trimCopy(protocol);
    if (value.empty() || value.find(',') != std::string::npos) {
      continue;
    }
    if (!joined.empty()) {
      joined += ',';
    }
    joined += value;
  }
  return joined;
}

int intArg(
    const jsi::Value* args,
    size_t count,
    size_t index,
    int fallback = 0) {
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

class Connection {
 public:
  Connection(int socketId, std::shared_ptr<HeadlessWebSocketModule> module)
      : socketId_(socketId), module_(std::move(module)), moduleRaw_(module_.lock().get()) {}

  ~Connection() {
    closeSilently();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  bool ownedBy(const void* module) const {
    return moduleRaw_ == module;
  }

  void connect(
      const std::string& url,
      std::vector<std::pair<std::string, std::string>> headers,
      std::string protocols);
  void sendText(const std::string& text);
  void sendBytes(const std::string& bytes);
  void ping();
  void closeWithCode(int code, const std::string& reason, bool emit);
  void closeSilently();

 private:
  template <typename Stream>
  void run(Stream& stream, const WsUrl& parsed, const std::string& protocols);
  void fail(const std::string& message);
  bool markClosed();

  const int socketId_;
  std::weak_ptr<HeadlessWebSocketModule> module_;
  HeadlessWebSocketModule* moduleRaw_{nullptr};
  std::mutex mutex_;
  bool closed_{false};
  asio::io_context context_;
  std::optional<websocket::stream<tcp::socket>> plain_;
  std::optional<websocket::stream<ssl::stream<tcp::socket>>> tls_;
  ssl::context sslContext_{ssl::context::tlsv12_client};
  std::thread worker_;
};

std::mutex connectionsMutex;
std::unordered_map<int, std::shared_ptr<Connection>> connections;

std::shared_ptr<Connection> connectionFor(int socketId) {
  std::lock_guard<std::mutex> lock(connectionsMutex);
  const auto found = connections.find(socketId);
  return found == connections.end() ? nullptr : found->second;
}

void retainConnection(int socketId, std::shared_ptr<Connection> connection) {
  std::shared_ptr<Connection> previous;
  {
    std::lock_guard<std::mutex> lock(connectionsMutex);
    previous = connections[socketId];
    connections[socketId] = std::move(connection);
  }
  if (previous && previous != connections[socketId]) {
    previous->closeSilently();
  }
}

void closeSocketsOwnedBy(void* module) {
  std::vector<std::shared_ptr<Connection>> owned;
  {
    std::lock_guard<std::mutex> lock(connectionsMutex);
    for (auto it = connections.begin(); it != connections.end();) {
      if (it->second->ownedBy(module)) {
        owned.push_back(it->second);
        it = connections.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto& connection : owned) {
    connection->closeSilently();
  }
}

class HeadlessWebSocketModule final
    : public react::TurboModule,
      public std::enable_shared_from_this<HeadlessWebSocketModule> {
 public:
  explicit HeadlessWebSocketModule(
      std::shared_ptr<react::CallInvoker> jsInvoker)
      : TurboModule("WebSocketModule", std::move(jsInvoker)) {
    methodMap_["connect"] = {4, &invokeConnect};
    methodMap_["send"] = {2, &invokeSend};
    methodMap_["sendBinary"] = {2, &invokeSendBinary};
    methodMap_["ping"] = {1, &invokePing};
    methodMap_["close"] = {3, &invokeClose};
    methodMap_["addListener"] = {1, &invokeNoop};
    methodMap_["removeListeners"] = {1, &invokeNoop};
  }

  ~HeadlessWebSocketModule() override {
    closeSocketsOwnedBy(this);
  }

  void emitOpen(int socketId, std::string protocol) {
    emitDeviceEvent(
        "websocketOpen",
        [socketId, protocol = std::move(protocol)](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object event(runtime);
          event.setProperty(runtime, "id", jsi::Value(socketId));
          event.setProperty(
              runtime,
              "protocol",
              jsi::String::createFromUtf8(runtime, protocol));
          args.emplace_back(std::move(event));
        });
  }

  void emitClosed(int socketId, int code, std::string reason) {
    emitDeviceEvent(
        "websocketClosed",
        [socketId, code, reason = std::move(reason)](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object event(runtime);
          event.setProperty(runtime, "id", jsi::Value(socketId));
          event.setProperty(runtime, "code", jsi::Value(code));
          event.setProperty(
              runtime,
              "reason",
              jsi::String::createFromUtf8(runtime, reason));
          args.emplace_back(std::move(event));
        });
  }

  void emitFailed(int socketId, std::string message) {
    emitDeviceEvent(
        "websocketFailed",
        [socketId, message = std::move(message)](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object event(runtime);
          event.setProperty(runtime, "id", jsi::Value(socketId));
          event.setProperty(
              runtime,
              "message",
              jsi::String::createFromUtf8(runtime, message));
          args.emplace_back(std::move(event));
        });
  }

  void emitText(int socketId, std::string data) {
    emitDeviceEvent(
        "websocketMessage",
        [socketId, data = std::move(data)](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object event(runtime);
          event.setProperty(runtime, "id", jsi::Value(socketId));
          event.setProperty(
              runtime,
              "type",
              jsi::String::createFromAscii(runtime, "text"));
          event.setProperty(
              runtime, "data", jsi::String::createFromUtf8(runtime, data));
          args.emplace_back(std::move(event));
        });
  }

  void emitBinary(int socketId, std::string bytes) {
    if (headlessBlobWebSocketHandlerEnabled(socketId)) {
      const int size = static_cast<int>(bytes.size());
      auto blobId = headlessBlobStore(std::move(bytes));
      emitDeviceEvent(
          "websocketMessage",
          [socketId, blobId = std::move(blobId), size](
              jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
            jsi::Object data(runtime);
            data.setProperty(
                runtime,
                "blobId",
                jsi::String::createFromUtf8(runtime, blobId));
            data.setProperty(runtime, "offset", jsi::Value(0));
            data.setProperty(runtime, "size", jsi::Value(size));
            jsi::Object event(runtime);
            event.setProperty(runtime, "id", jsi::Value(socketId));
            event.setProperty(
                runtime,
                "type",
                jsi::String::createFromAscii(runtime, "blob"));
            event.setProperty(runtime, "data", std::move(data));
            args.emplace_back(std::move(event));
          });
      return;
    }
    auto encoded = headlessBlobBase64Encode(bytes);
    emitDeviceEvent(
        "websocketMessage",
        [socketId, encoded = std::move(encoded)](
            jsi::Runtime& runtime, std::vector<jsi::Value>& args) {
          jsi::Object event(runtime);
          event.setProperty(runtime, "id", jsi::Value(socketId));
          event.setProperty(
              runtime,
              "type",
              jsi::String::createFromAscii(runtime, "binary"));
          event.setProperty(
              runtime,
              "data",
              jsi::String::createFromUtf8(runtime, encoded));
          args.emplace_back(std::move(event));
        });
  }

 private:
  static HeadlessWebSocketModule& self(react::TurboModule& module) {
    return static_cast<HeadlessWebSocketModule&>(module);
  }

  static jsi::Value invokeNoop(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value*,
      size_t) {
    return jsi::Value::undefined();
  }

  static jsi::Value invokeConnect(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    auto owner = self(module).shared_from_this();
    const int socketId = intArg(args, count, 3);
    if (count < 4 || !args[0].isString()) {
      owner->emitFailed(socketId, "Invalid WebSocket URL");
      return jsi::Value::undefined();
    }
    auto url = args[0].getString(runtime).utf8(runtime);
    std::vector<std::string> protocols;
    if (args[1].isObject() && args[1].getObject(runtime).isArray(runtime)) {
      auto array = args[1].getObject(runtime).getArray(runtime);
      const auto length = array.size(runtime);
      protocols.reserve(length);
      for (size_t index = 0; index < length; ++index) {
        const auto value = array.getValueAtIndex(runtime, index);
        if (value.isString()) {
          protocols.push_back(value.getString(runtime).utf8(runtime));
        }
      }
    }
    std::vector<std::pair<std::string, std::string>> headers;
    if (args[2].isObject()) {
      auto options = args[2].getObject(runtime);
      const auto headersValue = options.getProperty(runtime, "headers");
      if (headersValue.isObject()) {
        auto headersObject = headersValue.getObject(runtime);
        auto names = headersObject.getPropertyNames(runtime);
        const auto nameCount = names.size(runtime);
        for (size_t index = 0; index < nameCount; ++index) {
          const auto nameValue = names.getValueAtIndex(runtime, index);
          if (!nameValue.isString()) {
            continue;
          }
          auto key = nameValue.getString(runtime).utf8(runtime);
          const auto value = headersObject.getProperty(
              runtime, jsi::PropNameID::forUtf8(runtime, key));
          if (value.isString()) {
            headers.emplace_back(
                std::move(key), value.getString(runtime).utf8(runtime));
          }
        }
      }
    }
    if (url.empty() || !parseWsUrl(url)) {
      owner->emitFailed(socketId, "Invalid WebSocket URL");
      return jsi::Value::undefined();
    }
    auto connection = std::make_shared<Connection>(socketId, owner);
    retainConnection(socketId, connection);
    connection->connect(url, std::move(headers), joinedProtocols(protocols));
    return jsi::Value::undefined();
  }

  static jsi::Value invokeSend(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (auto connection = connectionFor(intArg(args, count, 1))) {
      connection->sendText(stringArg(runtime, args, count, 0));
    }
    return jsi::Value::undefined();
  }

  static jsi::Value invokeSendBinary(
      jsi::Runtime& runtime,
      react::TurboModule& module,
      const jsi::Value* args,
      size_t count) {
    const int socketId = intArg(args, count, 1);
    auto encoded = stringArg(runtime, args, count, 0);
    auto bytes = headlessBlobBase64Decode(encoded);
    if (bytes.empty() && !encoded.empty()) {
      self(module).emitFailed(socketId, "invalid base64");
      return jsi::Value::undefined();
    }
    headlessWebSocketSendBinary(socketId, bytes);
    return jsi::Value::undefined();
  }

  static jsi::Value invokePing(
      jsi::Runtime&,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (auto connection = connectionFor(intArg(args, count, 0))) {
      connection->ping();
    }
    return jsi::Value::undefined();
  }

  static jsi::Value invokeClose(
      jsi::Runtime& runtime,
      react::TurboModule&,
      const jsi::Value* args,
      size_t count) {
    if (auto connection = connectionFor(intArg(args, count, 2))) {
      connection->closeWithCode(
          intArg(args, count, 0, 1000),
          stringArg(runtime, args, count, 1),
          true);
    }
    return jsi::Value::undefined();
  }
};

void Connection::connect(
    const std::string& url,
    std::vector<std::pair<std::string, std::string>> headers,
    std::string protocols) {
  worker_ = std::thread([this,
                         url,
                         headers = std::move(headers),
                         protocols = std::move(protocols)] {
    const auto parsed = parseWsUrl(url);
    if (!parsed) {
      fail("Invalid WebSocket URL");
      return;
    }
    try {
      tcp::resolver resolver(context_);
      auto endpoints = resolver.resolve(parsed->host, parsed->port);
      const auto hostHeader = parsed->host + ':' + parsed->port;
      auto decorate = [&](websocket::request_type& request) {
        bool hasOrigin = false;
        for (const auto& header : headers) {
          request.set(header.first, header.second);
          if (header.first.size() == 6) {
            std::string lowered = header.first;
            for (auto& character : lowered) {
              character = static_cast<char>(
                  std::tolower(static_cast<unsigned char>(character)));
            }
            if (lowered == "origin") {
              hasOrigin = true;
            }
          }
        }
        if (!hasOrigin) {
          request.set(beast::http::field::origin, originFor(*parsed));
        }
        if (!protocols.empty()) {
          request.set(beast::http::field::sec_websocket_protocol, protocols);
        }
      };
      if (parsed->tls) {
        sslContext_.set_default_verify_paths();
        tls_.emplace(context_, sslContext_);
        if (!SSL_set_tlsext_host_name(tls_->next_layer().native_handle(), parsed->host.c_str())) {
          fail("TLS hostname failed");
          return;
        }
        asio::connect(beast::get_lowest_layer(*tls_), endpoints);
        tls_->next_layer().handshake(ssl::stream_base::client);
        tls_->set_option(websocket::stream_base::decorator(decorate));
        tls_->handshake(hostHeader, parsed->target);
        if (auto module = module_.lock()) {
          module->emitOpen(socketId_, protocols);
        }
        run(*tls_, *parsed, protocols);
      } else {
        plain_.emplace(context_);
        asio::connect(plain_->next_layer(), endpoints);
        plain_->set_option(websocket::stream_base::decorator(decorate));
        plain_->handshake(hostHeader, parsed->target);
        if (auto module = module_.lock()) {
          module->emitOpen(socketId_, protocols);
        }
        run(*plain_, *parsed, protocols);
      }
    } catch (const std::exception& error) {
      fail(error.what());
    }
  });
}

template <typename Stream>
void Connection::run(
    Stream& stream,
    const WsUrl&,
    const std::string&) {
  while (!closed_) {
    beast::flat_buffer buffer;
    stream.read(buffer);
    if (stream.got_text()) {
      if (auto module = module_.lock()) {
        module->emitText(socketId_, beast::buffers_to_string(buffer.data()));
      }
    } else {
      std::string bytes = beast::buffers_to_string(buffer.data());
      if (auto module = module_.lock()) {
        module->emitBinary(socketId_, std::move(bytes));
      }
    }
  }
}

void Connection::sendText(const std::string& text) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) {
    return;
  }
  try {
    if (plain_) {
      plain_->text(true);
      plain_->write(asio::buffer(text));
    } else if (tls_) {
      tls_->text(true);
      tls_->write(asio::buffer(text));
    }
  } catch (const std::exception& error) {
    fail(error.what());
  }
}

void Connection::sendBytes(const std::string& bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) {
    return;
  }
  try {
    if (plain_) {
      plain_->binary(true);
      plain_->write(asio::buffer(bytes));
    } else if (tls_) {
      tls_->binary(true);
      tls_->write(asio::buffer(bytes));
    }
  } catch (const std::exception& error) {
    fail(error.what());
  }
}

void Connection::ping() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) {
    return;
  }
  try {
    websocket::ping_data payload;
    if (plain_) {
      plain_->ping(payload);
    } else if (tls_) {
      tls_->ping(payload);
    }
  } catch (const std::exception&) {
  }
}

void Connection::closeWithCode(int code, const std::string& reason, bool emit) {
  if (!markClosed()) {
    return;
  }
  try {
    websocket::close_reason closeReason;
    closeReason.code = static_cast<websocket::close_code>(code);
    closeReason.reason = reason;
    if (plain_) {
      plain_->close(closeReason);
    } else if (tls_) {
      tls_->close(closeReason);
    }
  } catch (const std::exception&) {
    if (plain_) {
      beast::error_code error;
      plain_->next_layer().close(error);
    } else if (tls_) {
      beast::error_code error;
      beast::get_lowest_layer(*tls_).close(error);
    }
  }
  headlessBlobRemoveWebSocketHandler(socketId_);
  if (emit) {
    if (auto module = module_.lock()) {
      module->emitClosed(socketId_, code, reason);
    }
  }
}

void Connection::closeSilently() {
  if (!markClosed()) {
    return;
  }
  beast::error_code error;
  if (plain_) {
    plain_->next_layer().cancel(error);
    plain_->next_layer().close(error);
  } else if (tls_) {
    beast::get_lowest_layer(*tls_).cancel(error);
    beast::get_lowest_layer(*tls_).close(error);
  }
  headlessBlobRemoveWebSocketHandler(socketId_);
}

void Connection::fail(const std::string& message) {
  if (!markClosed()) {
    return;
  }
  if (auto module = module_.lock()) {
    module->emitFailed(socketId_, message);
  }
  beast::error_code error;
  if (plain_) {
    plain_->next_layer().close(error);
  } else if (tls_) {
    beast::get_lowest_layer(*tls_).close(error);
  }
  headlessBlobRemoveWebSocketHandler(socketId_);
}

bool Connection::markClosed() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_) {
    return false;
  }
  closed_ = true;
  return true;
}
} // namespace

void headlessWebSocketSendBinary(int socketId, const std::string& bytes) {
  if (auto connection = connectionFor(socketId)) {
    connection->sendBytes(bytes);
  }
}

std::shared_ptr<react::TurboModule> createHeadlessWebSocketModule(
    std::shared_ptr<react::CallInvoker> jsInvoker) {
  return std::make_shared<HeadlessWebSocketModule>(std::move(jsInvoker));
}

void headlessWebSocketReset() {
  std::vector<std::shared_ptr<Connection>> sockets;
  {
    std::lock_guard<std::mutex> lock(connectionsMutex);
    for (auto& entry : connections) {
      sockets.push_back(entry.second);
    }
    connections.clear();
  }
  for (auto& socket : sockets) {
    socket->closeSilently();
  }
}
