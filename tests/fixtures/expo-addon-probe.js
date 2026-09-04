const get = globalThis.__turboModuleProxy;

if (typeof globalThis.expo !== 'object' || globalThis.expo === null) {
  throw new Error('global.expo was not installed');
}
if (typeof globalThis.expo.EventEmitter !== 'function') {
  throw new Error('global.expo.EventEmitter is missing');
}
if (typeof globalThis.expo.NativeModule !== 'function') {
  throw new Error('global.expo.NativeModule is missing');
}
if (!globalThis.expo.modules || !globalThis.expo.modules.ExpoAsset) {
  throw new Error('global.expo.modules.ExpoAsset is missing');
}
const proxy = globalThis.nativeModuleProxy;
if (!proxy || proxy.ExpoAsset !== globalThis.expo.modules.ExpoAsset) {
  throw new Error(
    'expo.modules.ExpoAsset must be nativeModuleProxy.ExpoAsset (=== jsRepresentation)');
}

for (const name of [
  'ExpoAsset',
  'ExpoKeepAwake',
  'ExpoSplashScreen',
  'ExpoFontLoader',
  'ExpoSystemUI',
  'ExponentConstants',
  'ExpoModulesCore',
  'ExpoFetchModule',
  'ExpoLinking',
]) {
  if (!get(name)) {
    throw new Error(`Expo host-adapter module ${name} is missing`);
  }
}
if (!globalThis.expo.modules.ExpoFetchModule ||
    typeof globalThis.expo.modules.ExpoFetchModule.NativeRequest !== 'function' ||
    typeof globalThis.expo.modules.ExpoFetchModule.NativeResponse !== 'function') {
  throw new Error('global.expo.modules.ExpoFetchModule constructors are missing');
}
if (get('ExpoGo')) {
  throw new Error('ExpoGo must stay unavailable so isRunningInExpoGo is false');
}

const constants = get('ExponentConstants').getConstants();
if (constants.executionEnvironment !== 'bare' || constants.appOwnership != null) {
  throw new Error('ExponentConstants must report a bare host-adapted environment');
}
if (!constants.manifest || constants.manifest.scheme !== 'rnsim') {
  throw new Error('ExponentConstants.manifest must include scheme rnsim');
}
const subscription = get('ExpoLinking').addListener('onURLReceived', function () {});
if (!subscription || typeof subscription.remove !== 'function') {
  throw new Error('ExpoLinking.addListener must return a subscription');
}
subscription.remove();

globalThis.RN$SimulatorWorkload.ready();
globalThis.RN$SimulatorWorkload.complete();
