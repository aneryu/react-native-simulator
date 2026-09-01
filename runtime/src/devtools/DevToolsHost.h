#pragma once

#include <react-native-simulator/Engine.h>

#include <jsinspector-modern/HostTarget.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

class DevToolsHost final
    : public facebook::react::jsinspector_modern::HostTargetDelegate,
      public std::enable_shared_from_this<DevToolsHost> {
 public:
  static std::shared_ptr<DevToolsHost> start(
      const ReactNativeSimulator::DevToolsConfig& config);
  ~DevToolsHost() override;

  facebook::react::jsinspector_modern::HostTarget* target() const;
  bool hasSession() const;
  bool hadSession() const;
  const std::string& frontendUrl() const;
  void registerSource(std::string sourceUrl, std::string source);
  void setOnReload(std::function<void()> callback);

  facebook::react::jsinspector_modern::HostTargetMetadata getMetadata()
      override;
  void onReload(const PageReloadRequest& request) override;
  void onSetPausedInDebuggerMessage(
      const OverlaySetPausedInDebuggerMessageRequest& request) override;

 private:
  class Impl;

  explicit DevToolsHost(
      ReactNativeSimulator::DevToolsConfig config);
  void initialize();

  ReactNativeSimulator::DevToolsConfig config_;
  std::unique_ptr<Impl> impl_;
};
