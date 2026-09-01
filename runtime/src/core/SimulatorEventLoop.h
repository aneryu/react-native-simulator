#pragma once

#include <ReactCommon/RuntimeExecutor.h>
#include <cxxreact/MessageQueueThread.h>
#include <jsi/jsi.h>
#include <react/runtime/PlatformTimerRegistry.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace facebook::react {
class TimerManager;
}

class SimulatorEventLoop final : public facebook::react::MessageQueueThread {
 public:
  SimulatorEventLoop();

  void runOnQueue(std::function<void()>&& work) override;
  void runOnQueueSync(std::function<void()>&& work) override;
  void quitSynchronous() override;

  std::unique_ptr<facebook::react::PlatformTimerRegistry>
  createTimerRegistry();

  void setRuntimeExecutor(facebook::react::RuntimeExecutor executor);
  uint32_t requestAnimationFrame(
      std::function<void(facebook::jsi::Runtime&, double)> callback);
  void cancelAnimationFrame(uint32_t handle);

  void startDisplayLink(std::function<void()> onFrame);
  void stopDisplayLink();

  size_t drainUntilIdle();
  size_t runFor(std::chrono::milliseconds duration);
  size_t runUntil(
      const std::function<bool()>& predicate,
      std::chrono::milliseconds timeout);
  bool hasPendingWork() const;

 private:
  struct Timer {
    std::chrono::steady_clock::time_point due;
    std::chrono::milliseconds interval;
    bool recurring{false};
  };

  struct State {
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::function<void()>> work;
    std::map<uint32_t, Timer> timers;
    std::function<void()> displayLink;
    std::chrono::steady_clock::time_point displayLinkDue{};
    facebook::react::RuntimeExecutor runtimeExecutor;
    std::map<
        uint32_t,
        std::function<void(facebook::jsi::Runtime&, double)>>
        animationFrames;
    uint32_t nextAnimationFrame{1};
    std::chrono::steady_clock::time_point animationFrameDue{};
    std::weak_ptr<facebook::react::TimerManager> timerManager;
    bool quit{false};
  };

  class TimerRegistry;

  bool runOneReady(bool includeDisplayLink);
  std::optional<std::chrono::steady_clock::time_point> nextTimerDue() const;

  std::shared_ptr<State> state_;
  std::thread::id ownerThread_;
};
