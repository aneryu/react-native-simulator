#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

std::filesystem::path hostExecutablePath();
std::string hostOsName();
std::string hostArchitecture();

struct HostMemorySample {
  std::uint64_t residentBytes{0};
  std::uint64_t peakResidentBytes{0};
};

HostMemorySample sampleHostMemory();
