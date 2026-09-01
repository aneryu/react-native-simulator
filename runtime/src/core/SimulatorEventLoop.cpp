#include "SimulatorEventLoop.h"

#include <react/runtime/TimerManager.h>

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

class SimulatorEventLoop::TimerRegistry final
    : public facebook::react::PlatformTimerRegistry {
 public:
  explicit TimerRegistry(std::shared_ptr<State> state)
      : state_(std::move(state)) {}

  void createTimer(uint32_t timerID, double delayMS) override {
    schedule(timerID, delayMS, false);
  }

  void deleteTimer(uint32_t timerID) override {
    std::lock_guard lock(state_->mutex);
    state_->timers.erase(timerID);
  }

  void createRecurringTimer(uint32_t timerID, double delayMS) override {
    schedule(timerID, delayMS, true);
  }

  void setTimerManager(
      std::weak_ptr<facebook::react::TimerManager> timerManager) override {
    std::lock_guard lock(state_->mutex);
    state_->timerManager = std::move(timerManager);
  }

  void quit() override {
    std::lock_guard lock(state_->mutex);
    state_->timers.clear();
    state_->timerManager.reset();
    state_->cv.notify_all();
  }

 private:
  void schedule(uint32_t timerID, double delayMS, bool recurring) {
    const auto delay = std::chrono::milliseconds(
        std::max<int64_t>(0, static_cast<int64_t>(delayMS)));
    std::lock_guard lock(state_->mutex);
    state_->timers[timerID] = {
        std::chrono::steady_clock::now() + delay,
        recurring ? delay : std::chrono::milliseconds::zero(),
        recurring};
    state_->cv.notify_one();
  }

  std::shared_ptr<State> state_;
};

SimulatorEventLoop::SimulatorEventLoop()
    : state_(std::make_shared<State>()),
      ownerThread_(std::this_thread::get_id()) {}

void SimulatorEventLoop::runOnQueue(std::function<void()>&& work) {
  std::lock_guard lock(state_->mutex);
  if (!state_->quit) {
    state_->work.push_back(std::move(work));
    state_->cv.notify_one();
  }
}

void SimulatorEventLoop::runOnQueueSync(std::function<void()>&& work) {
  if (std::this_thread::get_id() != ownerThread_) {
    throw std::runtime_error(
        "react-native-simulator event loop is bound to its creating thread");
  }
  if (!state_->quit) {
    work();
  }
}

void SimulatorEventLoop::quitSynchronous() {
  std::lock_guard lock(state_->mutex);
  state_->quit = true;
  state_->work.clear();
  state_->timers.clear();
  state_->animationFrames.clear();
  state_->displayLink = nullptr;
  state_->cv.notify_all();
}

std::unique_ptr<facebook::react::PlatformTimerRegistry>
SimulatorEventLoop::createTimerRegistry() {
  return std::make_unique<TimerRegistry>(state_);
}

void SimulatorEventLoop::setRuntimeExecutor(
    facebook::react::RuntimeExecutor executor) {
  std::lock_guard lock(state_->mutex);
  state_->runtimeExecutor = std::move(executor);
}

uint32_t SimulatorEventLoop::requestAnimationFrame(
    std::function<void(facebook::jsi::Runtime&, double)> callback) {
  std::lock_guard lock(state_->mutex);
  const auto handle = state_->nextAnimationFrame++;
  const bool wasEmpty = state_->animationFrames.empty();
  state_->animationFrames.emplace(handle, std::move(callback));
  if (wasEmpty) {
    state_->animationFrameDue = std::chrono::steady_clock::now();
  }
  state_->cv.notify_one();
  return handle;
}

void SimulatorEventLoop::cancelAnimationFrame(uint32_t handle) {
  std::lock_guard lock(state_->mutex);
  state_->animationFrames.erase(handle);
}

void SimulatorEventLoop::startDisplayLink(std::function<void()> onFrame) {
  std::lock_guard lock(state_->mutex);
  state_->displayLink = std::move(onFrame);
  state_->displayLinkDue = std::chrono::steady_clock::now();
  state_->cv.notify_one();
}

void SimulatorEventLoop::stopDisplayLink() {
  std::lock_guard lock(state_->mutex);
  state_->displayLink = nullptr;
}

