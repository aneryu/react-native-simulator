RN$SimulatorWorkload.ready();
const platform = globalThis.nativeModuleProxy.PlatformConstants;
// RN TurboModuleBinding returns an empty jsRepresentation whose prototype is
// the HostObject. Object.keys therefore stays empty; property lookup still
// walks the prototype. Assert the overlay methods exist and forward.
if (typeof platform.getConstants !== 'function' ||
    typeof platform.getAndroidID !== 'function') {
  throw new Error(
    'compat-rn73 overlay must expose getConstants and getAndroidID');
}
const constants = platform.getConstants();
const version = constants.reactNativeVersion;
if (!version || version.major !== 0 || version.minor !== 73 || version.patch !== 10) {
  throw new Error('JS-visible RN version must be 0.73.10: ' + JSON.stringify(version));
}
if (constants.Version !== 35 || constants.Brand !== 'headless' ||
    constants.Manufacturer !== 'react-native-simulator' ||
    constants.uiMode !== 'normal' || constants.Release !== '15' ||
    constants.Serial !== 'headless' || constants.isTesting !== false) {
  throw new Error(
    'compat-rn73 must preserve non-version PlatformConstants: ' +
    JSON.stringify(constants));
}
const androidId = platform.getAndroidID();
if (androidId !== 'react-native-simulator') {
  throw new Error('getAndroidID must still be the RN 0.87 host value');
}
globalThis.RN$SimulatorWorkloadResult = {iterations: 1, checksum: 73};
RN$SimulatorWorkload.complete();
