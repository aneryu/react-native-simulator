#include "PackagerConnection.h"

#include <folly/json.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace {
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
  if (const auto colon = authority.rfind(':'); colon != std::string::npos) {
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

void shutdownNative(tcp::socket& socket) {
  if (!socket.is_open()) {
    return;
  }
  const auto fd = socket.native_handle();
  if (fd >= 0) {
    ::shutdown(fd, SHUT_RDWR);
  }
}

void interruptibleSleep(std::atomic<bool>& stopped, std::chrono::milliseconds total) {
  const auto deadline = std::chrono::steady_clock::now() + total;
  while (!stopped.load() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}
} // namespace

class PackagerConnection::Impl {
 public:
  std::atomic<bool> stopped{false};
  PackagerConnection::ReloadCallback onReload;
  std::mutex mutex;
  tcp::socket* socket{nullptr};
  std::thread worker;

  void cancelSocket() {
    std::lock_guard<std::mutex> lock(mutex);
    if (socket != nullptr) {
      beast::error_code error;
      shutdownNative(*socket);
      socket->close(error);
    }
  }

  ~Impl() {
    stopped.store(true);
    cancelSocket();
    if (worker.joinable()) {
      worker.join();
    }
  }
};

std::unique_ptr<PackagerConnection> PackagerConnection::connect(
    std::string url,
    ReloadCallback onReload) {
  auto impl = std::make_shared<Impl>();
  impl->onReload = std::move(onReload);
  impl->worker = std::thread([impl, url = std::move(url)] {
    bool initialConnection = true;
    while (!impl->stopped.load()) {
      const auto parsed = parseWsUrl(url);
      if (!parsed || parsed->tls) {
        // Metro packager connections are loopback ws://. Fail closed on wss.
        interruptibleSleep(impl->stopped, std::chrono::seconds(5));
        continue;
      }
      try {
        asio::io_context context;
        tcp::resolver resolver(context);
        websocket::stream<tcp::socket> stream(context);
        {
          std::lock_guard<std::mutex> lock(impl->mutex);
          impl->socket = &stream.next_layer();
        }
        auto endpoints = resolver.resolve(parsed->host, parsed->port);
        asio::connect(stream.next_layer(), endpoints.begin(), endpoints.end());
        stream.set_option(websocket::stream_base::timeout::suggested(
            beast::role_type::client));
        stream.set_option(websocket::stream_base::decorator(
            [origin = originFor(*parsed)](websocket::request_type& request) {
              request.set(beast::http::field::origin, origin);
            }));
        stream.handshake(parsed->host + ':' + parsed->port, parsed->target);
        if (!initialConnection && impl->onReload) {
          impl->onReload();
        }
        initialConnection = false;
        while (!impl->stopped.load()) {
          beast::flat_buffer buffer;
          stream.read(buffer);
          if (impl->stopped.load()) {
            break;
          }
          const auto text = beast::buffers_to_string(buffer.data());
          try {
            auto json = folly::parseJson(text);
            if (!json.isObject() || json.getDefault("version") != 2) {
              continue;
            }
            if (json.getDefault("method") == "reload" && impl->onReload) {
              impl->onReload();
            }
          } catch (const std::exception&) {
          }
        }
        beast::error_code error;
        stream.close(websocket::close_code::normal, error);
      } catch (const std::exception&) {
      }
      {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->socket = nullptr;
      }
      if (!impl->stopped.load()) {
        interruptibleSleep(impl->stopped, std::chrono::seconds(5));
      }
    }
  });
  return std::unique_ptr<PackagerConnection>(
      new PackagerConnection(std::move(impl)));
}

PackagerConnection::PackagerConnection(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

PackagerConnection::~PackagerConnection() = default;
