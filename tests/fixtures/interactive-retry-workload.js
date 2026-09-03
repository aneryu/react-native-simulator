if (typeof globalThis.RN$registerCallableModule === 'function') {
  globalThis.RN$registerCallableModule('HMRClient', function () {
    return {
      setup() {},
      enable() {},
      disable() {},
      registerBundle() {},
    };
  });
}

RN$SimulatorWorkload.ready();

const uim = globalThis.nativeFabricUIManager;
if (!uim || typeof uim.createNode !== 'function') {
  throw new Error('nativeFabricUIManager.createNode is required');
}

const SURFACE = 21;
const node = uim.createNode(
  100,
  'View',
  SURFACE,
  {width: 120, height: 40, alignSelf: 'flex-start'},
  {tag: 100},
);
const set = uim.createChildSet();
uim.appendChildToSet(set, node);
uim.completeRoot(SURFACE, set);

setTimeout(function () {
  globalThis.RN$SimulatorWorkloadResult = {iterations: 1, checksum: 1};
  RN$SimulatorWorkload.complete();
}, 0);
