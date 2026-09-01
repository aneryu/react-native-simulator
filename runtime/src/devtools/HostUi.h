#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Host chrome for RN APIs that need a dialog or sheet. Not Fabric.
// Headless uses mock handlers (default: auto-complete). Interactive may
// defer to the ImGui frontend via setDeferToFrontend(true).

enum class HostUiKind {
  Alert,
  Share,
  Permission,
  OpenUrl,
  OpenSettings,
  Vibration,
};

struct HostUiAlert {
  std::string title;
  std::string message;
  std::string buttonPositive{"OK"};
  std::string buttonNegative;
  std::string buttonNeutral;
  std::vector<std::string> items;
  bool cancelable{true};
};

struct HostUiAlertResult {
  bool dismissed{false};
  int buttonKey{-1}; // -1 positive, -2 negative, -3 neutral; item index if items
};

struct HostUiShare {
  std::string dialogTitle;
  std::string title;
  std::string message;
};

struct HostUiShareResult {
  bool shared{true};
};

struct HostUiPermission {
  std::vector<std::string> permissions;
};

struct HostUiOpenUrl {
  std::string url;
  bool settings{false};
};

struct HostUiVibration {
  int durationMs{400};
};

struct HostUiController {
  using AlertMock = std::function<HostUiAlertResult(const HostUiAlert&)>;
  using ShareMock = std::function<HostUiShareResult(const HostUiShare&)>;
  using PermissionMock =
      std::function<std::string(const std::string& permission)>;
  using OpenUrlMock = std::function<std::optional<std::string>(
      const HostUiOpenUrl&)>; // nullopt = opened, string = reject reason
  using VibrationMock = std::function<void(const HostUiVibration&)>;

  std::function<void()> onInvalidate;

  // Tests / CI: replace the default auto-complete mocks.
  void setMockAlert(AlertMock mock);
  void setMockShare(ShareMock mock);
  void setMockPermission(PermissionMock mock);
  void setMockOpenUrl(OpenUrlMock mock);
  void setMockVibration(VibrationMock mock);

  // Interactive frontend: queue requests instead of running mocks.
  void setDeferToFrontend(bool defer);

  void presentAlert(
      HostUiAlert spec,
      std::function<void(HostUiAlertResult)> done);
  void presentShare(
      HostUiShare spec,
      std::function<void(HostUiShareResult)> done);
  void presentPermission(
      HostUiPermission spec,
      std::function<void(std::vector<std::pair<std::string, std::string>>)>
          done);
  void presentOpenUrl(
      HostUiOpenUrl spec,
      std::function<void(bool opened, std::string error)> done);
  void presentVibration(HostUiVibration spec);

  bool checkPermissionGranted(const std::string& permission) const;

  struct Pending {
    int id{0};
    HostUiKind kind{HostUiKind::Alert};
    HostUiAlert alert;
    HostUiShare share;
    HostUiPermission permission;
    HostUiOpenUrl openUrl;
    HostUiVibration vibration;
  };

  std::optional<Pending> peek() const;
  void completeAlert(HostUiAlertResult result);
  void completeShare(HostUiShareResult result);
  void completePermission(bool granted);
  void completeOpenUrl(bool opened, std::string error = {});
  void dismissVibration();
  void reset();

  void invalidate();

 private:
  struct Queued {
    Pending pending;
    std::function<void(HostUiAlertResult)> onAlert;
    std::function<void(HostUiShareResult)> onShare;
    std::function<void(std::vector<std::pair<std::string, std::string>>)>
        onPermission;
    std::function<void(bool, std::string)> onOpenUrl;
  };

  void enqueue(Queued item);
  std::optional<Queued> popLocked();
  HostUiAlertResult defaultAlert(const HostUiAlert&) const;
  HostUiShareResult defaultShare(const HostUiShare&) const;
  std::string defaultPermission(const std::string&) const;
  std::optional<std::string> defaultOpenUrl(const HostUiOpenUrl&) const;

  mutable std::mutex mutex_;
  bool deferToFrontend_{false};
  int nextId_{1};
  std::optional<Queued> current_;
  std::deque<Queued> waiting_;
  AlertMock mockAlert_;
  ShareMock mockShare_;
  PermissionMock mockPermission_;
  OpenUrlMock mockOpenUrl_;
  VibrationMock mockVibration_;
  std::unordered_map<std::string, std::string> permissionResults_;
};

HostUiController& hostUi();
