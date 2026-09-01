import {mkdir, writeFile} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import path from 'node:path';
import {
  environmentFromSample,
  runHeadlessSample,
  summarizeMetrics,
} from './lib/benchmark-utils.mjs';

const root = process.cwd();

function boolean(value) {
  if (value === 'true' || value === '1') return true;
  if (value === 'false' || value === '0') return false;
  throw new Error(`Expected true or false, received ${value}`);
}

function parseArgs(argv) {
  const options = {
    bundle: process.env.RNS_BUNDLE ?? null,
    profile: 'macos-rn87',
    addon: null,
    runs: 10,
    warmup: 2,
    iterations: 100,
    timeoutMs: 5000,
    settleMs: 0,
    seed: 1,
    workload: 'mixed',
    requireReactFabric: true,
    failOnComponentFallback: true,
    output: null,
  };
  const mappings = {
    '--bundle': ['bundle', String],
    '--profile': ['profile', String],
    '--addon': ['addon', String],
    '--runs': ['runs', Number],
    '--warmup': ['warmup', Number],
    '--iterations': ['iterations', Number],
    '--timeout-ms': ['timeoutMs', Number],
    '--settle-ms': ['settleMs', Number],
    '--seed': ['seed', Number],
    '--workload': ['workload', String],
    '--require-react-fabric': ['requireReactFabric', boolean],
    '--fail-on-component-fallback': ['failOnComponentFallback', boolean],
    '--output': ['output', String],
  };
  for (let index = 0; index < argv.length; index += 2) {
    const mapping = mappings[argv[index]];
    if (mapping == null || argv[index + 1] == null) {
      throw new Error(`Unknown or incomplete option: ${argv[index] ?? '(missing)'}`);
    }
    options[mapping[0]] = mapping[1](argv[index + 1]);
  }
  for (const name of ['runs', 'iterations', 'timeoutMs']) {
    if (!Number.isInteger(options[name]) || options[name] < 1) {
      throw new Error(`${name} must be a positive integer`);
    }
  }
  if (!Number.isInteger(options.warmup) || options.warmup < 0 ||
      !Number.isInteger(options.settleMs) || options.settleMs < 0) {
    throw new Error('warmup and settle-ms must be non-negative integers');
  }
  if (options.workload.length === 0) {
    throw new Error('workload must not be empty');
  }
  if (options.bundle == null) {
    throw new Error('--bundle is required');
  }
  options.bundle = path.resolve(root, options.bundle);
  return options;
}

const options = parseArgs(process.argv.slice(2));
const requestedBuild = process.env.RNS_BUILD_DIR;
const buildDirectory = requestedBuild == null
  ? (existsSync(path.join(root, 'build', 'final', 'runtime', 'rnsim'))
      ? path.join(root, 'build', 'final')
      : path.join(root, 'build'))
  : path.resolve(root, requestedBuild);
const host = process.env.RNS_BINARY ??
  path.join(buildDirectory, 'runtime', 'rnsim');
const samples = [];
const totalRuns = options.warmup + options.runs;

for (let index = 0; index < totalRuns; index += 1) {
  const metrics = await runHeadlessSample({
    root,
    host,
    bundle: options.bundle,
    ...options,
  });
  if (index >= options.warmup) {
    samples.push(metrics);
  }
  process.stderr.write(
    `${index < options.warmup ? 'warmup' : 'sample'} ` +
      `${index + 1}/${totalRuns}: ` +
      `bundle=${metrics.bundleAndDrainMs.toFixed(3)}ms\n`,
  );
}

const summary = summarizeMetrics(samples);
const report = {
  schemaVersion: 2,
  generatedAt: new Date().toISOString(),
  config: options,
  environment: environmentFromSample(samples[0]),
  summary,
  samples,
};

const outputPath = options.output == null
  ? path.join(
      root,
      'build',
      'benchmark-results',
      `${new Date().toISOString().replaceAll(':', '-')}.json`,
    )
  : path.resolve(root, options.output);
await mkdir(path.dirname(outputPath), {recursive: true});
await writeFile(outputPath, `${JSON.stringify(report, null, 2)}\n`);
console.log(JSON.stringify({output: outputPath, summary}));
