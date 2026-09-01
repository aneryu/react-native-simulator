// Opens RN Tester Image example via initial URL, then waits for network images.
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

setTimeout(function () {
  globalThis.RN$SimulatorWorkloadResult = {
    appKey: 'RNTesterApp',
    opened: 'ImageExample',
  };
  RN$SimulatorWorkload.complete();
}, 4500);
