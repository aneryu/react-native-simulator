import {mkdir, writeFile} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import path from 'node:path';
import {
  environmentFromSample,
  metricNames,
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
  const requestedBuild = process.env.RNS_BUILD_DIR;
  const buildDirectory = requestedBuild == null
    ? (existsSync(path.join(root, 'build', 'final', 'runtime', 'rnsim'))
        ? path.join(root, 'build', 'final')
        : path.join(root, 'build'))
    : path.resolve(root, requestedBuild);
  const defaultHost = path.join(buildDirectory, 'runtime', 'rnsim');
  const options = {
    baselineBinary: defaultHost,
    candidateBinary: defaultHost,
    baselineBundle: null,
    candidateBundle: null,
    baselineProfile: 'macos-rn87',
    candidateProfile: 'macos-rn87',
    baselineAddon: null,
    candidateAddon: null,
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
    '--baseline-binary': ['baselineBinary', String],
    '--candidate-binary': ['candidateBinary', String],
    '--baseline-bundle': ['baselineBundle', String],
    '--candidate-bundle': ['candidateBundle', String],
    '--baseline-profile': ['baselineProfile', String],
    '--candidate-profile': ['candidateProfile', String],
    '--baseline-addon': ['baselineAddon', String],
    '--candidate-addon': ['candidateAddon', String],
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
  if (!Number.isInteger(options.runs) || options.runs < 2 || options.runs % 2 !== 0) {
    throw new Error('runs must be a positive even integer of at least 2');
  }
  for (const name of ['iterations', 'timeoutMs']) {
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
  if (options.baselineBundle == null || options.candidateBundle == null) {
    throw new Error('--baseline-bundle and --candidate-bundle are required');
  }
  for (const name of [
    'baselineBinary',
    'candidateBinary',
    'baselineBundle',
    'candidateBundle',
  ]) {
    options[name] = path.resolve(root, options[name]);
  }
  return options;
}

function deltaPercent(baseline, candidate) {
  if (baseline === 0) return candidate === 0 ? 0 : null;
  return ((candidate / baseline) - 1) * 100;
}

function compareSummaries(baseline, candidate) {
  return Object.fromEntries(metricNames.map(name => [
    name,
    Object.fromEntries(
      ['min', 'median', 'p90', 'p95', 'max', 'mean'].map(stat => [
        stat,
        deltaPercent(baseline[name][stat], candidate[name][stat]),
      ]),
    ),
  ]));
}

const options = parseArgs(process.argv.slice(2));
const variants = {
  baseline: {
    host: options.baselineBinary,
    bundle: options.baselineBundle,
    profile: options.baselineProfile,
    addon: options.baselineAddon,
  },
  candidate: {
    host: options.candidateBinary,
    bundle: options.candidateBundle,
    profile: options.candidateProfile,
    addon: options.candidateAddon,
  },
};
const samples = {baseline: [], candidate: []};
const executions = [];

async function executeVariant(variant, phase) {
  const metrics = await runHeadlessSample({
    root,
    ...variants[variant],
    iterations: options.iterations,
    timeoutMs: options.timeoutMs,
    settleMs: options.settleMs,
    workload: options.workload,
    seed: options.seed,
    requireReactFabric: options.requireReactFabric,
    failOnComponentFallback: options.failOnComponentFallback,
  });
  executions.push({phase, variant, metrics});
  if (phase === 'sample') samples[variant].push(metrics);
  process.stderr.write(
    `${phase} ${variant} ${samples[variant].length}/${options.runs}: ` +
      `render=${metrics.renderMs.toFixed(3)}ms ` +
      `rss=${(metrics.peakResidentBytes / 1048576).toFixed(2)}MiB\n`,
  );
}

for (let index = 0; index < options.warmup; index += 1) {
  const order = index % 2 === 0
    ? ['baseline', 'candidate']
    : ['candidate', 'baseline'];
  for (const variant of order) await executeVariant(variant, 'warmup');
}

for (let block = 0; block < options.runs / 2; block += 1) {
  const order = block % 2 === 0
    ? ['baseline', 'candidate', 'candidate', 'baseline']
    : ['candidate', 'baseline', 'baseline', 'candidate'];
  for (const variant of order) await executeVariant(variant, 'sample');
}

const baselineSummary = summarizeMetrics(samples.baseline);
const candidateSummary = summarizeMetrics(samples.candidate);
const report = {
  schemaVersion: 2,
  generatedAt: new Date().toISOString(),
  methodology: 'independent-process-ABBA-BAAB',
  config: options,
  environment: {
    baseline: environmentFromSample(samples.baseline[0]),
    candidate: environmentFromSample(samples.candidate[0]),
  },
  summary: {
    baseline: baselineSummary,
    candidate: candidateSummary,
    candidateDeltaPercent: compareSummaries(baselineSummary, candidateSummary),
  },
  samples,
  executions,
};
const outputPath = options.output == null
  ? path.join(
      root,
      'build',
      'benchmark-results',
      `compare-${new Date().toISOString().replaceAll(':', '-')}.json`,
    )
  : path.resolve(root, options.output);
await mkdir(path.dirname(outputPath), {recursive: true});
await writeFile(outputPath, `${JSON.stringify(report, null, 2)}\n`);
console.log(JSON.stringify({
  output: outputPath,
  methodology: report.methodology,
  medianDeltaPercent: Object.fromEntries(
    metricNames.map(name => [
      name,
      report.summary.candidateDeltaPercent[name].median,
    ]),
  ),
}));
