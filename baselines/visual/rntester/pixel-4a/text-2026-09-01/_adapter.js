const registry = globalThis.RN$AppRegistry;
if (!registry) {
  throw new Error('RN Tester bundle did not install RN$AppRegistry');
}
const appKeys = registry.getAppKeys();
if (!appKeys || appKeys.indexOf('RNTesterApp') < 0) {
  throw new Error('RNTesterApp was not registered');
}
const ACTIONS = [];
const SETTLE_MS = 2200;
RN$SimulatorWorkload.ready();
RN$SimulatorWorkload.registerRootTag(21);
registry.runApplication('RNTesterApp', {
  rootTag: 21,
  initialProps: {},
  fabric: true,
});
function finish(extra) {
  globalThis.RN$SimulatorWorkloadResult = Object.assign({
    appKey: 'RNTesterApp',
    opened: 'example',
    actionCount: ACTIONS.length,
  }, extra || {});
  RN$SimulatorWorkload.complete();
}
function isPointer(step) {
  return step && (
    step.type === 'pointerDown' ||
    step.type === 'pointerMove' ||
    step.type === 'pointerUp' ||
    step.type === 'pointerCancel'
  );
}
function run(index) {
  if (index >= ACTIONS.length) {
    setTimeout(function () { finish({ok: true}); }, 500);
    return;
  }
  const step = ACTIONS[index];
  if (step.type === 'wait') {
    setTimeout(function () { run(index + 1); }, step.ms || 300);
    return;
  }
  if (!globalThis.RN$Simulator || !RN$Simulator.dispatchActions) {
    finish({ok: false, reason: 'no-dispatch'});
    return;
  }
  const batch = [];
  let next = index;
  if (isPointer(step)) {
    while (next < ACTIONS.length && isPointer(ACTIONS[next])) {
      batch.push(ACTIONS[next]);
      next += 1;
    }
  } else {
    batch.push(step);
    next = index + 1;
  }
  RN$Simulator.dispatchActions(batch).then(function () {
    run(next);
  }).catch(function (error) {
    console.log(String(error));
    finish({ok: false, reason: String(error)});
  });
}
setTimeout(function () { run(0); }, SETTLE_MS);
