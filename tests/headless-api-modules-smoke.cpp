#include <react-native-simulator/Engine.h>

#include <iostream>
#include <string>

int main() {
  ReactNativeSimulator::EngineConfig config;
  config.iterations = 5;
  config.timeoutMs = 4000;
  config.profile = "android-rn87";
  config.colorScheme = "dark";
  config.appState = "background";
  config.reduceMotion = true;
  config.viewportWidth = 320;
  config.viewportHeight = 640;
  config.pointScaleFactor = 2;

  ReactNativeSimulator::Engine runtime(std::move(config));
  runtime.loadBundle(
      R"JS(
RN$SimulatorWorkload.ready();
const modules = globalThis.nativeModuleProxy;
const blob = modules.BlobModule;
const reader = modules.FileReaderModule;
const appearance = modules.Appearance;
const appState = modules.AppState;
const a11y = modules.AccessibilityInfo;
const perf = modules.NativePerformanceCxx;
const frame = modules.FrameRateLogger;
const editing = modules.ImageEditingManager;
const store = modules.ImageStoreManager;

const id = 'blob-smoke';
blob.createFromParts([{type: 'string', data: 'hello'}], id);
const constants = blob.getConstants();
perf.reportMark('smoke', 12);
perf.reportMeasure('span', 10, 5);
frame.setContext('api-smoke');
frame.beginScroll();
frame.endScroll();

const result = {
  iterations: 5,
  checksum: 0,
  scheme: appearance.getColorScheme(),
  blobScheme: constants.BLOB_URI_SCHEME,
  mark: perf.getMarkTime('smoke'),
  entries: perf.getEntries().length,
  hasEditing: typeof editing.cropImage === 'function',
  hasStore: typeof store.getBase64ForTag === 'function',
};

appState.getCurrentAppState(function(state) {
  result.appState = state.app_state;
});
a11y.isReduceMotionEnabled(function(enabled) {
  result.reduceMotion = enabled;
});

const png =
  'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==';

Promise.all([
  reader.readAsText({blobId: id, offset: 0, size: 5}, 'UTF-8'),
  reader.readAsDataURL({blobId: id, offset: 0, size: 5, type: 'text/plain'}),
  new Promise(function(resolve, reject) {
    store.addImageFromBase64(
      png,
      function(tag) { resolve(tag); },
      function(error) { reject(new Error(String(error))); });
  }),
]).then(function(values) {
  result.text = values[0];
  result.dataUrl = values[1];
  const tag = values[2];
  result.stored = typeof tag === 'string' && tag.indexOf('file://') === 0;
  return new Promise(function(resolve, reject) {
    editing.cropImage(
      tag,
      {offset: {x: 0, y: 0}, size: {width: 1, height: 1}},
      function(uri) { resolve(uri); },
      function(error) { reject(new Error(String(error))); });
  });
}).then(function(cropped) {
  result.cropped = typeof cropped === 'string' && cropped.indexOf('file://') === 0;
  if (result.scheme !== 'dark' || result.appState !== 'background' ||
      result.reduceMotion !== true || result.text !== 'hello' ||
      String(result.dataUrl).indexOf('data:text/plain;base64,') !== 0 ||
      result.blobScheme !== 'blob' || result.mark !== 12 ||
      result.entries < 2 || !result.hasEditing || !result.hasStore ||
      !result.stored || !result.cropped) {
    throw new Error('api module smoke failed: ' + JSON.stringify(result));
  }
  result.checksum = 11;
  globalThis.RN$SimulatorWorkloadResult = result;
  RN$SimulatorWorkload.complete();
}).catch(function(error) {
  throw error;
});
)JS",
      "memory://headless-api-modules.js");

  const auto result = runtime.run();
  if (result.exitCode != 0 ||
      result.metricsJson.find("\"workloadChecksum\":11") == std::string::npos) {
    std::cerr << result.error << '\n' << result.metricsJson << '\n';
    return 1;
  }

  ReactNativeSimulator::EngineConfig iosConfig;
  iosConfig.iterations = 5;
  iosConfig.timeoutMs = 2000;
  iosConfig.profile = "ios-rn87";
  ReactNativeSimulator::Engine iosRuntime(std::move(iosConfig));
  iosRuntime.loadBundle(
      R"JS(
RN$SimulatorWorkload.ready();
const modules = globalThis.nativeModuleProxy;
const settings = modules.SettingsManager;
const alert = modules.AlertManager;
const action = modules.ActionSheetManager;
const status = modules.StatusBarManager;
settings.setValues({theme: 'dark', count: 2});
const constants = settings.getConstants().settings;
if (constants.theme !== 'dark' || constants.count !== 2 ||
    typeof alert.alertWithArgs !== 'function' ||
    typeof action.showActionSheetWithOptions !== 'function' ||
    status.getConstants().HEIGHT <= 0) {
  throw new Error('ios module smoke failed');
}
globalThis.RN$SimulatorWorkloadResult = {iterations: 5, checksum: 13};
RN$SimulatorWorkload.complete();
)JS",
      "memory://headless-ios-modules.js");
  const auto iosResult = iosRuntime.run();
  if (iosResult.exitCode != 0 ||
      iosResult.metricsJson.find("\"workloadChecksum\":13") ==
          std::string::npos) {
    std::cerr << iosResult.error << '\n' << iosResult.metricsJson << '\n';
    return 1;
  }

  ReactNativeSimulator::EngineConfig backConfig;
  backConfig.iterations = 5;
  backConfig.timeoutMs = 2000;
  backConfig.profile = "android-rn87";
  ReactNativeSimulator::Engine backRuntime(std::move(backConfig));
  backRuntime.loadBundle(
      R"JS(
let events = [];
globalThis.__rctDeviceEventEmitter = {
  emit: function(name, payload) {
    events.push({name: name, payload: payload});
  }
};
RN$SimulatorWorkload.ready();
const dem = globalThis.nativeModuleProxy.DeviceEventManager;
if (typeof dem.invokeDefaultBackPressHandler !== 'function') {
  throw new Error('DeviceEventManager missing invokeDefaultBackPressHandler');
}
RN$Simulator.dispatchActions([{type: 'hardwareBackPress'}]).then(function() {
  if (events.length !== 1 || events[0].name !== 'hardwareBackPress' ||
      typeof events[0].payload.timeStamp !== 'number') {
    throw new Error('hardwareBackPress failed: ' + JSON.stringify(events));
  }
  globalThis.RN$SimulatorWorkloadResult = {iterations: 5, checksum: 17};
  RN$SimulatorWorkload.complete();
}).catch(function(error) { throw error; });
)JS",
      "memory://headless-back-press.js");
  const auto backResult = backRuntime.run();
  if (backResult.exitCode != 0 ||
      backResult.metricsJson.find("\"workloadChecksum\":17") ==
          std::string::npos) {
    std::cerr << backResult.error << '\n' << backResult.metricsJson << '\n';
    return 1;
  }
  return 0;
}
