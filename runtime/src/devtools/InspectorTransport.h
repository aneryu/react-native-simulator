#pragma once

#include <filesystem>
#include <memory>
#include <string>

class InspectorTransport final {
 public:
  static std::shared_ptr<InspectorTransport> connect(
      const std::filesystem::path& socketPath);

  ~InspectorTransport();

  InspectorTransport(const InspectorTransport&) = delete;
  InspectorTransport& operator=(const InspectorTransport&) = delete;

  void sendJson(std::string json);
  void waitForDisconnect();

 private:
  explicit InspectorTransport(int socket);

  class Impl;
  std::unique_ptr<Impl> impl_;
};
