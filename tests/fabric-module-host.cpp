#include <react-native-simulator/Engine.h>

#include "EngineTestSupport.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: fabric-module-host <rns-addon-fabric-probe> <workload.js>\n";
    return 1;
  }
  ReactNativeSimulator::EngineConfig config;
  config.iterations = 1;
  config.timeoutMs = 2000;
  config.profile = "android-rn87";
  config.viewportWidth = 300;
  config.viewportHeight = 80;
  ReactNativeSimulator::LaunchDraft draft(std::move(config));
  draft.addAddonPath(argv[1], ReactNativeSimulator::AddonRequestOrigin::Test);
  draft.addBundle(ReactNativeSimulator::test::fileBundle(argv[2]));
  auto candidates = ReactNativeSimulator::prepareExplicitAddons(draft);
  auto plan = ReactNativeSimulator::finalizeLaunchPlan(
      std::move(draft), std::move(candidates));
  ReactNativeSimulator::Engine engine;
  engine.applyLaunchPlan(std::move(plan));
  const auto result = engine.run();
  if (result.exitCode != 0 ||
      result.metricsJson.find("\"workloadChecksum\":21") == std::string::npos ||
      result.metricsJson.find("\"name\":\"fabric-probe\"") == std::string::npos ||
      result.metricsJson.find("\"source\":\"module\"") == std::string::npos ||
      result.metricsJson.find("topProbeEvent") == std::string::npos ||
      result.metricsJson.find("\"reactFabricCreates\"") == std::string::npos ||
      result.metricsJson.find("\"reactFabricInserts\"") == std::string::npos ||
      result.metricsJson.find("\"reactFabricUpdates\"") == std::string::npos ||
      result.metricsJson.find("\"reactFabricRemoves\"") == std::string::npos ||
      result.metricsJson.find("\"reactFabricDeletes\"") == std::string::npos) {
    std::cerr << result.error << '\n' << result.metricsJson << '\n';
    return 1;
  }
  const auto creates = result.metricsJson.find("\"reactFabricCreates\":0");
  const auto inserts = result.metricsJson.find("\"reactFabricInserts\":0");
  if (creates != std::string::npos || inserts != std::string::npos) {
    std::cerr << "Fabric MODULE did not record Create/Insert\n"
              << result.metricsJson << '\n';
    return 1;
  }
  return 0;
}
