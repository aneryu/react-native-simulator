#include "DevToolsHost.h"
#include "HostPlatform.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <folly/json.h>
#include <jsinspector-modern/InspectorInterfaces.h>
#include <react/threading/TaskDispatchThread.h>

#include <poll.h>
#include <spawn.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

extern char** environ;

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
namespace inspector = facebook::react::jsinspector_modern;
using tcp = asio::ip::tcp;

namespace {

std::filesystem::path executablePath() {
  return hostExecutablePath();
}

std::filesystem::path resolveFrontendDirectory(
    const ReactNativeSimulator::DevToolsConfig& config) {
  if (config.frontendDirectory) {
    return std::filesystem::weakly_canonical(*config.frontendDirectory);
  }
  if (const char* environment = std::getenv("RNS_DEVTOOLS_FRONTEND_DIR")) {
    return std::filesystem::weakly_canonical(environment);
  }
  const auto installed = executablePath().parent_path().parent_path() /
      "share/react-native-simulator/debugger-frontend";
  if (std::filesystem::exists(installed / "rn_fusebox.html")) {
    return std::filesystem::weakly_canonical(installed);
  }
#if RNS_ENABLE_SOURCE_DEVTOOLS_FRONTEND
  const std::filesystem::path sourceFrontend = RNS_DEBUGGER_FRONTEND_DIR;
  if (std::filesystem::exists(sourceFrontend / "rn_fusebox.html")) {
    return std::filesystem::weakly_canonical(sourceFrontend);
  }
#endif
  throw std::runtime_error(
      "React Native DevTools frontend is not bundled. Supply a compatible "
      "frontend with --devtools-frontend-dir DIR or "
      "RNS_DEVTOOLS_FRONTEND_DIR.");
}

std::string mimeType(const std::filesystem::path& path) {
  const auto extension = path.extension().string();
  if (extension == ".html") return "text/html; charset=utf-8";
  if (extension == ".js" || extension == ".mjs") {
    return "text/javascript; charset=utf-8";
  }
  if (extension == ".css") return "text/css; charset=utf-8";
  if (extension == ".json" || extension == ".map") {
    return "application/json";
  }
  if (extension == ".svg") return "image/svg+xml";
  if (extension == ".png") return "image/png";
  if (extension == ".woff2") return "font/woff2";
  return "application/octet-stream";
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Cannot open DevTools asset: " + path.string());
  }
  return {
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>()};
}

std::string withoutQuery(beast::string_view target) {
  const auto query = target.find('?');
  return std::string(target.substr(0, query));
}

std::optional<std::string> queryParameter(
    beast::string_view target,
    std::string_view name) {
  const auto query = target.find('?');
  if (query == beast::string_view::npos) return std::nullopt;
  auto remaining = target.substr(query + 1);
  while (!remaining.empty()) {
    const auto separator = remaining.find('&');
    const auto field = remaining.substr(0, separator);
    const auto equals = field.find('=');
    if (equals != beast::string_view::npos &&
        field.substr(0, equals) == name) {
      return std::string(field.substr(equals + 1));
    }
    if (separator == beast::string_view::npos) break;
    remaining.remove_prefix(separator + 1);
  }
  return std::nullopt;
}

std::string randomSessionToken() {
  std::array<unsigned char, 32> bytes{};
  ::arc4random_buf(bytes.data(), bytes.size());
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const auto byte : bytes) {
    stream << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return stream.str();
}

std::optional<std::filesystem::path> assetPath(
    const std::filesystem::path& root,
    beast::string_view target) {
  constexpr std::string_view prefix = "/debugger-frontend/";
  const auto requestPath = withoutQuery(target);
  if (!requestPath.starts_with(prefix)) {
    return std::nullopt;
  }
  const auto relative = std::filesystem::path(
      requestPath.substr(prefix.size())).lexically_normal();
  if (relative.empty() || relative.is_absolute()) {
    return std::nullopt;
  }
  for (const auto& component : relative) {
    if (component == "..") return std::nullopt;
  }
  const auto candidate = std::filesystem::weakly_canonical(root / relative);
  const auto rootString = root.string() + std::filesystem::path::preferred_separator;
  if (!candidate.string().starts_with(rootString)) {
    return std::nullopt;
  }
  return candidate;
}

