// Opens RN Tester Text example via initial URL, then waits for layout.
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

const scrollY = Number(globalThis.RNSIM_TEXT_SCROLL || 0);

setTimeout(function () {
  const finish = function (scrolled) {
    globalThis.RN$SimulatorWorkloadResult = {
      appKey: 'RNTesterApp',
      opened: 'TextExample',
      scrolled: scrolled,
    };
    RN$SimulatorWorkload.complete();
  };
  if (!(scrollY > 0) || !globalThis.RN$Simulator || !RN$Simulator.dispatchActions) {
    finish(false);
    return;
  }
  RN$Simulator.dispatchActions([
    {type: 'scroll', x: 196, y: 420, deltaX: 0, deltaY: scrollY},
  ])
    .then(function () {
      setTimeout(function () {
        finish(true);
      }, 400);
    })
    .catch(function (error) {
      console.log(String(error));
      finish(false);
    });
}, 1800);
