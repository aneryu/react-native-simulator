#pragma once

#include <functional>
#include <memory>
#include <string>

// Metro PackagerConnection client for ws://host:port/message.
// Connect failures are silent so local --bundle sessions stay usable.
class PackagerConnection final {
 public:
  using ReloadCallback = std::function<void()>;

  static std::unique_ptr<PackagerConnection> connect(
      std::string url,
      ReloadCallback onReload);

  ~PackagerConnection();
  PackagerConnection(const PackagerConnection&) = delete;
  PackagerConnection& operator=(const PackagerConnection&) = delete;

 private:
  class Impl;
  explicit PackagerConnection(std::shared_ptr<Impl> impl);

  std::shared_ptr<Impl> impl_;
};