bool SimulatorEventLoop::runOneReady(bool includeDisplayLink) {
  std::function<void()> work;
  std::vector<uint32_t> dueTimers;
  std::shared_ptr<facebook::react::TimerManager> timerManager;
  std::function<void()> displayLink;
  std::vector<std::function<void(facebook::jsi::Runtime&, double)>>
      animationFrames;
  facebook::react::RuntimeExecutor runtimeExecutor;
  double animationTimestampMs = 0;
  {
    std::lock_guard lock(state_->mutex);
    if (state_->quit) {
      return false;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool nativeFrameDue = includeDisplayLink && state_->displayLink &&
        state_->displayLinkDue <= now;
    const bool jsFrameDue = includeDisplayLink &&
        !state_->animationFrames.empty() &&
        state_->animationFrameDue <= now;
    // One vsync tick drives native displayLink and JS rAF together so
    // Animated.parallel siblings share a timestamp instead of racing as
    // independent setTimeout(0) callbacks.
    if (nativeFrameDue || jsFrameDue) {
      if (nativeFrameDue) {
        state_->displayLinkDue = now + std::chrono::milliseconds(16);
        displayLink = state_->displayLink;
      }
      if (jsFrameDue) {
        state_->animationFrameDue = now + std::chrono::milliseconds(16);
        animationTimestampMs = std::chrono::duration<double, std::milli>(
                                   now.time_since_epoch())
                                   .count();
        runtimeExecutor = state_->runtimeExecutor;
        animationFrames.reserve(state_->animationFrames.size());
        for (auto& [_, callback] : state_->animationFrames) {
          animationFrames.push_back(std::move(callback));
        }
        state_->animationFrames.clear();
      }
    } else if (!state_->work.empty()) {
      work = std::move(state_->work.front());
      state_->work.pop_front();
    } else {
      for (const auto& [handle, timer] : state_->timers) {
        if (timer.due <= now) {
          dueTimers.push_back(handle);
        }
      }
      if (!dueTimers.empty()) {
        timerManager = state_->timerManager.lock();
        for (const auto handle : dueTimers) {
          const auto found = state_->timers.find(handle);
          if (found == state_->timers.end()) {
            continue;
          }
          if (!found->second.recurring) {
            state_->timers.erase(found);
          } else {
            found->second.due = now + std::max(
                found->second.interval, std::chrono::milliseconds(1));
          }
        }
      } else {
        return false;
      }
    }
  }
  if (work) {
    work();
    return true;
  }
  if (!dueTimers.empty()) {
    for (const auto handle : dueTimers) {
      if (timerManager) {
        timerManager->callTimer(static_cast<int>(handle));
      }
    }
    return true;
  }
  const bool ranFrame = displayLink || !animationFrames.empty();
  if (displayLink) {
    displayLink();
  }
  if (!animationFrames.empty() && runtimeExecutor) {
    runtimeExecutor([animationFrames = std::move(animationFrames),
                     animationTimestampMs](facebook::jsi::Runtime& runtime) {
      for (const auto& callback : animationFrames) {
        callback(runtime, animationTimestampMs);
      }
    });
  }
  return ranFrame;
}

size_t SimulatorEventLoop::drainUntilIdle() {
  size_t count = 0;
  while (true) {
    std::function<void()> work;
    {
      std::lock_guard lock(state_->mutex);
      if (state_->quit || state_->work.empty()) {
        break;
      }
      work = std::move(state_->work.front());
      state_->work.pop_front();
    }
    work();
    ++count;
  }
  return count;
}

size_t SimulatorEventLoop::runFor(std::chrono::milliseconds duration) {
  return runUntil([] { return false; }, duration);
}

size_t SimulatorEventLoop::runUntil(
    const std::function<bool()>& predicate,
    std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  size_t count = 0;
  while (true) {
    if (predicate()) {
      break;
    }
    {
      std::lock_guard lock(state_->mutex);
      if (state_->quit) {
        break;
      }
    }
    if (runOneReady(true)) {
      ++count;
      if (std::chrono::steady_clock::now() >= deadline) {
        break;
      }
      continue;
    }
    std::unique_lock lock(state_->mutex);
    if (state_->quit || !state_->work.empty()) {
      continue;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      break;
    }
    const auto nextDue = nextTimerDue();
    const auto wakeAt = nextDue ? std::min(*nextDue, deadline) : deadline;
    state_->cv.wait_until(lock, wakeAt, [&] {
      return state_->quit || !state_->work.empty();
    });
  }
  return count;
}

bool SimulatorEventLoop::hasPendingWork() const {
  std::lock_guard lock(state_->mutex);
  return !state_->work.empty() || !state_->timers.empty() ||
      !state_->animationFrames.empty();
}

std::optional<std::chrono::steady_clock::time_point>
SimulatorEventLoop::nextTimerDue() const {
  std::optional<std::chrono::steady_clock::time_point> next;
  for (const auto& [_, timer] : state_->timers) {
    if (!next || timer.due < *next) {
      next = timer.due;
    }
  }
  if (state_->displayLink && (!next || state_->displayLinkDue < *next)) {
    next = state_->displayLinkDue;
  }
  if (!state_->animationFrames.empty() &&
      (!next || state_->animationFrameDue < *next)) {
    next = state_->animationFrameDue;
  }
  return next;
}