void launchProcess(const std::filesystem::path& executable,
                   const std::vector<std::string>& arguments) {
  std::vector<std::string> storage;
  storage.reserve(arguments.size() + 1);
  storage.push_back(executable.string());
  storage.insert(storage.end(), arguments.begin(), arguments.end());
  std::vector<char*> argv;
  argv.reserve(storage.size() + 1);
  for (auto& item : storage) argv.push_back(item.data());
  argv.push_back(nullptr);
  pid_t pid = 0;
  const int result = posix_spawn(
      &pid, executable.c_str(), nullptr, nullptr, argv.data(), environ);
  if (result != 0) {
    throw std::runtime_error(
        "Cannot launch DevTools process: " + std::to_string(result));
  }
}

class CdpSession;

class RemoteConnection final : public inspector::IRemoteConnection {
 public:
  explicit RemoteConnection(std::weak_ptr<CdpSession> session)
      : session_(std::move(session)) {}
  void onMessage(std::string message) override;
  void onDisconnect() override;

 private:
  std::weak_ptr<CdpSession> session_;
};

class CdpSession final : public std::enable_shared_from_this<CdpSession> {
 public:
  using SourceLookup =
      std::function<std::optional<std::string>(const std::string&)>;

  CdpSession(
      tcp::socket socket,
      int pageId,
      std::atomic<size_t>& activeSessions,
      std::atomic<bool>& hadSession,
      SourceLookup sourceLookup)
      : webSocket_(std::move(socket)),
        pageId_(pageId),
        activeSessions_(activeSessions),
        hadSession_(hadSession),
        sourceLookup_(std::move(sourceLookup)) {}

