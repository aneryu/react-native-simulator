#pragma once

#include <stdexcept>
#include <string>

class MetroHttpError : public std::runtime_error {
 public:
  MetroHttpError(unsigned status, std::string body);
  unsigned status{0};
  std::string body;
};

std::string fetchHttpBundle(
    const std::string& url,
    int timeoutMs = 60000);
// True when the bundle URL's origin answers Metro's /status probe.
bool isMetroRunning(const std::string& bundleUrl);
// http://host:port for a loopback Metro bundle URL.
std::string metroOrigin(const std::string& bundleUrl);
// Keep host/port/query, replace the bundle path (no leading slash).
std::string replaceMetroBundlePath(
    const std::string& bundleUrl,
    const std::string& path);
