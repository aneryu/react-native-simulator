RN$SimulatorWorkload.ready();

const config = RN$SimulatorWorkloadConfig;
const sample = globalThis.nativeModuleProxy.HeadlessSampleModule;
const result = {
  iterations: config.iterations,
  renderIterations: 0,
  cpuIterations: config.iterations,
  renderMs: 0,
  cpuMs: 0,
  checksum: 0,
  timeoutFired: false,
  canceledTimerFired: false,
  intervalCount: 0,
  microtaskFired: false,
  nativeModuleSum: sample.add(19, 23),
  nativeModuleEcho: sample.echo('react-native-simulator'),
};
globalThis.RN$SimulatorWorkloadResult = result;

const cpuStart = Date.now();
for (let iteration = 0; iteration < config.iterations; iteration += 1) {
  for (let value = 0; value < 1000; value += 1) {
    result.checksum = (result.checksum + value + iteration) >>> 0;
  }
}
result.cpuMs = Date.now() - cpuStart;

Promise.resolve().then(() => {
  result.microtaskFired = true;
});
const canceled = setTimeout(() => {
  result.canceledTimerFired = true;
}, 0);
clearTimeout(canceled);
const interval = setInterval(() => {
  result.intervalCount += 1;
  clearInterval(interval);
}, 0);
setTimeout(() => {
  result.timeoutFired = true;
}, 0);
setTimeout(() => {
  if (!result.microtaskFired || result.intervalCount !== 1 ||
      !result.timeoutFired || result.canceledTimerFired ||
      result.nativeModuleSum !== 42) {
    throw new Error('runtime smoke contract failed');
  }
  RN$SimulatorWorkload.complete();
}, 2);