  void run(http::request<http::string_body> request) {
    beast::error_code error;
    webSocket_.set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::server));
    webSocket_.accept(request, error);
    if (error) return;

    local_ = inspector::getInspectorInstance().connect(
        pageId_, std::make_unique<RemoteConnection>(weak_from_this()));
    if (!local_) return;
    ++activeSessions_;
    hadSession_ = true;

    while (!closed_) {
      flushOutgoing();
      pollfd descriptor{
          .fd = webSocket_.next_layer().native_handle(),
          .events = POLLIN,
          .revents = 0,
      };
      const int ready = ::poll(&descriptor, 1, 10);
      if (ready < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        break;
      }
      if (ready == 0 || !(descriptor.revents & POLLIN)) continue;
      beast::flat_buffer buffer;
      webSocket_.read(buffer, error);
      if (error) break;
      auto message = beast::buffers_to_string(buffer.data());
      if (!handleSourceRequest(message)) {
        local_->sendMessage(std::move(message));
      }
    }

    if (local_) {
      local_->disconnect();
      local_.reset();
      --activeSessions_;
    }
    beast::error_code ignored;
    webSocket_.next_layer().shutdown(tcp::socket::shutdown_both, ignored);
    webSocket_.next_layer().close(ignored);
  }

  void enqueue(std::string message) {
    if (message.find("\"method\":\"Debugger.scriptParsed\"") !=
        std::string::npos) {
      try {
        const auto parsed = folly::parseJson(message);
        const auto& params = parsed.at("params");
        const auto scriptId = params.getDefault("scriptId", "").asString();
        const auto url = params.getDefault("url", "").asString();
        if (!scriptId.empty() && !url.empty()) {
          std::lock_guard sourceLock(sourceMutex_);
          scriptUrls_[scriptId] = url;
        }
      } catch (const std::exception&) {
      }
    }
    std::lock_guard lock(mutex_);
    outgoing_.push_back(std::move(message));
  }

  void close() {
    closed_ = true;
    beast::error_code ignored;
    webSocket_.next_layer().cancel(ignored);
    webSocket_.next_layer().close(ignored);
  }

 private:
  bool handleSourceRequest(const std::string& message) {
    if (message.find("Debugger.getScriptSource") == std::string::npos) {
      return false;
    }
    try {
      const auto request = folly::parseJson(message);
      if (request.getDefault("method", "").asString() !=
          "Debugger.getScriptSource") {
        return false;
      }
      const auto id = request.at("id").asInt();
      const auto scriptId = request.at("params").at("scriptId").asString();
      std::string sourceUrl;
      {
        std::lock_guard sourceLock(sourceMutex_);
        if (const auto found = scriptUrls_.find(scriptId);
            found != scriptUrls_.end()) {
          sourceUrl = found->second;
        }
      }
      const auto source = sourceUrl.empty()
          ? std::optional<std::string>{}
          : sourceLookup_(sourceUrl);
      folly::dynamic response = folly::dynamic::object("id", id);
      if (source) {
        response["result"] =
            folly::dynamic::object("scriptSource", *source);
      } else {
        response["error"] = folly::dynamic::object
            ("code", -32000)
            ("message", "Source is unavailable for " + sourceUrl);
      }
      enqueue(folly::toJson(response));
      return true;
    } catch (const std::exception&) {
      return false;
    }
  }

  void flushOutgoing() {
    std::deque<std::string> messages;
    {
      std::lock_guard lock(mutex_);
      messages.swap(outgoing_);
    }
    beast::error_code error;
    for (const auto& message : messages) {
      webSocket_.text(true);
      webSocket_.write(asio::buffer(message), error);
      if (error) {
        closed_ = true;
        return;
      }
    }
  }

  websocket::stream<tcp::socket> webSocket_;
  int pageId_;
  std::atomic<size_t>& activeSessions_;
  std::atomic<bool>& hadSession_;
  std::unique_ptr<inspector::ILocalConnection> local_;
  SourceLookup sourceLookup_;
  std::mutex mutex_;
  std::deque<std::string> outgoing_;
  std::mutex sourceMutex_;
  std::unordered_map<std::string, std::string> scriptUrls_;
  std::atomic<bool> closed_{false};
};

void RemoteConnection::onMessage(std::string message) {
  if (auto session = session_.lock()) session->enqueue(std::move(message));
}

void RemoteConnection::onDisconnect() {
  if (auto session = session_.lock()) session->close();
}

class StandaloneServer final {
 public:
  StandaloneServer(
      uint16_t port,
      int pageId,
      std::filesystem::path frontendDirectory,
      std::string appName,
      std::string sessionToken,
      std::atomic<size_t>& activeSessions,
      std::atomic<bool>& hadSession,
      CdpSession::SourceLookup sourceLookup)
      : acceptor_(ioContext_),
        pageId_(pageId),
        frontendDirectory_(std::move(frontendDirectory)),
        appName_(std::move(appName)),
        sessionToken_(std::move(sessionToken)),
        activeSessions_(activeSessions),
        hadSession_(hadSession),
        sourceLookup_(std::move(sourceLookup)) {
    if (!std::filesystem::exists(frontendDirectory_ / "rn_fusebox.html")) {
      throw std::runtime_error(
          "React Native DevTools frontend not found at " +
          frontendDirectory_.string());
    }
    beast::error_code error;
    const tcp::endpoint endpoint{asio::ip::make_address("127.0.0.1"), port};
    acceptor_.open(endpoint.protocol(), error);
    if (error) throw std::runtime_error(error.message());
    acceptor_.set_option(asio::socket_base::reuse_address(true), error);
    acceptor_.bind(endpoint, error);
    if (error) {
      throw std::runtime_error(
          "Cannot bind DevTools server to 127.0.0.1:" +
          std::to_string(port) + ": " + error.message());
    }
    acceptor_.listen(asio::socket_base::max_listen_connections, error);
    if (error) throw std::runtime_error(error.message());
    acceptor_.non_blocking(true, error);
    thread_ = std::thread([this] { acceptLoop(); });
  }

