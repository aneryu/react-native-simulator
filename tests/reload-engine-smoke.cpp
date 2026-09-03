#include <react-native-simulator/Engine.h>
#include "EngineTestSupport.h"

#include "TestEngineThread.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: reload-engine-smoke bundle.js\n";
    return 1;
  }
  ReactNativeSimulator::EngineConfig config;
  config.mode = ReactNativeSimulator::SimulatorMode::Interactive;
  config.timeoutMs = 15000;
  config.autoRunApplication = false;
  auto engine = ReactNativeSimulator::test::makeEngine(
      std::move(config),
      {ReactNativeSimulator::test::fileBundle(argv[1])});

  std::string runError;
  TestEngineThread runner([&] {
    const auto result = engine.run();
    if (result.exitCode != 0) {
      runError = result.error.empty() ? "engine failed" : result.error;
    }
  });

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  std::uint64_t loadedGeneration = 0;
  bool requested = false;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto state = engine.applicationLaunchState();
    if (state.initialBundlesLoaded) {
      loadedGeneration = state.runtimeGeneration;
    }
    if (loadedGeneration == 1 && !requested) {
      engine.requestReload();
      requested = true;
    }
    if (loadedGeneration >= 2) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  engine.requestStop();
  runner.join();
  if (!runError.empty()) {
    std::cerr << runError << '\n';
    return 1;
  }
  if (loadedGeneration < 2) {
    std::cerr << "reload did not recreate the runtime (generation="
              << loadedGeneration << ")\n";
    return 1;
  }
  return 0;
}
