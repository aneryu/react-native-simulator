// Opens RN Tester's ActivityIndicator example after the home list mounts.
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

function tap(x, y) {
  return RN$Simulator.dispatchActions([
    {type: 'pointerDown', x: x, y: y, buttons: 1},
    {type: 'pointerUp', x: x, y: y, buttons: 0},
  ]);
}

function finish(opened) {
  globalThis.RN$SimulatorWorkloadResult = {
    appKey: 'RNTesterApp',
    opened: opened,
  };
  RN$SimulatorWorkload.complete();
}

setTimeout(function () {
  tap(196, 180)
    .then(function () {
      setTimeout(function () {
        finish('ActivityIndicator');
      }, 900);
    })
    .catch(function (error) {
      console.log(String(error));
      finish('error');
    });
}, 700);
