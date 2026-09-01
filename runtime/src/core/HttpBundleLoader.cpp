#include "HttpBundleLoader.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <cstdint>
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
  unsigned status{0};
  std::string body;
};

LoopbackGet getLoopback(
    const std::string& url,
    std::chrono::milliseconds timeout,
    std::string_view accept) {
  const auto parsed = parseLoopbackHttpUrl(url);
  LoopbackGet result;
  try {
    asio::io_context context;
    tcp::resolver resolver(context);
    beast::tcp_stream stream(context);
    stream.expires_after(timeout);
    stream.connect(resolver.resolve(parsed.host, parsed.port));
    http::request<http::empty_body> request{
        http::verb::get, parsed.target, 11};
    request.set(http::field::host, parsed.host + ':' + parsed.port);
    request.set(http::field::user_agent, "react-native-simulator");
    request.set(http::field::accept, accept);
    http::write(stream, request);
    beast::flat_buffer buffer;
    http::response_parser<http::string_body> parser;
    parser.body_limit(512 * 1024 * 1024);
    http::read(stream, buffer, parser);
    auto response = parser.release();
    result.connected = true;
    result.status = response.result_int();
    result.body = std::move(response.body());
  } catch (const std::exception&) {
    return {};
  }
  return result;
}
} // namespace

MetroHttpError::MetroHttpError(unsigned status, std::string body)
    : std::runtime_error(
          "Metro returned HTTP " + std::to_string(status) +
          (body.empty() ? std::string{} : ": " + body.substr(0, 1024))),
      status(status),
      body(std::move(body)) {}

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

std::string fetchHttpBundle(const std::string& url, int timeoutMs) {
  if (timeoutMs < 1) {
    timeoutMs = 1;
  }
  const auto response = getLoopback(
      url,
      std::chrono::milliseconds(timeoutMs),
      "application/javascript");
  if (!response.connected) {
    throw std::runtime_error("connection failed");
  }
  if (response.status != 200) {
    throw MetroHttpError(response.status, response.body);
  }
  return response.body;
}
