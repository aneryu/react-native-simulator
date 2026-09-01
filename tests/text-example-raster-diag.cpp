#include "SkiaMountedTreeRenderer.h"

#include <react-native-simulator/Engine.h>
#include <react-native-simulator/Interaction.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

int main() {
  setenv("RNSIM_INITIAL_URL", "rntester://example/TextExample", 1);
  setenv("RNSIM_RASTER_STATS", "1", 1);

  ReactNativeSimulator::EngineConfig config;
  config.mode = ReactNativeSimulator::SimulatorMode::Interactive;
  config.autoRunApplication = true;
  config.timeoutMs = 30000;
  config.profile = "android-rn87";
  config.appKey = "RNTesterApp";
  config.viewportWidth = 392.7273f;
  config.viewportHeight = 753.4545f;
  config.pointScaleFactor = 2.75f;
  config.fontDirectory = "build/android-fonts";
  std::mutex sceneMutex;
  std::shared_ptr<const ReactNativeSimulator::SceneSnapshot> lastScene;
  std::atomic<int> updates{0};
  const auto started = std::chrono::steady_clock::now();
  config.onSceneUpdate = [&](auto scene) {
    const auto ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - started)
                        .count();
    const int n = ++updates;
    std::fprintf(
        stderr,
        "scene #%d t=%.0fms rev=%lld nodes=%zu shadow=%zu\n",
        n,
        ms,
        static_cast<long long>(scene->revision),
        scene->nodes.size(),
        scene->shadowNodes.size());
    std::lock_guard lock(sceneMutex);
    lastScene = std::move(scene);
  };

  ReactNativeSimulator::Engine engine(std::move(config));
  engine.addAddon("build/runtime/rns-addon-rntester.dylib");
  engine.loadBundle("build/rntester/RNTesterApp.android.jsbundle");

  ReactNativeSimulator::EngineResult result;
  std::thread runtime([&] { result = engine.run(); });

  std::shared_ptr<const ReactNativeSimulator::SceneSnapshot> scene;
  for (int i = 0; i < 200; ++i) {
    {
      std::lock_guard lock(sceneMutex);
      scene = lastScene;
    }
    if (scene && scene->nodes.size() > 400) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  const auto loadMs = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started)
                          .count();
  if (scene == nullptr) {
    engine.requestStop();
    runtime.join();
    std::cerr << "no scene after " << loadMs << "ms error=" << result.error
              << '\n';
    return 1;
  }

  int paragraphs = 0;
  int prepared = 0;
  int scrollables = 0;
  for (const auto& node : scene->nodes) {
    if (node.componentName == "Paragraph") {
      ++paragraphs;
    }
    if (node.preparedText) {
      ++prepared;
    }
    if (node.scrollable) {
      ++scrollables;
    }
  }
  std::cerr << "load " << loadMs << "ms updates=" << updates
            << " nodes=" << scene->nodes.size()
            << " shadow=" << scene->shadowNodes.size()
            << " paragraphs=" << paragraphs << " prepared=" << prepared
            << " scrollables=" << scrollables
            << " revision=" << scene->revision << '\n';

  ReactNativeSimulator::SkiaMountedTreeRenderer renderer(
      "build/android-fonts");
  double minMs = 1e9;
  double maxMs = 0;
  double sumMs = 0;
  constexpr int kRepeats = 8;
  for (int i = 0; i < kRepeats; ++i) {
    const auto started = std::chrono::steady_clock::now();
    const auto frame = renderer.render(*scene);
    const auto ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - started)
                        .count();
    minMs = std::min(minMs, ms);
    maxMs = std::max(maxMs, ms);
    sumMs += ms;
    if (!frame) {
      std::cerr << "render failed: " << frame.error << '\n';
      engine.requestStop();
      runtime.join();
      return 1;
    }
    if (i == 0) {
      std::cerr << "first raster " << ms << "ms " << frame.width << "x"
                << frame.height << '\n';
    }
  }
  std::cerr << "raster x" << kRepeats << " min=" << minMs << " max=" << maxMs
            << " avg=" << (sumMs / kRepeats) << "ms\n";

  auto scrolled = std::make_shared<ReactNativeSimulator::SceneSnapshot>(*scene);
  for (auto& node : scrolled->nodes) {
    if (node.scrollable) {
      node.scrollOffsetY += 1200;
    }
  }
  const auto scrollStarted = std::chrono::steady_clock::now();
  const auto scrolledFrame = renderer.render(*scrolled);
  const auto scrollMs = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - scrollStarted)
                            .count();
  if (!scrolledFrame) {
    std::cerr << "scrolled render failed: " << scrolledFrame.error << '\n';
    return 1;
  }
  std::cerr << "scrolled raster " << scrollMs << "ms\n";

  const int updatesAtReady = updates.load();
  std::this_thread::sleep_for(std::chrono::seconds(6));
  const int updatesAfterIdle = updates.load();
  std::cerr << "idle 6s scene updates " << updatesAtReady << " -> "
            << updatesAfterIdle << " delta="
            << (updatesAfterIdle - updatesAtReady) << '\n';

  try {
    engine.enqueueAction({
        .type = ReactNativeSimulator::InteractionActionType::Scroll,
        .x = 196,
        .y = 400,
        .deltaX = 0,
        .deltaY = 800,
    });
  } catch (const std::exception& error) {
    std::cerr << "enqueue scroll: " << error.what() << '\n';
  }
  std::this_thread::sleep_for(std::chrono::seconds(2));
  const int updatesAfterScroll = updates.load();
  std::cerr << "after scroll updates " << updatesAfterIdle << " -> "
            << updatesAfterScroll << " delta="
            << (updatesAfterScroll - updatesAfterIdle) << '\n';

  engine.requestStop();
  runtime.join();
  std::cerr << "engine exit=" << result.exitCode << " error=" << result.error
            << '\n';
  const bool looping = (updatesAfterIdle - updatesAtReady) > 30;
  const bool slow = loadMs > 15000 || maxMs > 250 || scrollMs > 250;
  return looping || slow ? 2 : 0;
}
