#include <cstdint>

struct FakeAddonDescriptor {
  std::uint32_t descriptorSize;
  std::uint32_t abiVersion;
  const char* addonApiFingerprint;
  const char* name;
  const char* reactNativeVersion;
  const char* hermesVersion;
  void* (*create)();
  void (*destroy)(void*) noexcept;
};

extern "C" const FakeAddonDescriptor* react_native_simulator_addon_v4() noexcept {
  static FakeAddonDescriptor descriptor{
      4,
      4,
      "not-a-fingerprint",
      "short-descriptor",
      "0.87.0",
      "260318099.0.1",
      nullptr,
      nullptr};
  return &descriptor;
}
