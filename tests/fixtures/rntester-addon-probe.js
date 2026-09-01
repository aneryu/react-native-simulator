const get = globalThis.__turboModuleProxy;

for (const name of [
  'PlatformConstants',
  'DeviceInfo',
  'AppState',
  'IntentAndroid',
  'DialogManagerAndroid',
  'ShareModule',
  'PermissionsAndroid',
]) {
  if (!get(name)) {
    throw new Error(`RN Tester needs framework module ${name}`);
  }
}
if (get('NativeCxxModuleExampleCxx') === null) {
  throw new Error('RN Tester addon module NativeCxxModuleExampleCxx is missing');
}
if (get('ScreenshotManager') === null) {
  throw new Error('RN Tester addon module ScreenshotManager is missing');
}
if (get('ExampleApplicationModule')) {
  throw new Error('application module leaked into the RN Tester addon session');
}

const constants = get('NativeCxxModuleExampleCxx').getConstants();
if (constants.const1 !== true || constants.const2 !== 69 ||
    constants.const3 !== 'react-native') {
  throw new Error('NativeCxxModuleExampleCxx stub constants are wrong');
}

globalThis.RN$SimulatorWorkload.ready();
globalThis.RN$SimulatorWorkload.complete();
