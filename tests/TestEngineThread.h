#pragma once

#include <pthread.h>

#include <cerrno>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

// React Native platform JS threads use a multi-megabyte stack. Match that
// contract in embedding tests: macOS's generic pthread default is only 512 KiB,
// while an ASan-instrumented Hermes deliberately reserves a 256 KiB native
// stack guard and cannot initialize its internal bytecode on such a thread.
class TestEngineThread {
 public:
  template <typename Function>
  explicit TestEngineThread(Function&& function) {
    auto task = std::make_unique<std::function<void()>>(
        std::forward<Function>(function));
    pthread_attr_t attributes;
    check(pthread_attr_init(&attributes), "pthread_attr_init");
    const int stackResult = pthread_attr_setstacksize(&attributes, 8 << 20);
    if (stackResult != 0) {
      pthread_attr_destroy(&attributes);
      check(stackResult, "pthread_attr_setstacksize");
    }
    const int createResult =
        pthread_create(&thread_, &attributes, &TestEngineThread::start,
                       task.get());
    pthread_attr_destroy(&attributes);
    check(createResult, "pthread_create");
    task.release();
    joinable_ = true;
  }

  TestEngineThread(const TestEngineThread&) = delete;
  TestEngineThread& operator=(const TestEngineThread&) = delete;

  ~TestEngineThread() {
    if (joinable_) {
      std::terminate();
    }
  }

  void join() {
    if (!joinable_) {
      throw std::logic_error("TestEngineThread is not joinable");
    }
    check(pthread_join(thread_, nullptr), "pthread_join");
    joinable_ = false;
  }

 private:
  static void* start(void* opaque) {
    std::unique_ptr<std::function<void()>> task(
        static_cast<std::function<void()>*>(opaque));
    (*task)();
    return nullptr;
  }

  static void check(int result, const char* operation) {
    if (result != 0) {
      throw std::runtime_error(
          std::string(operation) + ": " + std::strerror(result));
    }
  }

  pthread_t thread_{};
  bool joinable_{false};
};
