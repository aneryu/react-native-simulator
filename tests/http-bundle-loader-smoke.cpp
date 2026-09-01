#include "HttpBundleLoader.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <exception>
#include <iostream>
#include <string>
#include <thread>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

int main() {
  asio::io_context context;
  tcp::acceptor acceptor(context, {asio::ip::make_address("127.0.0.1"), 0});
  const auto port = acceptor.local_endpoint().port();
  std::exception_ptr serverError;
  std::thread server([&] {
    try {
      tcp::socket socket(context);
      acceptor.accept(socket);
      beast::flat_buffer buffer;
      http::request<http::empty_body> request;
      http::read(socket, buffer, request);
      if (request.target() !=
          "/index.bundle?platform=android&dev=true&minify=false") {
        throw std::runtime_error("unexpected Metro request target");
      }
      http::response<http::string_body> response{http::status::ok, 11};
      response.set(http::field::content_type, "application/javascript");
      response.body() = "globalThis.RNS_METRO_SMOKE = true;";
      response.prepare_payload();
      http::write(socket, response);
    } catch (...) {
      serverError = std::current_exception();
    }
  });

  try {
    const auto body = fetchHttpBundle(
        "http://127.0.0.1:" + std::to_string(port) +
        "/index.bundle?platform=android&dev=true&minify=false");
    server.join();
    if (serverError) {
      std::rethrow_exception(serverError);
    }
    if (body != "globalThis.RNS_METRO_SMOKE = true;") {
      throw std::runtime_error("Metro response body did not round-trip");
    }
    {
      asio::io_context closedContext;
      tcp::acceptor closed(
          closedContext, {asio::ip::make_address("127.0.0.1"), 0});
      const auto closedPort = closed.local_endpoint().port();
      closed.close();
      if (isMetroRunning(
              "http://127.0.0.1:" + std::to_string(closedPort) +
              "/index.bundle?platform=android&dev=true&minify=false")) {
        throw std::runtime_error("closed port was reported as Metro");
      }
    }
    asio::io_context statusContext;
    tcp::acceptor statusAcceptor(
        statusContext, {asio::ip::make_address("127.0.0.1"), 0});
    const auto statusPort = statusAcceptor.local_endpoint().port();
    std::thread statusServer([&] {
      try {
        tcp::socket socket(statusContext);
        statusAcceptor.accept(socket);
        beast::flat_buffer buffer;
        http::request<http::empty_body> request;
        http::read(socket, buffer, request);
        if (request.target() != "/status") {
          throw std::runtime_error("expected Metro /status probe");
        }
        http::response<http::string_body> response{http::status::ok, 11};
        response.set(http::field::content_type, "text/plain");
        response.body() = "packager-status:running";
        response.prepare_payload();
        http::write(socket, response);
      } catch (...) {
        serverError = std::current_exception();
      }
    });
    const bool running = isMetroRunning(
        "http://127.0.0.1:" + std::to_string(statusPort) +
        "/index.bundle?platform=android&dev=true&minify=false");
    statusAcceptor.close();
    statusServer.join();
    if (serverError) {
      std::rethrow_exception(serverError);
    }
    if (!running) {
      throw std::runtime_error("Metro /status was not detected");
    }
    if (metroOrigin(
            "http://localhost:8081/index.bundle?platform=android") !=
        "http://localhost:8081") {
      throw std::runtime_error("metroOrigin did not strip the bundle path");
    }
    if (replaceMetroBundlePath(
            "http://localhost:8081/index.bundle?platform=android&dev=true",
            "js/RNTesterApp.android.bundle") !=
        "http://localhost:8081/js/RNTesterApp.android.bundle?platform=android&dev=true") {
      throw std::runtime_error("replaceMetroBundlePath did not keep query");
    }
  } catch (const std::exception& error) {
    if (server.joinable()) {
      acceptor.close();
      server.join();
    }
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
