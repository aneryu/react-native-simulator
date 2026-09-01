#pragma once

namespace facebook::react::jsinspector_modern::tracing {

// DevTools tracing is a platform service, not part of the headless renderer.
// Keep the production call sites intact while making that optional adapter a
// no-op until the headless metrics backend is installed.
template <typename... Args>
class PerformanceTracerSection {
 public:
  explicit PerformanceTracerSection(
      const char*,
      const char* = nullptr,
      const char* = nullptr,
      const char* = nullptr,
      Args...) noexcept {}
};

} // namespace facebook::react::jsinspector_modern::tracing
