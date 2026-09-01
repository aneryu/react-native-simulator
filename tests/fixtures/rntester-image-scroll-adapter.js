// Opens RN Tester Image example, waits for network images, then scrolls down.
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
  RN$Simulator.dispatchActions([
    {type: 'scroll', x: 196, y: 420, deltaX: 0, deltaY: 2200},
  ])
    .then(function () {
      setTimeout(function () {
        globalThis.RN$SimulatorWorkloadResult = {
          appKey: 'RNTesterApp',
          opened: 'ImageExample',
          scrolled: true,
        };
        RN$SimulatorWorkload.complete();
      }, 1200);
    })
    .catch(function (error) {
      console.log(String(error));
      RN$SimulatorWorkload.complete();
    });
}, 3500);