  ~StandaloneServer() {
    stopAccepting();
    if (thread_.joinable()) thread_.join();
    std::vector<std::shared_ptr<CdpSession>> sessions;
    {
      std::lock_guard lock(sessionsMutex_);
      sessions = sessions_;
    }
    for (const auto& session : sessions) session->close();
    for (auto& worker : workers_) {
      if (worker.joinable()) worker.join();
    }
  }

  void stopAccepting() {
    stopped_ = true;
    beast::error_code ignored;
    acceptor_.close(ignored);
  }

 private:
  void acceptLoop() {
    while (!stopped_) {
      beast::error_code error;
      tcp::socket socket(ioContext_);
      acceptor_.accept(socket, error);
      if (error == asio::error::would_block || error == asio::error::try_again) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        continue;
      }
      if (error) {
        if (!stopped_) std::cerr << "DevTools accept failed: " << error.message() << '\n';
        continue;
      }
      handleConnection(std::move(socket));
    }
  }

  void handleConnection(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request<http::string_body> request;
    beast::error_code error;
    http::read(socket, buffer, request, error);
    if (error) return;
    const auto expectedHost = "127.0.0.1:" +
        std::to_string(acceptor_.local_endpoint().port());
    const auto origin = request[http::field::origin];
    const bool allowedOrigin = origin.empty() ||
        origin == "http://" + expectedHost || origin == "devtools://devtools";
    const bool authorized =
        request[http::field::host] == expectedHost && allowedOrigin &&
        queryParameter(request.target(), "token") == sessionToken_;
    if (websocket::is_upgrade(request) &&
        withoutQuery(request.target()) == "/cdp" && authorized) {
      auto session = std::make_shared<CdpSession>(
          std::move(socket), pageId_, activeSessions_, hadSession_,
          sourceLookup_);
      {
        std::lock_guard lock(sessionsMutex_);
        sessions_.push_back(session);
      }
      workers_.emplace_back(
          [session, request = std::move(request)]() mutable {
            session->run(std::move(request));
          });
      return;
    }
    if (websocket::is_upgrade(request) &&
        withoutQuery(request.target()) == "/cdp") {
      http::response<http::string_body> response;
      response.version(request.version());
      response.result(http::status::forbidden);
      response.set(http::field::content_type, "text/plain; charset=utf-8");
      response.body() = "DevTools WebSocket authorization failed";
      response.prepare_payload();
      beast::error_code ignored;
      http::write(socket, response, ignored);
      socket.shutdown(tcp::socket::shutdown_both, ignored);
      socket.close(ignored);
      return;
    }
    serveHttp(socket, request);
  }

  void serveHttp(
      tcp::socket& socket,
      const http::request<http::string_body>& request) {
    const auto host = "127.0.0.1:" +
        std::to_string(acceptor_.local_endpoint().port());
    const auto websocketUrl =
        "ws://" + host + "/cdp?token=" + sessionToken_;
    const auto frontendUrl = "http://" + host +
        "/debugger-frontend/rn_fusebox.html?ws=" + host +
        "/cdp%3Ftoken%3D" + sessionToken_ +
        "&sources.hide_add_folder=true&appId=" + appName_;
    http::response<http::string_body> response;
    response.version(request.version());
    response.keep_alive(false);
    const auto path = withoutQuery(request.target());
    if (request[http::field::host] != host) {
      response.result(http::status::bad_request);
      response.body() = "Invalid Host header";
    } else if (request.method() != http::verb::get) {
      response.result(http::status::method_not_allowed);
      response.body() = "Only GET is supported";
    } else if (path == "/json" || path == "/json/list") {
      folly::dynamic target = folly::dynamic::object
          ("id", "react-native-simulator")
          ("title", appName_)
          ("description", "React Native Simulator Hermes runtime")
          ("type", "node")
          ("webSocketDebuggerUrl", websocketUrl)
          ("devtoolsFrontendUrl", frontendUrl);
      response.result(http::status::ok);
      response.set(http::field::content_type, "application/json");
      response.body() = folly::toJson(folly::dynamic::array(std::move(target)));
    } else if (path == "/json/version") {
      response.result(http::status::ok);
      response.set(http::field::content_type, "application/json");
      response.body() = folly::toJson(folly::dynamic::object
          ("Browser", "React Native Simulator")
          ("Protocol-Version", "1.3"));
    } else if (path == "/") {
      response.result(http::status::found);
      response.set(http::field::location, frontendUrl);
    } else if (const auto asset = assetPath(frontendDirectory_, request.target());
               asset && std::filesystem::is_regular_file(*asset)) {
      response.result(http::status::ok);
      response.set(http::field::content_type, mimeType(*asset));
      response.body() = readFile(*asset);
    } else {
      response.result(http::status::not_found);
      response.body() = "Not found";
    }
    response.prepare_payload();
    beast::error_code ignored;
    http::write(socket, response, ignored);
    socket.shutdown(tcp::socket::shutdown_both, ignored);
    socket.close(ignored);
  }

  asio::io_context ioContext_;
  tcp::acceptor acceptor_;
  int pageId_;
  std::filesystem::path frontendDirectory_;
  std::string appName_;
  std::string sessionToken_;
  std::atomic<size_t>& activeSessions_;
  std::atomic<bool>& hadSession_;
  CdpSession::SourceLookup sourceLookup_;
  std::atomic<bool> stopped_{false};
  std::thread thread_;
  std::mutex sessionsMutex_;
  std::vector<std::shared_ptr<CdpSession>> sessions_;
  std::vector<std::thread> workers_;
};

} // namespace

