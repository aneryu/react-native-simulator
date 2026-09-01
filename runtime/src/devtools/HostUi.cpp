#include "HostUi.h"

#include "HostChrome.h"

#include <utility>

void HostUiController::setMockAlert(AlertMock mock) {
  std::lock_guard lock(mutex_);
  mockAlert_ = std::move(mock);
}

void HostUiController::setMockShare(ShareMock mock) {
  std::lock_guard lock(mutex_);
  mockShare_ = std::move(mock);
}

void HostUiController::setMockPermission(PermissionMock mock) {
  std::lock_guard lock(mutex_);
  mockPermission_ = std::move(mock);
}

void HostUiController::setMockOpenUrl(OpenUrlMock mock) {
  std::lock_guard lock(mutex_);
  mockOpenUrl_ = std::move(mock);
}

void HostUiController::setMockVibration(VibrationMock mock) {
  std::lock_guard lock(mutex_);
  mockVibration_ = std::move(mock);
}

void HostUiController::setDeferToFrontend(bool defer) {
  std::lock_guard lock(mutex_);
  deferToFrontend_ = defer;
}

HostUiAlertResult HostUiController::defaultAlert(
    const HostUiAlert&) const {
  return {.dismissed = false, .buttonKey = -1};
}

HostUiShareResult HostUiController::defaultShare(
    const HostUiShare&) const {
  return {.shared = true};
}

std::string HostUiController::defaultPermission(
    const std::string&) const {
  return "granted";
}

std::optional<std::string> HostUiController::defaultOpenUrl(
    const HostUiOpenUrl& spec) const {
  if (spec.settings) {
    return "IntentAndroid.openSettings has no Settings app in react-native-simulator";
  }
  return "IntentAndroid.openURL has no OS handler in react-native-simulator";
}

void HostUiController::enqueue(Queued item) {
  {
    std::lock_guard lock(mutex_);
    item.pending.id = nextId_++;
    if (current_) {
      waiting_.push_back(std::move(item));
    } else {
      current_ = std::move(item);
    }
  }
  invalidate();
}

std::optional<HostUiController::Queued>
HostUiController::popLocked() {
  auto item = std::move(current_);
  current_.reset();
  if (!waiting_.empty()) {
    current_ = std::move(waiting_.front());
    waiting_.pop_front();
  }
  return item;
}

void HostUiController::presentAlert(
    HostUiAlert spec,
    std::function<void(HostUiAlertResult)> done) {
  AlertMock mock;
  bool defer = false;
  {
    std::lock_guard lock(mutex_);
    defer = deferToFrontend_;
    mock = mockAlert_;
  }
  if (defer) {
    Queued item;
    item.pending.kind = HostUiKind::Alert;
    item.pending.alert = std::move(spec);
    item.onAlert = std::move(done);
    enqueue(std::move(item));
    return;
  }
  const auto result = mock ? mock(spec) : defaultAlert(spec);
  if (done) {
    done(result);
  }
}

void HostUiController::presentShare(
    HostUiShare spec,
    std::function<void(HostUiShareResult)> done) {
  ShareMock mock;
  bool defer = false;
  {
    std::lock_guard lock(mutex_);
    defer = deferToFrontend_;
    mock = mockShare_;
  }
  if (defer) {
    Queued item;
    item.pending.kind = HostUiKind::Share;
    item.pending.share = std::move(spec);
    item.onShare = std::move(done);
    enqueue(std::move(item));
    return;
  }
  const auto result = mock ? mock(spec) : defaultShare(spec);
  if (done) {
    done(result);
  }
}

void HostUiController::presentPermission(
    HostUiPermission spec,
    std::function<void(std::vector<std::pair<std::string, std::string>>)>
        done) {
  PermissionMock mock;
  bool defer = false;
  {
    std::lock_guard lock(mutex_);
    defer = deferToFrontend_;
    mock = mockPermission_;
  }
  if (defer) {
    Queued item;
    item.pending.kind = HostUiKind::Permission;
    item.pending.permission = std::move(spec);
    item.onPermission = std::move(done);
    enqueue(std::move(item));
    return;
  }
  std::vector<std::pair<std::string, std::string>> results;
  results.reserve(spec.permissions.size());
  {
    std::lock_guard lock(mutex_);
    for (const auto& permission : spec.permissions) {
      const auto status =
          mock ? mock(permission) : defaultPermission(permission);
      permissionResults_[permission] = status;
      results.emplace_back(permission, status);
    }
  }
  if (done) {
    done(std::move(results));
  }
}

