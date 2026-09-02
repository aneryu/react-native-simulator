#include "HttpBundleLoader.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

namespace {
struct HttpUrl {
  std::string host;
  std::string port;
  std::string target;
};

HttpUrl parseLoopbackHttpUrl(const std::string& url) {
  constexpr std::string_view scheme = "http://";
  if (!url.starts_with(scheme)) {
    throw std::invalid_argument("Metro URL must use http://");
  }
  const auto authorityStart = scheme.size();
  const auto pathStart = url.find('/', authorityStart);
  const auto authority = url.substr(
      authorityStart,
      pathStart == std::string::npos ? std::string::npos
                                     : pathStart - authorityStart);
  if (authority.empty() || authority.find('@') != std::string::npos) {
    throw std::invalid_argument("Invalid Metro URL authority");
  }
  const auto colon = authority.rfind(':');
  const auto host = colon == std::string::npos
      ? authority
      : authority.substr(0, colon);
  const auto port = colon == std::string::npos
      ? std::string{"80"}
      : authority.substr(colon + 1);
  if ((host != "localhost" && host != "127.0.0.1") || port.empty()) {
    throw std::invalid_argument(
        "Metro URL must target localhost or 127.0.0.1");
  }
  return {
      .host = host,
      .port = port,
      .target = pathStart == std::string::npos ? "/" : url.substr(pathStart),
  };
}
struct LoopbackGet {
  bool connected{false};
  bool cancelled{false};
  unsigned status{0};
  std::string body;
};

LoopbackGet getLoopback(
    const std::string& url,
    std::chrono::milliseconds timeout,
    std::string_view accept,
    const std::function<bool()>& cancelled = {}) {
  const auto parsed = parseLoopbackHttpUrl(url);
  LoopbackGet result;
  asio::io_context context;
  tcp::resolver resolver(context);
  beast::tcp_stream stream(context);
  tcp::resolver::results_type endpoints;
  beast::flat_buffer buffer;
  http::request<http::empty_body> request{
      http::verb::get, parsed.target, 11};
  request.set(http::field::host, parsed.host + ':' + parsed.port);
  request.set(http::field::user_agent, "react-native-simulator");
  request.set(http::field::accept, accept);
  http::response_parser<http::string_body> parser;
  parser.body_limit(512 * 1024 * 1024);

  beast::error_code operationError;
  bool completed = false;
  auto complete = [&](beast::error_code error) {
    operationError = error;
    completed = true;
  };
  resolver.async_resolve(
      parsed.host,
      parsed.port,
      [&](beast::error_code error, tcp::resolver::results_type resolved) {
        if (error) {
          complete(error);
          return;
        }
        endpoints = std::move(resolved);
        stream.async_connect(
            endpoints,
            [&](beast::error_code error, const tcp::endpoint&) {
              if (error) {
                complete(error);
                return;
              }
              http::async_write(
                  stream,
                  request,
                  [&](beast::error_code error, std::size_t) {
                    if (error) {
                      complete(error);
                      return;
                    }
                    http::async_read(
                        stream,
                        buffer,
                        parser,
                        [&](beast::error_code error, std::size_t) {
                          complete(error);
                        });
                  });
            });
      });

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::exception_ptr cancellationError;
  bool cancellationRequested = false;
  bool timedOut = false;
  while (!completed) {
    try {
      cancellationRequested = cancelled && cancelled();
    } catch (...) {
      cancellationError = std::current_exception();
      cancellationRequested = true;
    }
    timedOut = std::chrono::steady_clock::now() >= deadline;
    if (cancellationRequested || timedOut) {
      resolver.cancel();
      beast::error_code ignored;
      stream.socket().cancel(ignored);
      stream.socket().close(ignored);
      context.restart();
      context.run();
      break;
    }
    context.run_for(std::chrono::milliseconds(10));
    if (!completed && context.stopped()) {
      context.restart();
    }
  }

  if (cancellationError) {
    std::rethrow_exception(cancellationError);
  }
  if (cancellationRequested) {
    result.cancelled = true;
    return result;
  }
  if (timedOut || operationError) {
    return result;
  }
  auto response = parser.release();
  result.connected = true;
  result.status = response.result_int();
  result.body = std::move(response.body());
  return result;
}
} // namespace

MetroHttpError::MetroHttpError(unsigned status, std::string body)
    : std::runtime_error(
          "Metro returned HTTP " + std::to_string(status) +
          (body.empty() ? std::string{} : ": " + body.substr(0, 1024))),
      status(status),
      body(std::move(body)) {}

HttpRequestCancelled::HttpRequestCancelled()
    : std::runtime_error("HTTP request cancelled") {}

std::string metroOrigin(const std::string& bundleUrl) {
  const auto parsed = parseLoopbackHttpUrl(bundleUrl);
  return "http://" + parsed.host + ':' + parsed.port;
}

std::string replaceMetroBundlePath(
    const std::string& bundleUrl,
    const std::string& path) {
  auto clean = path;
  if (clean.starts_with('/')) {
    clean.erase(0, 1);
  }
  if (clean.empty()) {
    throw std::invalid_argument("Metro bundle path must not be empty");
  }
  const auto origin = metroOrigin(bundleUrl);
  const auto query = bundleUrl.find('?', std::string("http://").size());
  return origin + "/" + clean +
      (query == std::string::npos ? std::string{} : bundleUrl.substr(query));
}

bool isMetroRunning(const std::string& bundleUrl) {
  const auto status = getLoopback(
      metroOrigin(bundleUrl) + "/status",
      std::chrono::milliseconds(800),
      "text/plain");
  return status.connected && status.status == 200 &&
      status.body.find("packager-status:running") != std::string::npos;
}

std::string fetchHttpBundle(
    const std::string& url,
    int timeoutMs,
    const std::function<bool()>& cancelled) {
  if (timeoutMs < 1) {
    timeoutMs = 1;
  }
  const auto response = getLoopback(
      url,
      std::chrono::milliseconds(timeoutMs),
      "application/javascript",
      cancelled);
  if (response.cancelled) {
    throw HttpRequestCancelled();
  }
  if (!response.connected) {
    throw std::runtime_error("connection failed");
  }
  if (response.status != 200) {
    throw MetroHttpError(response.status, response.body);
  }
  return response.body;
}
