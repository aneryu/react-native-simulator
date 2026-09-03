#include <react-native-simulator/Engine.h>
#include <react-native-simulator/Interaction.h>

#include "EngineTestSupport.h"
#include "TestEngineThread.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

int main() {
  ReactNativeSimulator::SceneSnapshot hitScene;
  hitScene.rootTag = 1;
  ReactNativeSimulator::SceneNode root;
  root.tag = 1;
  root.layoutable = true;
  root.width = 100;
  root.height = 100;
  hitScene.nodes.push_back(root);
  ReactNativeSimulator::SceneNode scroll;
  scroll.tag = 2;
  scroll.parentTag = 1;
  scroll.layoutable = true;
  scroll.absoluteX = 10;
  scroll.absoluteY = 10;
  scroll.width = 40;
  scroll.height = 40;
  scroll.scrollable = true;
  scroll.scrollOffsetY = 20;
  hitScene.nodes.push_back(scroll);
  ReactNativeSimulator::SceneNode child;
  child.tag = 3;
  child.parentTag = 2;
  child.layoutable = true;
  child.absoluteX = 10;
  child.absoluteY = 35;
  child.width = 40;
  child.height = 30;
  hitScene.nodes.push_back(child);
  ReactNativeSimulator::SceneNode overlay;
  overlay.tag = 4;
  overlay.parentTag = 1;
  overlay.layoutable = true;
  overlay.absoluteX = 20;
  overlay.absoluteY = 20;
  overlay.width = 10;
  overlay.height = 10;
  hitScene.nodes.push_back(overlay);

  const auto overlayHit = ReactNativeSimulator::hitTestScene(hitScene, 25, 25);
  const auto scrolledChildHit =
      ReactNativeSimulator::hitTestScene(hitScene, 15, 20);
  const auto clippedChildHit =
      ReactNativeSimulator::hitTestScene(hitScene, 15, 55);
  if (!overlayHit || overlayHit->tag != 4 || overlayHit->localX != 5 ||
      overlayHit->localY != 5 || !scrolledChildHit ||
      scrolledChildHit->tag != 3 || scrolledChildHit->localY != 5 ||
      !clippedChildHit || clippedChildHit->tag != 1) {
    std::cerr << "retained scene hit testing failed\n";
    return 1;
  }
  hitScene.nodes.back().zIndex = -1;
  const auto zIndexHit = ReactNativeSimulator::hitTestScene(hitScene, 25, 25);
  if (!zIndexHit || zIndexHit->tag != 3) {
    std::cerr << "zIndex hit order did not match paint order\n";
    return 1;
  }
  hitScene.nodes[2].pointerEvents = "none";
  hitScene.nodes[1].pointerEvents = "box-none";
  const auto boxNoneHit = ReactNativeSimulator::hitTestScene(hitScene, 15, 20);
  if (!boxNoneHit || boxNoneHit->tag != 1) {
    std::cerr << "pointerEvents semantics were not respected\n";
    return 1;
  }
  hitScene.nodes[2].pointerEvents = "auto";
  hitScene.nodes[2].hitSlopTop = 5;
  const auto hitSlopHit = ReactNativeSimulator::hitTestScene(hitScene, 15, 12);
  if (!hitSlopHit || hitSlopHit->tag != 3) {
    std::cerr << "hitSlop did not expand the target\n";
    return 1;
  }
  hitScene.nodes[1].clipsContentToBounds = true;

  ReactNativeSimulator::SceneSnapshot transformHit;
  transformHit.rootTag = 1;
  ReactNativeSimulator::SceneNode transformRoot;
  transformRoot.tag = 1;
  transformRoot.layoutable = true;
  transformRoot.width = 80;
  transformRoot.height = 40;
  transformHit.nodes.push_back(transformRoot);
  ReactNativeSimulator::SceneNode transformParent;
  transformParent.tag = 2;
  transformParent.parentTag = 1;
  transformParent.layoutable = true;
  transformParent.width = 40;
  transformParent.height = 40;
  transformParent.hasTransform = true;
  transformParent.transformM[12] = 40;
  transformHit.nodes.push_back(transformParent);
  ReactNativeSimulator::SceneNode transformChild;
  transformChild.tag = 3;
  transformChild.parentTag = 2;
  transformChild.layoutable = true;
  transformChild.width = 20;
  transformChild.height = 20;
  transformHit.nodes.push_back(transformChild);
  const auto transformedChildHit =
      ReactNativeSimulator::hitTestScene(transformHit, 50, 10);
  const auto untransformedMiss =
      ReactNativeSimulator::hitTestScene(transformHit, 10, 10);
  if (!transformedChildHit || transformedChildHit->tag != 3 ||
      !untransformedMiss || untransformedMiss->tag != 1) {
    std::cerr << "parent transform was not applied to hit testing\n";
    return 1;
  }
  transformParent.hasTransform = true;
  transformParent.transformM[0] = 1;
  transformParent.transformM[5] = 0;
  transformParent.transformM[12] = 0;
  transformHit.nodes[1] = transformParent;
  const auto zeroScaleHit =
      ReactNativeSimulator::hitTestScene(transformHit, 10, 10);
  if (zeroScaleHit && zeroScaleHit->tag == 3) {
    std::cerr << "scaleY 0 parent still received nested hits\n";
    return 1;
  }

  ReactNativeSimulator::SceneSnapshot skewHit;
  skewHit.rootTag = 1;
  ReactNativeSimulator::SceneNode skewRoot;
  skewRoot.tag = 1;
  skewRoot.layoutable = true;
  skewRoot.width = 80;
  skewRoot.height = 80;
  skewHit.nodes.push_back(skewRoot);
  ReactNativeSimulator::SceneNode skewBox;
  skewBox.tag = 2;
  skewBox.parentTag = 1;
  skewBox.layoutable = true;
  skewBox.width = 80;
  skewBox.height = 80;
  skewBox.hasTransform = true;
  // skewY(45deg): y' = y + x, around the view center. (10,70) maps outside.
  skewBox.transformM[1] = 1;
  skewHit.nodes.push_back(skewBox);
  const auto skewInsideHit =
      ReactNativeSimulator::hitTestScene(skewHit, 10, 10);
  const auto skewOutsideHit =
      ReactNativeSimulator::hitTestScene(skewHit, 10, 70);
  if (!skewInsideHit || skewInsideHit->tag != 2) {
    std::cerr << "skewY parallelogram missed a point that should hit\n";
    return 1;
  }
  if (skewOutsideHit && skewOutsideHit->tag == 2) {
    std::cerr << "skewY parallelogram still hits the unsheared bottom-left\n";
    return 1;
  }
  skewBox.transformM[11] = -1;
  skewHit.nodes[1] = skewBox;
  const auto skewPerspInsideHit =
      ReactNativeSimulator::hitTestScene(skewHit, 10, 10);
  const auto skewPerspOutsideHit =
      ReactNativeSimulator::hitTestScene(skewHit, 10, 70);
  if (!skewPerspInsideHit || skewPerspInsideHit->tag != 2) {
    std::cerr << "skewY+perspective missed a point that should still hit\n";
    return 1;
  }
  if (skewPerspOutsideHit && skewPerspOutsideHit->tag == 2) {
    std::cerr << "skewY+perspective still hits the unsheared bottom-left\n";
    return 1;
  }
  ReactNativeSimulator::SceneSnapshot diamondHit;
  diamondHit.rootTag = 1;
  ReactNativeSimulator::SceneNode diamondRoot;
  diamondRoot.tag = 1;
  diamondRoot.layoutable = true;
  diamondRoot.width = 160;
  diamondRoot.height = 160;
  diamondHit.nodes.push_back(diamondRoot);
  ReactNativeSimulator::SceneNode diamondBox;
  diamondBox.tag = 2;
  diamondBox.parentTag = 1;
  diamondBox.layoutable = true;
  diamondBox.width = 80;
  diamondBox.height = 80;
  diamondBox.hasTransform = true;
  diamondBox.transformM[1] = 1;
  diamondBox.transformM[11] = -1;
  diamondHit.nodes.push_back(diamondBox);
  const auto diamondCornerHit =
      ReactNativeSimulator::hitTestScene(diamondHit, 90, 55);
  ReactNativeSimulator::SceneSnapshot skewWide;
  skewWide.rootTag = 1;
  ReactNativeSimulator::SceneNode skewWideRoot = diamondRoot;
  skewWide.nodes.push_back(skewWideRoot);
  ReactNativeSimulator::SceneNode skewWideBox = diamondBox;
  skewWideBox.transformM[11] = 0;
  skewWide.nodes.push_back(skewWideBox);
  const auto skewOnlyAtDiamond =
      ReactNativeSimulator::hitTestScene(skewWide, 90, 55);
  if (!diamondCornerHit || diamondCornerHit->tag != 2) {
    std::cerr << "skewY+perspective diamond missed its rotated corner\n";
    return 1;
  }
  if (skewOnlyAtDiamond && skewOnlyAtDiamond->tag == 2) {
    std::cerr << "pure skewY should not cover the perspective diamond corner\n";
    return 1;
  }

  ReactNativeSimulator::EngineConfig config;
  config.iterations = 5;
  config.timeoutMs = 1000;
  config.workload = "embedding-api";
  int sceneUpdates = 0;
  std::shared_ptr<const ReactNativeSimulator::SceneSnapshot> handedOffScene;
  config.onSceneUpdate = [&](auto scene) {
    ++sceneUpdates;
    handedOffScene = std::move(scene);
  };

  auto runtime = ReactNativeSimulator::test::makeEngine(
      std::move(config),
      {ReactNativeSimulator::test::memoryBundle(
           "RN$SimulatorWorkload.ready();\n"
           "globalThis.embeddingState = 40;\n",
           "memory://runtime-api-main.js"),
       ReactNativeSimulator::test::memoryBundle(
           "globalThis.embeddingState += 2;\n"
           "globalThis.RN$SimulatorWorkloadResult = {\n"
           "  iterations: 5, checksum: globalThis.embeddingState\n"
           "};\n"
           "RN$SimulatorWorkload.complete();\n",
           "memory://runtime-api-workload.js")});
  const auto result = runtime.run();
  if (result.exitCode != 0 || !result.error.empty() ||
      result.scene == nullptr || result.scene->viewportWidth != 300.0f ||
      result.scene->runtimeGeneration != 1 ||
      sceneUpdates < 1 || handedOffScene == nullptr ||
      handedOffScene->runtimeGeneration != 1 ||
      handedOffScene->revision != result.scene->revision ||
      result.scene->viewportHeight != 80.0f ||
      result.scene->pointScaleFactor != 1.0f ||
      result.metricsJson.find("\"schemaVersion\":3") == std::string::npos ||
      result.metricsJson.find("\"bundlesLoaded\":2") == std::string::npos ||
      result.metricsJson.find("\"workloadChecksum\":42") ==
          std::string::npos ||
      result.metricsJson.find("memory://runtime-api-main.js") ==
          std::string::npos ||
      result.metricsJson.find("memory://runtime-api-workload.js") ==
          std::string::npos) {
    std::cerr << result.error << '\n' << result.metricsJson;
    return 1;
  }

  bool rejectedLateCallback = false;
  try {
    runtime.setSceneUpdateCallback({});
  } catch (const std::logic_error&) {
    rejectedLateCallback = true;
  }
  if (!rejectedLateCallback) {
    std::cerr << "Engine accepted callback mutation after run\n";
    return 1;
  }

  ReactNativeSimulator::EngineConfig actionConfig;
  actionConfig.iterations = 5;
  actionConfig.timeoutMs = 1000;
  auto actionRuntime = ReactNativeSimulator::test::makeEngine(
      std::move(actionConfig),
      {ReactNativeSimulator::test::memoryBundle(
          "RN$SimulatorWorkload.ready();\n"
          "RN$Simulator.dispatchActions([{type:'pointerDown',x:999,y:999}])\n"
          ".then(function(){throw new Error('action unexpectedly resolved')})\n"
          ".catch(function(error){\n"
          " globalThis.RN$SimulatorWorkloadResult={iterations:5,checksum:\n"
          "   String(error.message).indexOf('no target')>=0?7:0};\n"
          " RN$SimulatorWorkload.complete();\n"
          "});\n",
          "memory://action-api.js")});
  const auto actionResult = actionRuntime.run();
  if (actionResult.exitCode != 0 ||
      actionResult.metricsJson.find("\"workloadChecksum\":7") ==
          std::string::npos) {
    std::cerr << actionResult.error << '\n' << actionResult.metricsJson;
    return 1;
  }

  ReactNativeSimulator::EngineConfig capabilityConfig;
  capabilityConfig.iterations = 1;
  capabilityConfig.timeoutMs = 1000;
  auto capabilityRuntime = ReactNativeSimulator::test::makeEngine(
      std::move(capabilityConfig),
      {ReactNativeSimulator::test::memoryBundle(
          "void globalThis.nativeModuleProxy.$$typeof;\n"
          "globalThis.__turboModuleProxy('DefinitelyMissingModule');\n"
          "RN$SimulatorWorkload.ready();\n"
          "globalThis.RN$SimulatorWorkloadResult={iterations:1,checksum:13};\n"
          "RN$SimulatorWorkload.complete();\n",
          "memory://capability-status.js")});
  const auto capabilityResult = capabilityRuntime.run();
  const auto capabilityStatus = capabilityRuntime.runtimeStatus();
  bool sawUnavailableUsage = false;
  bool sawMissingDiagnostic = false;
  bool sawMetadataProbe = false;
  for (const auto& capability : capabilityStatus.capabilityUsages) {
    sawUnavailableUsage = sawUnavailableUsage ||
        (capability.type == "module" &&
         capability.name == "DefinitelyMissingModule" &&
         capability.fidelity == "unavailable" &&
         capability.classification ==
             ReactNativeSimulator::RuntimeCapabilityClass::Unavailable);
    sawMetadataProbe = sawMetadataProbe || capability.name == "$$typeof";
  }
  for (const auto& diagnostic : capabilityStatus.diagnostics) {
    sawMissingDiagnostic = sawMissingDiagnostic ||
        (diagnostic.kind ==
             ReactNativeSimulator::RuntimeDiagnosticKind::MissingNativeModule &&
         diagnostic.name == "DefinitelyMissingModule");
    sawMetadataProbe = sawMetadataProbe || diagnostic.name == "$$typeof";
  }
  if (capabilityResult.exitCode != 0 || !sawUnavailableUsage ||
      !sawMissingDiagnostic || sawMetadataProbe) {
    std::cerr << "runtime status did not expose capability degradation\n";
    return 1;
  }

  ReactNativeSimulator::EngineConfig interactiveConfig;
  interactiveConfig.mode = ReactNativeSimulator::SimulatorMode::Interactive;
  interactiveConfig.autoRunApplication = true;
  interactiveConfig.initialPropsJson = "{\"n\":4}";
  interactiveConfig.timeoutMs = 1000;
  auto interactive = ReactNativeSimulator::test::makeEngine(
      std::move(interactiveConfig),
      {ReactNativeSimulator::test::memoryBundle(
          "globalThis.__turboModuleProxy('DeviceInfo');\n"
          "globalThis.RN$AppRegistry={\n"
          " getAppKeys:function(){return ['InteractiveApp'];},\n"
          " runApplication:function(key,parameters){\n"
          "  globalThis.RN$SimulatorWorkloadResult={iterations:1,checksum:\n"
          "   key==='InteractiveApp'&&parameters.rootTag===21&&\n"
          "   parameters.fabric===true&&parameters.initialProps&&\n"
          "   parameters.initialProps.n===4?9:0};\n"
          " }\n"
          "};\n",
          "memory://interactive.js")});
  ReactNativeSimulator::EngineResult interactiveResult;
  TestEngineThread interactiveThread(
      [&] { interactiveResult = interactive.run(); });
  bool interactiveRunning = false;
  bool acceptedActionWhileRunning = false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto status = interactive.runtimeStatus();
    if (status.phase == ReactNativeSimulator::RuntimePhase::Running) {
      bool sawDeviceInfo = false;
      for (const auto& capability : status.capabilityUsages) {
        if (capability.type == "module" &&
            capability.name == "DeviceInfo" &&
            capability.fidelity == "headless-adapter" &&
            capability.classification ==
                ReactNativeSimulator::RuntimeCapabilityClass::HostAdapted) {
          sawDeviceInfo = true;
          break;
        }
      }
      try {
        acceptedActionWhileRunning = interactive.enqueueAction({
            .type = ReactNativeSimulator::InteractionActionType::PointerMove,
            .x = -1,
            .y = -1,
        }) > 0;
      } catch (const std::logic_error&) {
      }
      interactiveRunning = status.runtimeGeneration == 1 &&
          status.hmr == ReactNativeSimulator::HMRStatus::Disabled &&
          status.diagnostics.empty() && sawDeviceInfo &&
          acceptedActionWhileRunning;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  interactive.requestStop();
  interactiveThread.join();
  if (!interactiveRunning ||
      interactive.runtimeStatus().phase !=
          ReactNativeSimulator::RuntimePhase::Stopped ||
      interactiveResult.exitCode != 0 || !interactiveResult.error.empty() ||
      interactiveResult.metricsJson.find("\"workloadChecksum\":9") ==
          std::string::npos) {
    std::cerr << "interactive stop lifecycle failed: "
              << interactiveResult.error << '\n';
    return 1;
  }

  ReactNativeSimulator::EngineConfig errorConfig;
  errorConfig.mode = ReactNativeSimulator::SimulatorMode::Interactive;
  errorConfig.timeoutMs = 1000;
  auto errorRuntime = ReactNativeSimulator::test::makeEngine(
      std::move(errorConfig),
      {ReactNativeSimulator::test::memoryBundle(
          "throw new Error('interactive status boom');\n",
          "memory://interactive-error.js")});
  ReactNativeSimulator::EngineResult errorResult;
  TestEngineThread errorThread([&] { errorResult = errorRuntime.run(); });
  bool observedStructuredError = false;
  bool rejectedApplicationWhilePaused = false;
  bool rejectedActionWhilePaused = false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto status = errorRuntime.runtimeStatus();
    if (status.phase ==
        ReactNativeSimulator::RuntimePhase::PausedAfterError) {
      observedStructuredError = !status.diagnostics.empty() &&
          status.diagnostics.front().kind ==
              ReactNativeSimulator::RuntimeDiagnosticKind::JavaScriptError &&
          status.diagnostics.front().message.find("interactive status boom") !=
              std::string::npos;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (observedStructuredError) {
    const auto before = errorRuntime.runtimeStatus();
    try {
      errorRuntime.runApplication("IgnoredApp", "{}");
    } catch (const std::logic_error&) {
      const auto after = errorRuntime.runtimeStatus();
      rejectedApplicationWhilePaused =
          after.phase == ReactNativeSimulator::RuntimePhase::PausedAfterError &&
          after.runtimeGeneration == before.runtimeGeneration &&
          after.diagnostics.size() == before.diagnostics.size() &&
          !after.diagnostics.empty() &&
          after.diagnostics.front().message ==
              before.diagnostics.front().message;
    }
    try {
      errorRuntime.enqueueAction({
          .type = ReactNativeSimulator::InteractionActionType::PointerDown,
          .x = 1,
          .y = 1,
      });
    } catch (const std::logic_error&) {
      const auto after = errorRuntime.runtimeStatus();
      rejectedActionWhilePaused =
          after.phase == ReactNativeSimulator::RuntimePhase::PausedAfterError &&
          after.runtimeGeneration == before.runtimeGeneration &&
          after.diagnostics.size() == before.diagnostics.size() &&
          !after.diagnostics.empty() &&
          after.diagnostics.front().message ==
              before.diagnostics.front().message;
    }
  }
  errorRuntime.requestStop();
  errorThread.join();
  if (!observedStructuredError || !rejectedApplicationWhilePaused ||
      !rejectedActionWhilePaused || errorResult.exitCode == 0) {
    std::cerr << "interactive runtime status did not preserve its JS error "
                 "while rejecting application or input work\n";
    return 1;
  }

  try {
    ReactNativeSimulator::Engine idle;
    idle.runApplication("X", "{}");
    std::cerr << "runApplication accepted before run()\n";
    return 1;
  } catch (const std::logic_error&) {
  }

  ReactNativeSimulator::EngineConfig selectConfig;
  selectConfig.mode = ReactNativeSimulator::SimulatorMode::Interactive;
  selectConfig.profile = "android-rn87";
  // Auto-run must stay non-fatal when more than one application is
  // registered or a configured key is stale. The interactive frontend can then
  // present the live keys and queue the caller's explicit choice.
  selectConfig.autoRunApplication = true;
  selectConfig.appKey = "StaleConfiguredApp";
  selectConfig.timeoutMs = 2000;
  std::atomic<std::uint64_t> latestSelectSceneGeneration{0};
  selectConfig.onSceneUpdate = [&](auto scene) {
    auto observed = latestSelectSceneGeneration.load();
    while (observed < scene->runtimeGeneration &&
           !latestSelectSceneGeneration.compare_exchange_weak(
               observed, scene->runtimeGeneration)) {
    }
  };
  auto selectable = ReactNativeSimulator::test::makeEngine(
      std::move(selectConfig),
      {ReactNativeSimulator::test::memoryBundle(
          "globalThis.RN$AppRegistry={\n"
          " getAppKeys:function(){return ['LogBox','AppA','AppB'];},\n"
          " runApplication:function(key,parameters){\n"
          "  const status=globalThis.__turboModuleProxy('StatusBarManager');\n"
          "  status.setHidden(false); status.setHidden(true);\n"
          "  globalThis.RN$SimulatorWorkloadResult={iterations:1,checksum:\n"
          "   key==='AppB'&&parameters.initialProps&&\n"
          "   parameters.initialProps.marker===7&&\n"
          "   parameters.rootTag===21&&parameters.fabric===true?11:0};\n"
          " }\n"
          "};\n",
          "memory://select-app.js")});
  ReactNativeSimulator::EngineResult selectResult;
  TestEngineThread selectThread([&] { selectResult = selectable.run(); });
  bool ranSelected = false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto launch = selectable.applicationLaunchState();
    if (launch.appRegistryReady) {
      if (launch.runningAppKey) {
        selectable.requestStop();
        selectThread.join();
        std::cerr << "ambiguous AppRegistry applications auto-ran "
                  << *launch.runningAppKey << '\n';
        return 1;
      }
      if (selectable.runtimeStatus().phase !=
          ReactNativeSimulator::RuntimePhase::ChoosingApplication) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        continue;
      }
      if (launch.lastError.find("StaleConfiguredApp") == std::string::npos ||
          launch.lastError.find("AppA") == std::string::npos ||
          launch.lastError.find("AppB") == std::string::npos) {
        selectable.requestStop();
        selectThread.join();
        std::cerr << "stale configured AppRegistry key was not actionable\n";
        return 1;
      }
      try {
        selectable.runApplication("AppB", "{\"marker\":7}");
        ranSelected = true;
        break;
      } catch (const std::logic_error&) {
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!ranSelected) {
    selectable.requestStop();
    selectThread.join();
    std::cerr << "failed to select AppRegistry application\n";
    return 1;
  }
  bool selectedRunning = false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const auto launch = selectable.applicationLaunchState();
    if (launch.runningAppKey && *launch.runningAppKey == "AppB" &&
        selectable.runtimeStatus().phase ==
            ReactNativeSimulator::RuntimePhase::Running) {
      selectedRunning = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!selectedRunning) {
    selectable.requestStop();
    selectThread.join();
    std::cerr << "selected AppRegistry application did not start\n";
    return 1;
  }
  selectable.requestReload();
  bool restoredSelectedAfterReload = false;
  for (int attempt = 0; attempt < 200; ++attempt) {
    const auto launch = selectable.applicationLaunchState();
    const auto status = selectable.runtimeStatus();
    if (launch.runtimeGeneration == 2 && status.runtimeGeneration == 2 &&
        launch.runningAppKey && *launch.runningAppKey == "AppB" &&
        status.phase == ReactNativeSimulator::RuntimePhase::Running &&
        latestSelectSceneGeneration.load() >= 2) {
      restoredSelectedAfterReload = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!restoredSelectedAfterReload) {
    const auto launch = selectable.applicationLaunchState();
    selectable.requestStop();
    selectThread.join();
    std::cerr << "reload did not restore selected AppB (generation="
              << launch.runtimeGeneration << ", running="
              << (launch.runningAppKey ? *launch.runningAppKey : "none")
              << ")\n";
    return 1;
  }
  try {
    selectable.runApplication("AppB", "[]");
    selectable.requestStop();
    selectThread.join();
    std::cerr << "runApplication accepted non-object JSON\n";
    return 1;
  } catch (const std::invalid_argument&) {
  }
  selectable.requestStop();
  selectThread.join();
  if (selectResult.exitCode != 0 || !selectResult.error.empty() ||
      selectResult.scene == nullptr ||
      selectResult.scene->runtimeGeneration != 2 ||
      latestSelectSceneGeneration.load() != 2 ||
      selectResult.metricsJson.find("\"workloadChecksum\":11") ==
          std::string::npos) {
    std::cerr << "selected AppRegistry application failed: "
              << selectResult.error << '\n'
              << selectResult.metricsJson;
    return 1;
  }
  return 0;
}
