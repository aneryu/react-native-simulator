import {mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {spawn} from 'node:child_process';
const root = process.cwd();
const requestedBuild = process.env.RNS_BUILD_DIR;
const buildDirectory = requestedBuild == null
  ? (existsSync(path.join(root, 'build', 'release', 'runtime', 'rnsim'))
      ? path.join(root, 'build', 'release')
      : existsSync(path.join(root, 'build', 'final', 'runtime', 'rnsim'))
        ? path.join(root, 'build', 'final')
        : path.join(root, 'build'))
  : path.resolve(root, requestedBuild);
const host = path.join(buildDirectory, 'runtime', 'rnsim');
const hermesc = path.join(buildDirectory, 'bin', 'hermesc');

function run(command, args) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {cwd: root});
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', chunk => { stdout += chunk; });
    child.stderr.on('data', chunk => { stderr += chunk; });
    child.on('error', reject);
    child.on('exit', (code, signal) => resolve({code, signal, stdout, stderr}));
  });
}

function metricsOf(execution) {
  const line = execution.stdout
    .trim()
    .split('\n')
    .findLast(value => value.startsWith('{"host":"react-native-simulator"'));
  return line == null ? null : JSON.parse(line);
}

async function compile(source, output) {
  const result = await run(hermesc, ['-emit-binary', '-out', output, source]);
  if (result.code !== 0) {
    throw new Error(`fixture compilation failed:\n${result.stderr}`);
  }
}

