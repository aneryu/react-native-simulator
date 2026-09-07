RN$SimulatorWorkload.ready();
const uim = globalThis.nativeFabricUIManager;
const node = uim.createNode(
  200,
  'RNCSafeAreaProvider',
  21,
  {style: {width: 100, height: 40}},
  {tag: 200},
);
const children = uim.createChildSet();
uim.appendChildToSet(children, node);
uim.completeRoot(21, children);
setTimeout(function () {
  globalThis.RN$SimulatorWorkloadResult = {iterations: 1, checksum: 9};
  RN$SimulatorWorkload.complete();
}, 0);
