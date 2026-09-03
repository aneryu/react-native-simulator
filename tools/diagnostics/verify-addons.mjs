import assert from 'node:assert/strict';
import {existsSync, readFileSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import path from 'node:path';

const root = process.cwd();
const requestedBuild = process.env.RNS_BUILD_DIR;
const buildDirectory = requestedBuild == null
  ? (existsSync(path.join(root, 'build', 'release', 'runtime', 'rnsim'))
      ? path.join(root, 'build', 'release')
      : existsSync(path.join(root, 'build', 'final', 'runtime', 'rnsim'))
        ? path.join(root, 'build', 'final')
        : path.join(root, 'build'))
  : path.resolve(root, requestedBuild);
const binary = path.join(buildDirectory, 'runtime', 'rnsim');
const rntesterAddon = path.join(
  buildDirectory, 'runtime', 'rns-addon-rntester.dylib');

function run(fixture, extraArgs = []) {
  const result = spawnSync(
    binary,
    ['headless', '--bundle', fixture, '--profile', 'android-rn87',
      '--timeout-ms', '1000', ...extraArgs],
    {encoding: 'utf8'},
  );
  const metricsLine = result.stdout
    .trim()
    .split('\n')
    .findLast(line => line.startsWith('{'));
  assert.ok(metricsLine, result.stderr || 'metrics JSON is missing');
  assert.equal(result.signal, null, result.stderr);
  assert.equal(result.status, 0, result.stderr);
  return JSON.parse(metricsLine);
}

const profileOnly = run('tests/fixtures/runtime-smoke.js');
assert.equal(profileOnly.jsErrors, 0);
assert.equal(profileOnly.workloadComplete, true);
assert.deepEqual(profileOnly.addons, []);
assert.ok(profileOnly.rnFrameworkModules.includes('PlatformConstants'));
assert.deepEqual(profileOnly.addonModules, []);

const withRnTester = run('tests/fixtures/rntester-addon-probe.js', [
  '--addon',
  rntesterAddon,
]);
assert.deepEqual(withRnTester.addons, ['rntester']);
assert.deepEqual(withRnTester.addonModules.sort(), [
  'NativeCxxModuleExampleCxx',
  'ScreenshotManager',
].sort());

for (const sourcePath of [
  'runtime/src/modules/HeadlessRNModules.cpp',
  'runtime/src/modules/HeadlessTurboModules.cpp',
  'runtime/src/core/SimulatorEngine.cpp',
]) {
  const source = readFileSync(sourcePath, 'utf8');
  assert.doesNotMatch(
    source,
    /NativeCxxModuleExampleCxx|ScreenshotManager|RNTReportFullyDrawnView|RNTMyNativeView|RNTMyLegacyNativeView|AndroidPopupMenu/,
  );
  assert.doesNotMatch(
    source,
    /ExpoAsset|ExpoKeepAwake|ExpoSplashScreen|ExpoFontLoader|ExpoSystemUI|ExponentConstants|ExpoModulesCore|ExpoFetchModule|ExpoLinking/,
  );
}

const withExpo = run('tests/fixtures/expo-addon-probe.js', ['--addon', 'expo']);
assert.deepEqual(withExpo.addons, ['expo']);
assert.ok(withExpo.addonModules.includes('ExpoAsset'));
assert.ok(withExpo.addonModules.includes('ExponentConstants'));

console.log('RN profile, RN Tester addon, and Expo addon isolation verified');
