// Headless companion that starts RNTesterApp and waits for the example
// opened via RNSIM_INITIAL_URL / IntentAndroid.getInitialURL. The tester
// app does not implement RN$SimulatorWorkload; this is not a sample app.
const registry = globalThis.RN$AppRegistry;
if (!registry) {
  throw new Error('RN Tester bundle did not install RN$AppRegistry');
}
const appKeys = registry.getAppKeys();
if (!appKeys || appKeys.indexOf('RNTesterApp') < 0) {
  throw new Error('RNTesterApp was not registered');
}

RN$SimulatorWorkload.ready();
RN$SimulatorWorkload.registerRootTag(21);
registry.runApplication('RNTesterApp', {
  rootTag: 21,
  initialProps: {},
  fabric: true,
});

const settle = Number(globalThis.RNSIM_EXAMPLE_SETTLE_MS || 0) || 4000;

setTimeout(function () {
  globalThis.RN$SimulatorWorkloadResult = {
    appKey: 'RNTesterApp',
    opened: 'example',
    settleMs: settle,
  };
  RN$SimulatorWorkload.complete();
}, settle);
