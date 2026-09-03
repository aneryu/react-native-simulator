RN$SimulatorWorkload.ready();
const safe = globalThis.nativeModuleProxy.RNCSafeAreaContext;
const hasProvider = globalThis.__nativeComponentRegistry__hasComponent(
  'RNCSafeAreaProvider');
if (safe || hasProvider) {
  throw new Error('safe-area surfaces should be unavailable');
}
globalThis.RN$SimulatorWorkloadResult = {iterations: 1, checksum: 8};
RN$SimulatorWorkload.complete();
