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

namespace {
void* createNull() {
  return nullptr;
}
void destroyNull(void*) noexcept {}
} // namespace

extern "C" const FakeAddonDescriptor* react_native_simulator_addon_v4() noexcept {
  static FakeAddonDescriptor descriptor{
      static_cast<std::uint32_t>(sizeof(FakeAddonDescriptor)),
      4,
      "wrong-fingerprint",
      "wrong-fingerprint",
      "0.87.0",
      "260318099.0.1",
      &createNull,
      &destroyNull};
  return &descriptor;
}
