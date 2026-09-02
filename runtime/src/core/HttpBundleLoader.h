#pragma once

#include <functional>
#include <stdexcept>
#include <string>

class MetroHttpError : public std::runtime_error {
 public:
  MetroHttpError(unsigned status, std::string body);
  unsigned status{0};
  std::string body;
};

class HttpRequestCancelled : public std::runtime_error {
 public:
  HttpRequestCancelled();
};

// The optional callback is polled during network I/O; true aborts the request
// and throws HttpRequestCancelled.
std::string fetchHttpBundle(
    const std::string& url,
    int timeoutMs = 60000,
    const std::function<bool()>& cancelled = {});
// True when the bundle URL's origin answers Metro's /status probe.
bool isMetroRunning(const std::string& bundleUrl);
// http://host:port for a loopback Metro bundle URL.
std::string metroOrigin(const std::string& bundleUrl);
// Keep host/port/query, replace the bundle path (no leading slash).
std::string replaceMetroBundlePath(
    const std::string& bundleUrl,
    const std::string& path);
