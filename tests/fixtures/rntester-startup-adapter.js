// Headless companion for a caller-built RN Tester bundle. The tester app does
// not implement RN$SimulatorWorkload; this adapter starts RNTesterApp and then
// completes. It is not a sample application.
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

function complete() {
  globalThis.RN$SimulatorWorkloadResult = {
    appKey: 'RNTesterApp',
    started: true,
  };
  RN$SimulatorWorkload.complete();
}

if (typeof setImmediate === 'function') {
  setImmediate(complete);
} else {
  setTimeout(complete, 0);
}
