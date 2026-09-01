#include <react-native-simulator/Engine.h>

#include <string>

int main() {
  ReactNativeSimulator::EngineConfig config;
  config.iterations = 2;
  ReactNativeSimulator::Engine runtime(config);
  runtime.loadBundle(
      std::string(
          "RN$SimulatorWorkload.ready();"
          "RN$SimulatorWorkload.complete();"),
      "memory://install-consumer.js");
  const auto result = runtime.run();
  return result.exitCode;
}
