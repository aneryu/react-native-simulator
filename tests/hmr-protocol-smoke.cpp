#include <react-native-simulator/Engine.h>

#include "TestEngineThread.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace {
constexpr std::string_view kBundle = R"JS(
globalThis.__rctDeviceEventEmitter = {
  emit: function (name, event) {
    const list = this._listeners && this._listeners[name];
    if (!list) {
      return;
    }
    for (let i = 0; i < list.length; i += 1) {
      list[i](event);
    }
  },
  addListener: function (name, fn) {
    this._listeners = this._listeners || {};
    this._listeners[name] = this._listeners[name] || [];
    this._listeners[name].push(fn);
  },
};

const HMRClient = {
  setup: function (platform, entry, host, port, enabled) {
    console.log(
        'HMR_SETUP', platform, entry, host, String(port), String(enabled));
    const wsMod = globalThis.nativeModuleProxy.WebSocketModule;
    const socketId = 1;
    const self = this;
    globalThis.__rctDeviceEventEmitter.addListener(
        'websocketOpen', function (event) {
          if (event.id !== socketId) {
            return;
          }
          console.log('HMR_HOT_OPEN');
        });
    globalThis.__rctDeviceEventEmitter.addListener(
        'websocketMessage', function (event) {
          if (event.id !== socketId) {
            return;
          }
          const message = JSON.parse(String(event.data));
          if (message.type !== 'update') {
            return;
          }
          const body = message.body || {};
          const modules = []
              .concat(body.added || [])
              .concat(body.modified || []);
          for (let i = 0; i < modules.length; i += 1) {
            const item = modules[i];
            const code = item.module[1];
            const url = item.sourceURL || 'hmr://module';
            if (typeof globalThis.globalEvalWithSourceUrl === 'function') {
              globalThis.globalEvalWithSourceUrl(code, url);
            } else {
              eval(code);
            }
          }
        });
    wsMod.connect('ws://' + host + ':' + port + '/hot', [], {}, socketId);
  },
};
globalThis.RN$registerCallableModule('HMRClient', function () {
  return HMRClient;
});
console.log(
    'HMR_EVAL',
    typeof globalThis.globalEvalWithSourceUrl);
)JS";

std::string hmrUpdateMessage() {
  return R"({"type":"update","body":{"isInitialUpdate":false,"added":[],"modified":[{"module":[1,"console.log('HMR_INJECTED');"],"sourceURL":"hmr://injected.js"}]}})";
}

class StubMetro {
 public:
  StubMetro() : acceptor_(context_, {asio::ip::make_address("127.0.0.1"), 0}) {
    port_ = acceptor_.local_endpoint().port();
  }

  uint16_t port() const {
    return port_;
  }

  void start() {
    thread_ = std::thread([this] { acceptLoop(); });
  }

  void stop() {
    stopping_ = true;
    boost::system::error_code ignored;
    acceptor_.close(ignored);
    context_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  void sendHotUpdate() {
    std::lock_guard lock(mutex_);
    pendingHot_ = hmrUpdateMessage();
  }

  void sendReload() {
    std::lock_guard lock(mutex_);
    pendingReload_ = true;
  }

  std::string error() const {
    std::lock_guard lock(mutex_);
    return error_;
  }

 private:
  void acceptLoop() {
    while (!stopping_) {
      try {
        tcp::socket socket(context_);
        acceptor_.accept(socket);
        if (stopping_) {
          break;
        }
        std::thread(&StubMetro::handleSocket, this, std::move(socket)).detach();
      } catch (const std::exception&) {
        if (stopping_) {
          break;
        }
      }
    }
  }

  void handleSocket(tcp::socket socket) {
    try {
      beast::flat_buffer buffer;
      http::request<http::string_body> request;
      http::read(socket, buffer, request);
      const std::string target(request.target());
      if (websocket::is_upgrade(request)) {
        websocket::stream<tcp::socket> ws(std::move(socket));
        ws.accept(request);
        if (target.rfind("/hot", 0) == 0) {
          serveHot(ws);
        } else if (target.rfind("/message", 0) == 0) {
          serveMessage(ws);
        }
        return;
      }
      http::response<http::string_body> response{http::status::ok, 11};
      response.set(http::field::content_type, "application/javascript");
      response.body() = std::string(kBundle);
      response.prepare_payload();
      http::write(socket, response);
    } catch (const std::exception&) {
    }
  }

  void serveHot(websocket::stream<tcp::socket>& ws) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (!stopping_ && std::chrono::steady_clock::now() < deadline) {
      std::string pending;
      {
        std::lock_guard lock(mutex_);
        pending.swap(pendingHot_);
      }
      if (!pending.empty()) {
        ws.write(asio::buffer(pending));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  void serveMessage(websocket::stream<tcp::socket>& ws) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (!stopping_ && std::chrono::steady_clock::now() < deadline) {
      bool reload = false;
      {
        std::lock_guard lock(mutex_);
        reload = pendingReload_;
        pendingReload_ = false;
      }
      if (reload) {
        ws.write(asio::buffer(
            std::string(R"({"version":2,"method":"reload"})")));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  asio::io_context context_;
  tcp::acceptor acceptor_;
  uint16_t port_{0};
  std::thread thread_;
  std::atomic<bool> stopping_{false};
  mutable std::mutex mutex_;
  std::string pendingHot_;
  bool pendingReload_{false};
  std::string error_;
};
} // namespace

int main() {
  StubMetro metro;
  metro.start();
  const auto url = "http://127.0.0.1:" + std::to_string(metro.port()) +
      "/index.bundle?platform=android&dev=true&minify=false";

  ReactNativeSimulator::EngineConfig config;
  config.mode = ReactNativeSimulator::SimulatorMode::Interactive;
  config.timeoutMs = 15000;
  config.autoRunApplication = false;
  ReactNativeSimulator::Engine engine(config);
  engine.loadBundle(std::string(kBundle), url);

  std::string runError;
  TestEngineThread runner([&] {
    const auto result = engine.run();
    if (result.exitCode != 0) {
      runError = result.error.empty() ? "engine failed" : result.error;
    }
  });

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  int loadedCycles = 0;
  bool wasLoaded = false;
  bool sentUpdate = false;
  bool sentReload = false;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto state = engine.applicationLaunchState();
    if (state.initialBundlesLoaded && !wasLoaded) {
      ++loadedCycles;
    }
    wasLoaded = state.initialBundlesLoaded;
    if (loadedCycles == 1 && !sentUpdate) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      metro.sendHotUpdate();
      sentUpdate = true;
    }
    if (loadedCycles == 1 && sentUpdate && !sentReload) {
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      metro.sendReload();
      sentReload = true;
    }
    if (loadedCycles >= 2) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  }
  engine.requestStop();
  runner.join();
  metro.stop();
  if (!metro.error().empty()) {
    std::cerr << metro.error() << '\n';
    return 1;
  }
  if (!runError.empty()) {
    std::cerr << runError << '\n';
    return 1;
  }
  if (loadedCycles < 2) {
    std::cerr << "packager reload did not recreate the runtime (loaded cycles="
              << loadedCycles << ")\n";
    return 1;
  }
  return 0;
}
