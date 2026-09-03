#include "InspectorTransport.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <poll.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {

int connectToUnixSocket(const std::filesystem::path& socketPath) {
  if (socketPath.string().size() >= sizeof(sockaddr_un::sun_path)) {
    throw std::runtime_error("Inspector socket path is too long");
  }

  const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::seconds(5);
  while (true) {
    const int socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket < 0) {
      throw std::runtime_error(
          "Cannot create inspector socket: " +
          std::string(std::strerror(errno)));
    }
#ifdef SO_NOSIGPIPE
    int noSigPipe = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe,
                 sizeof(noSigPipe));
#endif
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(
        address.sun_path,
        socketPath.c_str(),
        sizeof(address.sun_path) - 1);
    if (::connect(
            socket,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) == 0) {
      return socket;
    }
    const auto error = errno;
    ::close(socket);
    if (std::chrono::steady_clock::now() >= deadline) {
      throw std::runtime_error(
          "Cannot connect to inspector socket " + socketPath.string() + ": " +
          std::string(std::strerror(error)));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void writeAll(int socket, const std::string& message) {
  std::size_t offset = 0;
  while (offset < message.size()) {
    const auto written = ::send(
        socket,
        message.data() + offset,
        message.size() - offset,
#ifdef MSG_NOSIGNAL
        MSG_NOSIGNAL
#else
        0
#endif
    );
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }
    if (written == 0) {
      return;
    }
    offset += static_cast<std::size_t>(written);
  }
}

} // namespace

class InspectorTransport::Impl {
 public:
  explicit Impl(int socketValue) : socket(socketValue) {
    writer = std::thread([this] { writeLoop(); });
  }

  ~Impl() {
    {
      std::lock_guard lock(mutex);
      stopped = true;
    }
    condition.notify_one();
    if (writer.joinable()) {
      writer.join();
    }
    ::shutdown(socket, SHUT_RDWR);
    ::close(socket);
  }

  void enqueue(std::string json) {
    json.push_back('\n');
    {
      std::lock_guard lock(mutex);
      if (stopped) {
        return;
      }
      // A snapshot is a latest-state stream. Keep the queue bounded if a
      // workload commits faster than the inspector can paint frames.
      if (queue.size() >= 32) {
        queue.pop_front();
      }
      queue.push_back(std::move(json));
    }
    condition.notify_one();
  }

  void waitForDisconnect() {
    pollfd descriptor{
        .fd = socket,
        .events = POLLIN | POLLERR | POLLHUP,
        .revents = 0,
    };
    while (true) {
      const auto ready = ::poll(&descriptor, 1, 100);
      if (ready < 0 && errno == EINTR) {
        continue;
      }
      if (ready <= 0) {
        continue;
      }
      if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return;
      }
      if ((descriptor.revents & POLLIN) != 0) {
        char buffer[256];
        const auto read = ::recv(socket, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (read <= 0) {
          return;
        }
      }
      descriptor.revents = 0;
    }
  }

 private:
  void writeLoop() {
    while (true) {
      std::string message;
      {
        std::unique_lock lock(mutex);
        condition.wait(lock, [this] { return stopped || !queue.empty(); });
        if (queue.empty() && stopped) {
          return;
        }
        message = std::move(queue.front());
        queue.pop_front();
      }
      writeAll(socket, message);
    }
  }

  int socket;
  std::thread writer;
  std::mutex mutex;
  std::condition_variable condition;
  std::deque<std::string> queue;
  bool stopped{false};
};

std::shared_ptr<InspectorTransport>
InspectorTransport::connect(
    const std::filesystem::path& socketPath) {
  return std::shared_ptr<InspectorTransport>(
      new InspectorTransport(connectToUnixSocket(socketPath)));
}

InspectorTransport::InspectorTransport(int socket)
    : impl_(std::make_unique<Impl>(socket)) {}

InspectorTransport::~InspectorTransport() = default;

void InspectorTransport::sendJson(std::string json) {
  impl_->enqueue(std::move(json));
}

void InspectorTransport::waitForDisconnect() {
  impl_->waitForDisconnect();
}
