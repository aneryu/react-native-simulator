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
const rntesterAddon = [
  path.join(buildDirectory, 'runtime', 'addons', 'rntester', 'rns-addon-rntester.dylib'),
  path.join(buildDirectory, 'runtime', 'addons', 'rntester', 'rns-addon-rntester.so'),
  path.join(buildDirectory, 'runtime', 'rns-addon-rntester.dylib'),
  path.join(buildDirectory, 'runtime', 'rns-addon-rntester.so'),
].find(candidate => existsSync(candidate));

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
assert.equal(profileOnly.schemaVersion, 3);
assert.equal(profileOnly.jsErrors, 0);
assert.equal(profileOnly.workloadComplete, true);
assert.ok(Array.isArray(profileOnly.addons));
assert.ok(profileOnly.addons.some(addon => addon.name === 'safe-area'));
assert.ok(profileOnly.nativeCapabilities.modules.some(
  module => module.name === 'PlatformConstants' && module.owner === 'android-rn87'));
assert.ok(profileOnly.nativeCapabilities.components.some(
  component => component.name === 'SafeAreaView' && component.owner === 'android-rn87'));
assert.ok(profileOnly.nativeCapabilities.components.some(
  component => component.name === 'RNCSafeAreaProvider' &&
    component.owner === 'addon:safe-area'));
assert.ok(!profileOnly.nativeCapabilities.modules.some(
  module => module.owner === 'addon:compat-rn73'));

assert.ok(rntesterAddon, 'rntester MODULE was not built');
const withRnTester = run('tests/fixtures/rntester-addon-probe.js', [
  '--addon',
  rntesterAddon,
]);
assert.ok(withRnTester.addons.some(addon => addon.name === 'rntester'));
assert.ok(withRnTester.nativeCapabilities.modules.some(
  module => module.name === 'NativeCxxModuleExampleCxx' &&
    module.owner === 'addon:rntester'));
assert.ok(withRnTester.nativeCapabilities.modules.some(
  module => module.name === 'ScreenshotManager' &&
    module.owner === 'addon:rntester'));

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
assert.ok(withExpo.addons.some(addon => addon.name === 'expo'));
assert.ok(withExpo.nativeCapabilities.modules.some(
  module => module.name === 'ExpoAsset' && module.owner === 'addon:expo'));
assert.ok(withExpo.nativeCapabilities.modules.some(
  module => module.name === 'ExponentConstants' && module.owner === 'addon:expo'));

console.log('RN profile, RN Tester addon, and Expo addon isolation verified');
