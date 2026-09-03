#include <react-native-simulator/Engine.h>
#include "EngineTestSupport.h"

#include "TestEngineThread.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

void writeBundle(const std::filesystem::path& path, const std::string& source) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot write temporary bundle");
  }
  output << source;
}

} // namespace

int main() {
  const auto suffix = std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count();
  const auto bundle = std::filesystem::temp_directory_path() /
      ("rnsim-reload-error-" + std::to_string(suffix) + ".js");
  struct RemoveBundle {
    std::filesystem::path path;
    ~RemoveBundle() {
      std::error_code error;
      std::filesystem::remove(path, error);
    }
  } cleanup{bundle};

  writeBundle(bundle, "function (\n");

  ReactNativeSimulator::EngineConfig config;
  config.mode = ReactNativeSimulator::SimulatorMode::Interactive;
  config.timeoutMs = 15000;
  auto engine = ReactNativeSimulator::test::makeEngine(
      std::move(config), {ReactNativeSimulator::test::fileBundle(bundle)});

  std::string runError;
  TestEngineThread runner([&] {
    const auto result = engine.run();
    if (result.exitCode != 0) {
      runError = result.error.empty() ? "engine failed" : result.error;
    }
  });

  const auto errorDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  bool errorVisible = false;
  while (std::chrono::steady_clock::now() < errorDeadline) {
    const auto state = engine.applicationLaunchState();
    if (state.initialBundlesLoaded && !state.lastError.empty()) {
      errorVisible = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!errorVisible) {
    engine.requestStop();
    runner.join();
    std::cerr << "interactive JS error was not exposed to the frontend\n";
    return 1;
  }

  writeBundle(bundle, "globalThis.RNS_RELOAD_ERROR_RECOVERED = true;\n");
  engine.requestReload();

  const auto recoveryDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  bool recovered = false;
  while (std::chrono::steady_clock::now() < recoveryDeadline) {
    const auto state = engine.applicationLaunchState();
    if (state.initialBundlesLoaded && state.lastError.empty()) {
      recovered = true;
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
  if (!recovered) {
    std::cerr << "interactive runtime did not recover after bundle repair\n";
    return 1;
  }
  return 0;
}