void HostUiController::presentOpenUrl(
    HostUiOpenUrl spec,
    std::function<void(bool opened, std::string error)> done) {
  OpenUrlMock mock;
  bool defer = false;
  {
    std::lock_guard lock(mutex_);
    defer = deferToFrontend_;
    mock = mockOpenUrl_;
  }
  if (defer) {
    Queued item;
    item.pending.kind =
        spec.settings ? HostUiKind::OpenSettings : HostUiKind::OpenUrl;
    item.pending.openUrl = std::move(spec);
    item.onOpenUrl = std::move(done);
    enqueue(std::move(item));
    return;
  }
  const auto error = mock ? mock(spec) : defaultOpenUrl(spec);
  if (done) {
    if (error) {
      done(false, *error);
    } else {
      done(true, {});
    }
  }
}

void HostUiController::presentVibration(HostUiVibration spec) {
  VibrationMock mock;
  bool defer = false;
  {
    std::lock_guard lock(mutex_);
    defer = deferToFrontend_;
    mock = mockVibration_;
  }
  if (defer) {
    Queued item;
    item.pending.kind = HostUiKind::Vibration;
    item.pending.vibration = spec;
    enqueue(std::move(item));
    return;
  }
  if (mock) {
    mock(spec);
  }
}

bool HostUiController::checkPermissionGranted(
    const std::string& permission) const {
  std::lock_guard lock(mutex_);
  const auto found = permissionResults_.find(permission);
  if (found == permissionResults_.end()) {
    return true;
  }
  return found->second == "granted";
}

std::optional<HostUiController::Pending>
HostUiController::peek() const {
  std::lock_guard lock(mutex_);
  if (!current_) {
    return std::nullopt;
  }
  return current_->pending;
}

void HostUiController::completeAlert(HostUiAlertResult result) {
  std::function<void(HostUiAlertResult)> done;
  {
    std::lock_guard lock(mutex_);
    if (!current_ || current_->pending.kind != HostUiKind::Alert) {
      return;
    }
    done = std::move(current_->onAlert);
    popLocked();
  }
  if (done) {
    done(result);
  }
  invalidate();
}

void HostUiController::completeShare(HostUiShareResult result) {
  std::function<void(HostUiShareResult)> done;
  {
    std::lock_guard lock(mutex_);
    if (!current_ || current_->pending.kind != HostUiKind::Share) {
      return;
    }
    done = std::move(current_->onShare);
    popLocked();
  }
  if (done) {
    done(result);
  }
  invalidate();
}

void HostUiController::completePermission(bool granted) {
  std::function<void(std::vector<std::pair<std::string, std::string>>)> done;
  std::vector<std::pair<std::string, std::string>> results;
  {
    std::lock_guard lock(mutex_);
    if (!current_ || current_->pending.kind != HostUiKind::Permission) {
      return;
    }
    const auto status = granted ? "granted" : "denied";
    for (const auto& permission : current_->pending.permission.permissions) {
      permissionResults_[permission] = status;
      results.emplace_back(permission, status);
    }
    done = std::move(current_->onPermission);
    popLocked();
  }
  if (done) {
    done(std::move(results));
  }
  invalidate();
}

void HostUiController::completeOpenUrl(
    bool opened,
    std::string error) {
  std::function<void(bool, std::string)> done;
  {
    std::lock_guard lock(mutex_);
    if (!current_ ||
        (current_->pending.kind != HostUiKind::OpenUrl &&
         current_->pending.kind != HostUiKind::OpenSettings)) {
      return;
    }
    done = std::move(current_->onOpenUrl);
    popLocked();
  }
  if (done) {
    done(opened, std::move(error));
  }
  invalidate();
}

void HostUiController::dismissVibration() {
  std::lock_guard lock(mutex_);
  if (current_ && current_->pending.kind == HostUiKind::Vibration) {
    popLocked();
  }
}

void HostUiController::reset() {
  std::vector<Queued> items;
  {
    std::lock_guard lock(mutex_);
    if (current_) {
      items.push_back(std::move(*current_));
      current_.reset();
    }
    while (!waiting_.empty()) {
      items.push_back(std::move(waiting_.front()));
      waiting_.pop_front();
    }
    deferToFrontend_ = false;
  }
  for (auto& item : items) {
    if (item.onAlert) {
      item.onAlert({.dismissed = true, .buttonKey = 0});
    }
    if (item.onShare) {
      item.onShare({.shared = false});
    }
    if (item.onPermission) {
      std::vector<std::pair<std::string, std::string>> denied;
      for (const auto& permission : item.pending.permission.permissions) {
        denied.emplace_back(permission, "denied");
      }
      item.onPermission(std::move(denied));
    }
    if (item.onOpenUrl) {
      item.onOpenUrl(false, "host UI reset");
    }
  }
}

void HostUiController::invalidate() {
  if (onInvalidate) {
    onInvalidate();
  } else {
    hostChrome().invalidate();
  }
}

HostUiController& hostUi() {
  static HostUiController controller;
  return controller;
}
