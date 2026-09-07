import {spawn} from 'node:child_process';

export const metricNames = [
  'renderMs',
  'cpuMs',
  'commitMs',
  'layoutMs',
  'diffMs',
  'initializationMs',
  'bundleAndDrainMs',
  'heapAllocatedBytes',
  'heapSizeBytes',
  'heapExternalBytes',
  'heapTotalAllocatedBytes',
  'gcCollections',
  'gcTotalMs',
  'gcMaxPauseMs',
  'gcCpuMs',
  'residentBytes',
  'peakResidentBytes',
  'processUserCpuMs',
  'processSystemCpuMs',
  'workloadHeapAllocatedDeltaBytes',
  'workloadHeapSizeDeltaBytes',
  'workloadHeapExternalDeltaBytes',
  'workloadHeapTotalAllocatedBytes',
  'workloadGcCollections',
  'workloadGcTotalMs',
  'workloadGcCpuMs',
  'workloadResidentGrowthBytes',
  'workloadPeakResidentGrowthBytes',
  'workloadUserCpuMs',
  'workloadSystemCpuMs',
];

function percentile(sorted, fraction) {
  if (sorted.length === 1) return sorted[0];
  const position = (sorted.length - 1) * fraction;
  const lower = Math.floor(position);
  const upper = Math.ceil(position);
  const weight = position - lower;
  return sorted[lower] * (1 - weight) + sorted[upper] * weight;
}

export function summarize(values) {
  const sorted = [...values].sort((left, right) => left - right);
  const mean = values.reduce((sum, value) => sum + value, 0) / values.length;
  const variance = values.reduce(
    (sum, value) => sum + (value - mean) ** 2,
    0,
  ) / values.length;
  return {
    count: values.length,
    min: sorted[0],
    median: percentile(sorted, 0.5),
    p90: percentile(sorted, 0.9),
    p95: percentile(sorted, 0.95),
    max: sorted.at(-1),
    mean,
    stddev: Math.sqrt(variance),
  };
}

export function summarizeMetrics(samples) {
  return Object.fromEntries(metricNames.map(name => {
    const values = samples.map(sample => sample[name]);
    if (values.some(value => !Number.isFinite(value))) {
      throw new Error(`metric ${name} is missing or non-finite`);
    }
    return [name, summarize(values)];
  }));
}

export function environmentFromSample(sample) {
  return {
    host: sample.host,
    engine: sample.engine,
    reactNativeVersion: sample.reactNativeVersion,
    reactVersion: sample.reactVersion,
    reactPeerRange: sample.reactPeerRange,
    hermesVersion: sample.hermesVersion,
    bundleHash: sample.bundleHash,
    bundleBytes: sample.bundleBytes,
    platform: process.platform,
    arch: process.arch,
  };
}

function execute(command, args, cwd) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {cwd});
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', chunk => { stdout += chunk; });
    child.stderr.on('data', chunk => { stderr += chunk; });
    child.on('error', reject);
    child.on('exit', code => resolve({code, stdout, stderr}));
  });
}

export async function runHeadlessSample({
  root,
  host,
  bundle,
  iterations,
  timeoutMs,
  settleMs,
  workload,
  seed,
  profile = 'macos-rn87',
  addon = null,
  requireReactFabric = true,
  failOnComponentFallback = true,
}) {
  const args = [
    'headless', '--bundle', bundle,
    '--iterations', String(iterations),
    '--timeout-ms', String(timeoutMs),
    '--settle-ms', String(settleMs),
    '--workload', workload,
    '--seed', String(seed),
    '--profile', profile,
    '--require-react-fabric', String(requireReactFabric),
    '--require-no-pending-work', 'true',
    '--fail-on-component-fallback', String(failOnComponentFallback),
  ];
  if (addon != null) {
    args.push('--addon', addon);
  }
  const execution = await execute(host, args, root);
  if (execution.code !== 0) {
    throw new Error(
      `react-native-simulator exited with ${execution.code}:\n` +
      execution.stderr,
    );
  }
  const metricsLine = execution.stdout
    .trim()
    .split('\n')
    .findLast(line => line.startsWith('{"host":"react-native-simulator"'));
  if (metricsLine == null) {
    throw new Error('react-native-simulator did not emit metrics');
  }
  const metrics = JSON.parse(metricsLine);
  if (metrics.schemaVersion !== 3) {
    throw new Error(`unsupported runtime metrics schema ${metrics.schemaVersion}`);
  }
  for (const gate of [
    'bundleLoaded',
    'workloadReady',
    'workloadComplete',
    'fabric',
    'yoga',
  ]) {
    if (metrics[gate] !== true) {
      throw new Error(`react-native-simulator failed correctness gate ${gate}`);
    }
  }
  if (metrics.workloadTimedOut || metrics.pendingWork ||
      metrics.requirements?.passed !== true || metrics.jsErrors !== 0) {
    throw new Error('react-native-simulator did not finish cleanly');
  }
  return metrics;
}
