#include <react-native-simulator/Engine.h>
#include "EngineTestSupport.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using namespace std::chrono_literals;

namespace {

uint16_t reservePort() {
  asio::io_context context;
  tcp::acceptor acceptor(context, {asio::ip::make_address("127.0.0.1"), 0});
  return acceptor.local_endpoint().port();
}

std::string get(uint16_t port, const std::string& target) {
  asio::io_context context;
  tcp::resolver resolver(context);
  beast::tcp_stream stream(context);
  stream.connect(resolver.resolve("127.0.0.1", std::to_string(port)));
  http::request<http::empty_body> request{http::verb::get, target, 11};
  request.set(http::field::host, "127.0.0.1:" + std::to_string(port));
  http::write(stream, request);
  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(stream, buffer, response);
  if (response.result() != http::status::ok) {
    throw std::runtime_error("DevTools HTTP request failed: " + target);
  }
  return response.body();
}

std::string waitForServer(uint16_t port) {
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (std::chrono::steady_clock::now() < deadline) {
    try {
      const auto targets = get(port, "/json");
      if (targets.find("ws://127.0.0.1:" + std::to_string(port) + "/cdp") !=
          std::string::npos) {
        return targets;
      }
    } catch (const std::exception&) {
    }
    std::this_thread::sleep_for(20ms);
  }
  throw std::runtime_error("Timed out waiting for DevTools discovery");
}

void expectWebSocketRejected(
    uint16_t port,
    const std::string& target,
    const std::string& origin = {}) {
  asio::io_context context;
  tcp::resolver resolver(context);
  websocket::stream<beast::tcp_stream> socket(context);
  beast::get_lowest_layer(socket).expires_after(10s);
  beast::get_lowest_layer(socket).connect(
      resolver.resolve("127.0.0.1", std::to_string(port)));
  if (!origin.empty()) {
    socket.set_option(websocket::stream_base::decorator(
        [&origin](websocket::request_type& request) {
          request.set(http::field::origin, origin);
        }));
  }
  beast::error_code error;
  socket.handshake(
      "127.0.0.1:" + std::to_string(port), target, error);
  if (!error) {
    throw std::runtime_error(
        "DevTools accepted an unauthorized WebSocket: " + target);
  }
}

void send(websocket::stream<beast::tcp_stream>& socket, std::string message) {
  socket.write(asio::buffer(message));
}

std::string receive(websocket::stream<beast::tcp_stream>& socket) {
  beast::flat_buffer buffer;
  socket.read(buffer);
  return beast::buffers_to_string(buffer.data());
}

std::string waitForResponse(
    websocket::stream<beast::tcp_stream>& socket,
    int id) {
  const auto idMarker = "\"id\":" + std::to_string(id);
  while (true) {
    const auto message = receive(socket);
    if (message.find(idMarker) == std::string::npos) {
      continue;
    }
    if (message.find(R"("error")") != std::string::npos) {
      throw std::runtime_error("CDP request failed: " + message);
    }
    return message;
  }
}

std::string jsonStringField(
    const std::string& message,
    const std::string& field) {
  const auto marker = "\"" + field + "\":\"";
  const auto start = message.find(marker);
  if (start == std::string::npos) {
    return {};
  }
  const auto valueStart = start + marker.size();
  const auto end = message.find('"', valueStart);
  return end == std::string::npos
      ? std::string{}
      : message.substr(valueStart, end - valueStart);
}

void exerciseCdp(uint16_t port) {
  const auto discovery = waitForServer(port);
  if (get(port, "/debugger-frontend/rn_fusebox.html").find("devtools") ==
      std::string::npos) {
    throw std::runtime_error("React Native DevTools frontend was not served");
  }

  const auto websocketUrl = jsonStringField(discovery, "webSocketDebuggerUrl");
  const auto host = "127.0.0.1:" + std::to_string(port);
  const auto targetOffset = websocketUrl.find(host);
  if (targetOffset == std::string::npos) {
    throw std::runtime_error("DevTools discovery returned no loopback target");
  }
  const auto websocketTarget = websocketUrl.substr(targetOffset + host.size());
  if (websocketTarget.find("/cdp?token=") != 0) {
    throw std::runtime_error("DevTools discovery returned no session token");
  }
  expectWebSocketRejected(port, "/cdp");
  expectWebSocketRejected(port, websocketTarget, "https://example.invalid");

  asio::io_context context;
  tcp::resolver resolver(context);
  websocket::stream<beast::tcp_stream> socket(context);
  beast::get_lowest_layer(socket).expires_after(10s);
  beast::get_lowest_layer(socket).connect(
      resolver.resolve("127.0.0.1", std::to_string(port)));
  socket.set_option(websocket::stream_base::decorator(
      [&host](websocket::request_type& request) {
        request.set(http::field::origin, "http://" + host);
      }));
  socket.handshake(host, websocketTarget);
  send(socket, R"({"id":100,"method":"ReactNativeApplication.enable"})");
  bool applicationEnabled = false;
  bool metadataReceived = false;
  while (!applicationEnabled || !metadataReceived) {
    const auto message = receive(socket);
    applicationEnabled |=
        message.find(R"("id":100)") != std::string::npos &&
        message.find(R"("result")") != std::string::npos;
    metadataReceived |=
        message.find("ReactNativeApplication.metadataUpdated") !=
            std::string::npos &&
        message.find(R"("unstable_isProfilingBuild":false)") !=
            std::string::npos;
  }
  send(socket, R"({"id":1,"method":"Runtime.enable"})");

  bool runtimeEnabled = false;
  bool executionContextCreated = false;
  while (!runtimeEnabled || !executionContextCreated) {
    const auto message = receive(socket);
    runtimeEnabled |=
        message.find(R"("id":1)") != std::string::npos &&
        message.find(R"("result")") != std::string::npos;
    executionContextCreated |=
        message.find("Runtime.executionContextCreated") != std::string::npos;
  }

  send(
      socket,
      R"({"id":103,"method":"Runtime.addBinding","params":{"name":"__CHROME_DEVTOOLS_FRONTEND_BINDING__"}})");
  waitForResponse(socket, 103);
  send(
      socket,
      R"cdp({"id":104,"method":"Runtime.evaluate","params":{"expression":"globalThis.__CHROME_DEVTOOLS_FRONTEND_BINDING__('rns-react-devtools-binding')"}})cdp");
  bool bindingEvaluated = false;
  bool bindingCalled = false;
  while (!bindingEvaluated || !bindingCalled) {
    const auto message = receive(socket);
    bindingEvaluated |= message.find(R"("id":104)") != std::string::npos &&
        message.find(R"("result")") != std::string::npos;
    bindingCalled |= message.find("Runtime.bindingCalled") !=
            std::string::npos &&
        message.find("__CHROME_DEVTOOLS_FRONTEND_BINDING__") !=
            std::string::npos &&
        message.find("rns-react-devtools-binding") != std::string::npos;
  }

  send(
      socket,
      R"({"id":2,"method":"Runtime.evaluate","params":{"expression":"console.log('rns-cdp-console', {answer: 42}); 21 * 2","returnByValue":true}})");
  bool evaluated = false;
  bool consoleCalled = false;
  while (!evaluated || !consoleCalled) {
    const auto message = receive(socket);
    evaluated |= message.find(R"("id":2)") != std::string::npos &&
        message.find(R"("value":42)") != std::string::npos;
    consoleCalled |= message.find("Runtime.consoleAPICalled") !=
            std::string::npos &&
        message.find("rns-cdp-console") != std::string::npos;
  }

  send(socket, R"({"id":3,"method":"Debugger.enable"})");
  bool debuggerEnabled = false;
  bool scriptParsed = false;
  std::string sourceScriptId;
  while (!debuggerEnabled || !scriptParsed) {
    const auto message = receive(socket);
    debuggerEnabled |= message.find(R"("id":3)") != std::string::npos &&
        message.find(R"("result")") != std::string::npos;
    if (message.find("Debugger.scriptParsed") != std::string::npos &&
        message.find("development-session.js") != std::string::npos) {
      scriptParsed = true;
      sourceScriptId = jsonStringField(message, "scriptId");
    }
  }
  if (sourceScriptId.empty()) {
    throw std::runtime_error("Debugger.scriptParsed returned no script id");
  }
  send(
      socket,
      "{\"id\":101,\"method\":\"Debugger.getScriptSource\","
      "\"params\":{\"scriptId\":\"" + sourceScriptId + "\"}}");
  const auto scriptSource = waitForResponse(socket, 101);
  if (scriptSource.find("RNS_DEVELOPMENT_SESSION") == std::string::npos) {
    throw std::runtime_error("Debugger.getScriptSource returned no source");
  }

  send(
      socket,
      R"({"id":5,"method":"HeapProfiler.startSampling","params":{"samplingInterval":256}})");
  waitForResponse(socket, 5);
  send(
      socket,
      R"({"id":6,"method":"Runtime.evaluate","params":{"expression":"globalThis.RNS_MEMORY_TEST = Array.from({length: 1000}, (_, i) => ({i}));"}})");
  waitForResponse(socket, 6);
  send(socket, R"({"id":7,"method":"HeapProfiler.stopSampling"})");
  const auto heapProfile = waitForResponse(socket, 7);
  if (heapProfile.find(R"("profile")") == std::string::npos) {
    throw std::runtime_error("HeapProfiler returned no sampling profile");
  }
  send(socket, R"({"id":102,"method":"HeapProfiler.takeHeapSnapshot"})");
  bool heapSnapshotComplete = false;
  bool heapSnapshotChunk = false;
  while (!heapSnapshotComplete || !heapSnapshotChunk) {
    const auto message = receive(socket);
    if (message.find(R"("id":102)") != std::string::npos) {
      if (message.find(R"("error")") != std::string::npos) {
        throw std::runtime_error("Heap snapshot failed: " + message);
      }
      heapSnapshotComplete = true;
    }
    heapSnapshotChunk |=
        message.find("HeapProfiler.addHeapSnapshotChunk") != std::string::npos;
  }

  send(socket, R"({"id":8,"method":"Debugger.disable"})");
  waitForResponse(socket, 8);
  send(
      socket,
      R"({"id":9,"method":"Tracing.start","params":{"categories":"disabled-by-default-devtools.timeline,disabled-by-default-v8.cpu_profiler,v8.execute,devtools.timeline,blink.user_timing"}})");
  waitForResponse(socket, 9);
  send(
      socket,
      R"({"id":10,"method":"Runtime.evaluate","params":{"expression":"for (let i = 0; i < 10000; i++) Math.sqrt(i);"}})");
  waitForResponse(socket, 10);
  send(socket, R"({"id":11,"method":"Tracing.end"})");
  bool tracingEnded = false;
  bool tracingComplete = false;
  bool traceData = false;
  while (!tracingEnded || !tracingComplete || !traceData) {
    const auto message = receive(socket);
    tracingEnded |= message.find(R"("id":11)") != std::string::npos &&
        message.find(R"("result")") != std::string::npos;
    tracingComplete |=
        message.find("Tracing.tracingComplete") != std::string::npos;
    traceData |= message.find("Tracing.dataCollected") != std::string::npos;
  }
  send(
      socket,
      R"({"id":105,"method":"Runtime.evaluate","params":{"expression":"globalThis.RNS_DEVELOPMENT_SESSION.ticks > 0","returnByValue":true}})");
  const auto liveEventLoop = waitForResponse(socket, 105);
  if (liveEventLoop.find(R"("value":true)") == std::string::npos) {
    throw std::runtime_error(
        "Development session stopped advancing the event loop");
  }
  beast::error_code ignored;
  socket.close(websocket::close_code::normal, ignored);
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: devtools-smoke <fixture.js> <frontend-directory>\n";
    return 2;
  }