class DevToolsHost::Impl {
 public:
  facebook::react::TaskDispatchThread inspectorThread{"RNHInspector"};
  std::shared_ptr<inspector::HostTarget> target;
  std::optional<int> pageId;
  std::unique_ptr<StandaloneServer> server;
  std::atomic<size_t> activeSessions{0};
  std::atomic<bool> hadSession{false};
  std::mutex sourcesMutex;
  std::unordered_map<std::string, std::string> sources;
  std::string frontendUrl;
  std::string sessionToken;
  std::mutex reloadMutex;
  std::function<void()> onReload;
};

DevToolsHost::DevToolsHost(
    ReactNativeSimulator::DevToolsConfig config)
    : config_(std::move(config)), impl_(std::make_unique<Impl>()) {}

std::shared_ptr<DevToolsHost> DevToolsHost::start(
    const ReactNativeSimulator::DevToolsConfig& config) {
  auto result = std::shared_ptr<DevToolsHost>(new DevToolsHost(config));
  result->initialize();
  return result;
}

void DevToolsHost::initialize() {
  const auto frontendDirectory = resolveFrontendDirectory(config_);
  impl_->sessionToken = randomSessionToken();
  impl_->target = inspector::HostTarget::create(
      *this,
      [weak = weak_from_this()](std::function<void()>&& callback) {
        if (auto self = weak.lock()) {
          self->impl_->inspectorThread.runAsync(std::move(callback));
        }
      });
  impl_->pageId = inspector::getInspectorInstance().addPage(
      config_.appName,
      "Hermes",
      [weakTarget = std::weak_ptr(impl_->target)](
          std::unique_ptr<inspector::IRemoteConnection> remote) {
        if (auto target = weakTarget.lock()) {
          return target->connect(std::move(remote));
        }
        return std::unique_ptr<inspector::ILocalConnection>{};
      });
  impl_->server = std::make_unique<StandaloneServer>(
      config_.port,
      *impl_->pageId,
      frontendDirectory,
      config_.appName,
      impl_->sessionToken,
      impl_->activeSessions,
      impl_->hadSession,
      [weak = weak_from_this()](const std::string& sourceUrl) {
        if (const auto self = weak.lock()) {
          std::lock_guard lock(self->impl_->sourcesMutex);
          if (const auto found = self->impl_->sources.find(sourceUrl);
              found != self->impl_->sources.end()) {
            return std::optional<std::string>{found->second};
          }
        }
        return std::optional<std::string>{};
      });
  const auto host = "127.0.0.1:" + std::to_string(config_.port);
  impl_->frontendUrl = "http://" + host +
      "/debugger-frontend/rn_fusebox.html?ws=" + host +
      "/cdp%3Ftoken%3D" + impl_->sessionToken +
      "&sources.hide_add_folder=true&appId=" + config_.appName;
  std::cerr << "React Native DevTools: " << impl_->frontendUrl << '\n';

  if (config_.open) {
    auto shell = config_.shellPath;
    const std::filesystem::path installedShell =
        "/Applications/React Native DevTools.app/Contents/MacOS/React Native DevTools";
    if (!shell && std::filesystem::exists(installedShell)) shell = installedShell;
    if (shell) {
      launchProcess(
          *shell,
          {"--frontendUrl=" + impl_->frontendUrl,
           "--windowKey=react-native-simulator-" +
               std::to_string(config_.port)});
    } else {
      launchProcess("/usr/bin/open", {impl_->frontendUrl});
    }
  }
}