const fixtureDirectory = await mkdtemp(
  path.join(tmpdir(), 'react-native-simulator-engine-'),
);
try {
  const smokeSource = path.join(root, 'tests', 'fixtures', 'runtime-smoke.js');
  const smokeBundle = path.join(fixtureDirectory, 'runtime-smoke.hbc');
  await compile(smokeSource, smokeBundle);

  for (const iterations of [5, 50]) {
    const execution = await run(host, [
      'headless', '--bundle', smokeBundle,
      '--iterations', String(iterations),
      '--timeout-ms', '1000',
    ]);
    const metrics = metricsOf(execution);
    if (execution.code !== 0 || execution.signal != null ||
        metrics?.schemaVersion !== 2 ||
        metrics?.validationMode !== 'workload' ||
        metrics?.bundleLoaded !== true || metrics?.fabric !== true ||
        metrics?.yoga !== true || metrics?.timersPassed !== true ||
        metrics?.customNativeModule !== true ||
        metrics?.workloadIterations !== iterations ||
        metrics?.cpuIterations !== iterations ||
        metrics?.workloadComplete !== true ||
        metrics?.workloadTimedOut !== false || metrics?.jsErrors !== 0 ||
        metrics?.heapAllocatedBytes <= 0 || metrics?.residentBytes <= 0 ||
        metrics?.workloadUserCpuMs < 0 ||
        metrics?.nativeCapabilities?.modules?.NativeMicrotasksCxx !==
          'real-headless') {
      throw new Error(
        `runtime smoke failed:\n${execution.stdout}\n${execution.stderr}`,
      );
    }
  }
  console.log(JSON.stringify({runtimeSmoke: 'verified'}));

  const secondarySource = path.join(fixtureDirectory, 'secondary.js');
  const secondaryBundle = path.join(fixtureDirectory, 'secondary.hbc');
  await writeFile(
    secondarySource,
    'globalThis.RN$SecondaryBundleLoaded = true;\n',
  );
  await compile(secondarySource, secondaryBundle);
  const multiExecution = await run(host, [
    'headless', '--bundle', smokeBundle,
    '--bundle', secondaryBundle,
    '--iterations', '5',
    '--timeout-ms', '1000',
  ]);
  const multiMetrics = metricsOf(multiExecution);
  if (multiExecution.code !== 0 || multiMetrics?.bundlesLoaded !== 2 ||
      multiMetrics?.bundles[1]?.requestedByJS !== false) {
    throw new Error('CLI multi-bundle loading failed');
  }
  console.log(JSON.stringify({cliMultiBundle: 'verified'}));

  const dynamicEntry = path.join(fixtureDirectory, 'dynamic-entry.js');
  const dynamicNext = path.join(fixtureDirectory, 'dynamic-next.js');
  await writeFile(
    dynamicEntry,
    `RN$SimulatorWorkload.ready();\n` +
      `RN$Simulator.loadBundle(${JSON.stringify(dynamicNext)})` +
      `.then(function (metadata) {\n` +
      `  if (!metadata.hash || metadata.bytes <= 0) throw new Error('metadata');\n` +
      `  RN$SimulatorWorkload.complete();\n` +
      `});\n`,
  );
  await writeFile(dynamicNext, 'globalThis.RN$DynamicBundleLoaded = true;\n');
  const dynamicExecution = await run(host, [
    'headless', '--bundle', dynamicEntry, '--timeout-ms', '1000',
  ]);
  const dynamicMetrics = metricsOf(dynamicExecution);
  if (dynamicExecution.code !== 0 || dynamicMetrics?.bundlesLoaded !== 2 ||
      dynamicMetrics?.bundles[1]?.requestedByJS !== true ||
      dynamicMetrics?.validationMode !== 'runtime') {
    throw new Error('dynamic bundle loading failed');
  }
  console.log(JSON.stringify({dynamicBundleAPI: 'verified'}));

  const missingEntry = path.join(fixtureDirectory, 'missing-entry.js');
  const missingPath = path.join(fixtureDirectory, 'missing.hbc');
  await writeFile(
    missingEntry,
    `RN$SimulatorWorkload.ready();\n` +
      `RN$Simulator.loadBundle(${JSON.stringify(missingPath)})` +
      `.then(function () { throw new Error('resolved'); }, function () {\n` +
      `  RN$SimulatorWorkload.complete();\n` +
      `});\n`,
  );
  const missingExecution = await run(host, [
    'headless', '--bundle', missingEntry, '--timeout-ms', '1000',
  ]);
  const missingMetrics = metricsOf(missingExecution);
  if (missingExecution.code === 0 ||
      missingMetrics?.bundleLoadFailed !== true ||
      missingMetrics?.bundles[1]?.loaded !== false) {
    throw new Error('dynamic bundle rejection failed');
  }
  console.log(JSON.stringify({dynamicBundleRejection: 'verified'}));

  const metricsPath = path.join(fixtureDirectory, 'metrics.json');
  const outputExecution = await run(host, [
    'headless', '--bundle', smokeBundle,
    '--iterations', '5',
    '--output', metricsPath,
  ]);
  const persisted = JSON.parse(await readFile(metricsPath, 'utf8'));
  if (outputExecution.code !== 0 || persisted.workloadIterations !== 5) {
    throw new Error('metrics output persistence failed');
  }
  console.log(JSON.stringify({outputProtocol: 'verified'}));

  const timeoutSource = path.join(fixtureDirectory, 'timeout.js');
  await writeFile(
    timeoutSource,
    'RN$SimulatorWorkload.ready(); setTimeout(function () {}, 1000);\n',
  );
  const timeoutExecution = await run(host, [
    'headless', '--bundle', timeoutSource, '--timeout-ms', '20',
  ]);
  const timeoutMetrics = metricsOf(timeoutExecution);
  if (timeoutExecution.code === 0 ||
      timeoutMetrics?.workloadTimedOut !== true ||
      timeoutMetrics?.pendingWork !== true) {
    throw new Error('timeout boundary failed');
  }
  console.log(JSON.stringify({timeoutBoundary: 'verified'}));

  const pendingSource = path.join(fixtureDirectory, 'pending.js');
  await writeFile(
    pendingSource,
    'RN$SimulatorWorkload.ready(); setTimeout(function () {}, 1000); ' +
      'RN$SimulatorWorkload.complete();\n',
  );
  const pendingExecution = await run(host, [
    'headless', '--bundle', pendingSource,
    '--timeout-ms', '100',
    '--require-no-pending-work', 'true',
  ]);
  const pendingMetrics = metricsOf(pendingExecution);
  if (pendingExecution.code === 0 || pendingMetrics?.pendingWork !== true ||
      pendingMetrics?.requirements?.passed !== false) {
    throw new Error('pending-work strict gate failed');
  }
  console.log(JSON.stringify({pendingWorkGate: 'verified'}));

  const fabricRequirementExecution = await run(host, [
    'headless', '--bundle', smokeBundle,
    '--iterations', '5',
    '--timeout-ms', '1000',
    '--require-react-fabric', 'true',
  ]);
  const fabricRequirementMetrics = metricsOf(fabricRequirementExecution);
  if (fabricRequirementExecution.code === 0 ||
      fabricRequirementMetrics?.reactFabric !== false ||
      fabricRequirementMetrics?.requirements?.passed !== false) {
    throw new Error('React Fabric strict gate failed');
  }
  console.log(JSON.stringify({reactFabricGate: 'verified'}));

  const comparisonPath = path.join(fixtureDirectory, 'comparison.json');
  const comparisonExecution = await run(process.execPath, [
    path.join(root, 'tools', 'benchmark', 'compare.mjs'),
    '--baseline-binary', host,
    '--candidate-binary', host,
    '--baseline-bundle', smokeBundle,
    '--candidate-bundle', smokeBundle,
    '--runs', '2',
    '--warmup', '0',
    '--iterations', '5',
    '--require-react-fabric', 'false',
    '--fail-on-component-fallback', 'false',
    '--output', comparisonPath,
  ]);
  const comparison = JSON.parse(await readFile(comparisonPath, 'utf8'));
  if (comparisonExecution.code !== 0 ||
      comparison.schemaVersion !== 2 ||
      comparison.methodology !== 'independent-process-ABBA-BAAB' ||
      comparison.samples.baseline.length !== 2 ||
      comparison.samples.candidate.length !== 2) {
    throw new Error(`ABBA comparison failed:\n${comparisonExecution.stderr}`);
  }
  console.log(JSON.stringify({abbaComparison: 'verified'}));

  const errorSource = path.join(fixtureDirectory, 'error.js');
  await writeFile(
    errorSource,
    'throw new Error("react-native-simulator-error-boundary");\n',
  );
  const errorExecution = await run(host, [
    'headless', '--bundle', errorSource, '--timeout-ms', '100',
  ]);
  if (errorExecution.code === 0 ||
      !errorExecution.stderr.includes('react-native-simulator-error-boundary')) {
    throw new Error('ReactInstance error boundary failed');
  }
  console.log(JSON.stringify({errorBoundary: 'verified'}));
} finally {
  await rm(fixtureDirectory, {recursive: true, force: true});
}
