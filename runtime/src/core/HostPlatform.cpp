#include "HostPlatform.h"

#include <sys/resource.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cstdint>
#include <fstream>
#include <system_error>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach/mach.h>
#endif

std::filesystem::path hostExecutablePath() {
#if defined(__APPLE__)
  std::vector<char> buffer(1024);
  uint32_t size = static_cast<uint32_t>(buffer.size());
  while (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    buffer.resize(size);
  }
  return std::filesystem::weakly_canonical(buffer.data());
#else
  std::error_code error;
  auto path = std::filesystem::read_symlink("/proc/self/exe", error);
  if (error) {
    return {};
  }
  return std::filesystem::weakly_canonical(path, error);
#endif
}

std::string hostOsName() {
#if defined(__APPLE__)
  return "macos";
#elif defined(__linux__)
  return "linux";
#else
  return "unknown";
#endif
}

std::string hostArchitecture() {
  utsname system{};
  if (::uname(&system) != 0) {
    return "unknown";
  }
  return system.machine;
}

HostMemorySample sampleHostMemory() {
  HostMemorySample sample;
#if defined(__APPLE__)
  mach_task_basic_info_data_t taskInfo{};
  mach_msg_type_number_t taskInfoCount = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(
          mach_task_self(),
          MACH_TASK_BASIC_INFO,
          reinterpret_cast<task_info_t>(&taskInfo),
          &taskInfoCount) == KERN_SUCCESS) {
    sample.residentBytes = taskInfo.resident_size;
  }
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    sample.peakResidentBytes = static_cast<std::uint64_t>(usage.ru_maxrss);
  }
#else
  std::ifstream statm("/proc/self/statm");
  unsigned long pages = 0;
  unsigned long residentPages = 0;
  if (statm >> pages >> residentPages) {
    const auto pageSize = static_cast<std::uint64_t>(::sysconf(_SC_PAGESIZE));
    sample.residentBytes = residentPages * pageSize;
  }
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    sample.peakResidentBytes =
        static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
  }
#endif
  return sample;
}