DevToolsHost::~DevToolsHost() {
  if (!impl_) return;
  if (impl_->server) impl_->server->stopAccepting();
  if (impl_->pageId) {
    inspector::getInspectorInstance().removePage(*impl_->pageId);
    impl_->pageId.reset();
  }
  impl_->server.reset();
  impl_->inspectorThread.runSync([this] { impl_->target.reset(); });
  impl_->inspectorThread.quit();
}

inspector::HostTarget* DevToolsHost::target() const {
  return impl_->target.get();
}

bool DevToolsHost::hasSession() const {
  return impl_->activeSessions > 0;
}

bool DevToolsHost::hadSession() const {
  return impl_->hadSession;
}

const std::string& DevToolsHost::frontendUrl() const {
  return impl_->frontendUrl;
}

void DevToolsHost::registerSource(
    std::string sourceUrl,
    std::string source) {
  std::lock_guard lock(impl_->sourcesMutex);
  impl_->sources.insert_or_assign(std::move(sourceUrl), std::move(source));
}

void DevToolsHost::setOnReload(std::function<void()> callback) {
  std::lock_guard lock(impl_->reloadMutex);
  impl_->onReload = std::move(callback);
}

inspector::HostTargetMetadata DevToolsHost::getMetadata() {
  return {
      .appDisplayName = config_.appName,
      .appIdentifier = config_.appName,
      .deviceName = config_.deviceName,
      .integrationName = "RNHOST",
      .platform = hostOsName(),
      .reactNativeVersion = RNS_REACT_NATIVE_VERSION,
  };
}

void DevToolsHost::onReload(const PageReloadRequest&) {
  std::function<void()> callback;
  {
    std::lock_guard lock(impl_->reloadMutex);
    callback = impl_->onReload;
  }
  if (callback) {
    callback();
    return;
  }
  std::cerr << "React Native DevTools requested reload; headless sessions "
               "require a new process\n";
}

void DevToolsHost::onSetPausedInDebuggerMessage(
    const OverlaySetPausedInDebuggerMessageRequest& request) {
  if (request.message) {
    std::cerr << "JavaScript paused in React Native DevTools: "
              << *request.message << '\n';
  }
}
