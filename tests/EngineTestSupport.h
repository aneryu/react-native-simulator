#pragma once

#include <react-native-simulator/Engine.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ReactNativeSimulator {
namespace test {

inline InitialBundleSpec memoryBundle(std::string body, std::string sourceUrl) {
  InitialBundleSpec spec;
  spec.sourceUrl = std::move(sourceUrl);
  spec.body = std::move(body);
  return spec;
}

inline InitialBundleSpec fileBundle(const std::filesystem::path& path) {
  InitialBundleSpec spec;
  auto resolved = std::filesystem::weakly_canonical(path);
  spec.sourceUrl = std::string("file://") + resolved.generic_string();
  spec.path = resolved;
  return spec;
}

inline Engine makeEngine(
    EngineConfig config,
    std::vector<InitialBundleSpec> bundles,
    std::function<void(LaunchDraft&)> setup = {}) {
  LaunchDraft draft(std::move(config));
  if (setup) {
    setup(draft);
  }
  for (auto& bundle : bundles) {
    draft.addBundle(std::move(bundle));
  }
  auto candidates = prepareExplicitAddons(draft);
  auto plan = finalizeLaunchPlan(std::move(draft), std::move(candidates));
  Engine engine;
  engine.applyLaunchPlan(std::move(plan));
  return engine;
}

} // namespace test
} // namespace ReactNativeSimulator