  const auto port = reservePort();
  ReactNativeSimulator::EngineConfig config;
  config.iterations = 5;
  config.timeoutMs = 5000;
  config.mode = ReactNativeSimulator::SimulatorMode::Interactive;
  config.devTools.enabled = true;
  config.devTools.port = port;
  config.devTools.waitForDebuggerMs = 5000;
  config.devTools.waitForDisconnect = true;
  config.devTools.frontendDirectory = argv[2];

  std::exception_ptr clientError;
  std::thread client([&] {
    try {
      exerciseCdp(port);
    } catch (...) {
      clientError = std::current_exception();
    }
  });

  auto runtime = ReactNativeSimulator::test::makeEngine(
      std::move(config),
      {ReactNativeSimulator::test::fileBundle(argv[1])});
  const auto result = runtime.run();
  client.join();

  if (clientError) {
    try {
      std::rethrow_exception(clientError);
    } catch (const std::exception& error) {
      std::cerr << error.what() << '\n';
    }
    if (!result.error.empty()) {
      std::cerr << result.error << '\n';
    }
    return 1;
  }
  if (result.exitCode != 0) {
    std::cerr << result.error << '\n' << result.metricsJson << '\n';
    return result.exitCode;
  }
  if (result.metricsJson.find(R"("devToolsEnabled":true)") ==
          std::string::npos ||
      result.metricsJson.find(R"("devToolsSessionAttached":true)") ==
          std::string::npos ||
      result.metricsJson.find(R"("validationMode":"development")") ==
          std::string::npos) {
    std::cerr << result.metricsJson << '\n';
    return 1;
  }
  return 0;
}
