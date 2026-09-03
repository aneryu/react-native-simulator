#!/usr/bin/env node
// Packager for the default RN Tester conformance integration. This is not part
// of the core configure/build/test/runtime path and does not embed a bundle.
// It invokes Metro from the RN 0.87 checkout after yarn install.
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {
  copyFileSync,
  existsSync,
  mkdirSync,
  readdirSync,
  readFileSync,
  writeFileSync,
} from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import {fileURLToPath} from 'node:url';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const projectRoot = path.resolve(scriptDirectory, '..', '..');
const rnRoot = path.join(projectRoot, 'third_party', 'react-native');
const testerRoot = path.join(rnRoot, 'packages', 'rn-tester');
const metroConfig = path.join(testerRoot, 'metro.config.js');

function printHelp() {
  process.stdout.write(`Pack RN Tester for react-native-simulator.

Usage:
  node tools/rntester/bundle.mjs [options]

This is companion tooling for the default RN Tester integration. Core rnsim
configure/build/test paths still do not require Node. The generated bundle is
caller-built output under build/ and is not embedded in the runtime.

Options:
  --platform android|ios   Metro platform (default android)
  --dev true|false         Metro __DEV__ bundle (default true)
  --out-dir DIR            Output directory (default build/rntester)
  --build-dir DIR          Simulator build dir for hermesc/rnsim
                           (default build)
  --install                yarn install the RN checkout if needed
  --offline                Pass --offline to yarn install
  --reset-cache            Reset Metro cache
  --hbc                    Also compile the JS bundle with hermesc
  --skip-inventory         Do not run inventory-rntester.mjs
  --help                   Show this help

After a successful bundle:

  build/release/runtime/rnsim --config build/rntester/rnsim.json

  cmake -S . -B build -DRNS_RNTESTER_BUNDLE=build/rntester/RNTesterApp.android.jsbundle
  ctest --test-dir build --output-on-failure -R rntester-android-startup
`);
}

function parseBoolean(name, value) {
  if (value === 'true' || value === '1') {
    return true;
  }
  if (value === 'false' || value === '0') {
    return false;
  }
  throw new Error(`${name} must be true or false`);
}

function parseArgs(argv) {
  const options = {
    platform: 'android',
    dev: true,
    outDir: path.join(projectRoot, 'build', 'rntester'),
    buildDir: path.join(projectRoot, 'build'),
    install: false,
    offline: false,
    resetCache: false,
    hbc: false,
    skipInventory: false,
  };
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    const next = () => {
      const value = argv[++index];
      if (value == null) {
        throw new Error(`${argument} requires a value`);
      }
      return value;
    };
    if (argument === '--help' || argument === '-h') {
      printHelp();
      process.exit(0);
    } else if (argument === '--platform') {
      options.platform = next();
    } else if (argument === '--dev') {
      options.dev = parseBoolean(argument, next());
    } else if (argument === '--out-dir') {
      options.outDir = path.resolve(next());
    } else if (argument === '--build-dir') {
      options.buildDir = path.resolve(next());
    } else if (argument === '--install') {
      options.install = true;
    } else if (argument === '--offline') {
      options.offline = true;
    } else if (argument === '--reset-cache') {
      options.resetCache = true;
    } else if (argument === '--hbc') {
      options.hbc = true;
    } else if (argument === '--skip-inventory') {
      options.skipInventory = true;
    } else {
      throw new Error(`Unknown argument: ${argument}`);
    }
  }
  if (options.platform !== 'android' && options.platform !== 'ios') {
    throw new Error('--platform must be android or ios');
  }
  return options;
}

function run(command, args, spawnOptions = {}) {
  const result = spawnSync(command, args, {
    encoding: 'utf8',
    stdio: 'inherit',
    ...spawnOptions,
  });
  if (result.error) {
    throw result.error;
  }
  if (result.status !== 0) {
    throw new Error(
      `${command} ${args.join(' ')} failed with status ${result.status}`,
    );
  }
  return result;
}

function findYarn() {
  for (const candidate of ['yarn', 'yarnpkg']) {
    const result = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (result.status === 0) {
      return candidate;
    }
  }
  const corepack = spawnSync('corepack', ['yarn', '--version'], {
    encoding: 'utf8',
  });
  if (corepack.status === 0) {
    return 'corepack-yarn';
  }
  throw new Error(
    'yarn 1.22.x is required to install the RN 0.87 checkout. Install Yarn or enable Corepack.',
  );
}

function yarnCommand(yarn, args, spawnOptions) {
  if (yarn === 'corepack-yarn') {
    run('corepack', ['yarn', ...args], spawnOptions);
    return;
  }
  run(yarn, args, spawnOptions);
}

function sha256(filePath) {
  return createHash('sha256').update(readFileSync(filePath)).digest('hex');
}

const options = parseArgs(process.argv.slice(2));

if (!existsSync(path.join(testerRoot, 'js', `RNTesterApp.${options.platform}.js`))) {
  throw new Error(`RN Tester is missing at ${testerRoot}`);
}
if (!existsSync(metroConfig)) {
  throw new Error(`RN Tester metro.config.js is missing: ${metroConfig}`);
}

if (!options.skipInventory) {
  run(process.execPath, [
    path.join(projectRoot, 'tools', 'diagnostics', 'inventory-rntester.mjs'),
  ], {cwd: projectRoot});
}

const nodeModules = path.join(rnRoot, 'node_modules');
const yarn = findYarn();
if (!existsSync(nodeModules)) {
  if (!options.install) {
    throw new Error(
      `RN checkout has no node_modules at ${nodeModules}.\n` +
        `Re-run with --install to yarn install the RN 0.87 monorepo, or install it yourself:\n` +
        `  yarn --cwd ${path.relative(projectRoot, rnRoot) || '.'} install --frozen-lockfile`,
    );
  }
  const installArgs = ['install', '--frozen-lockfile'];
  if (options.offline) {
    installArgs.push('--offline');
  }
  yarnCommand(yarn, installArgs, {cwd: rnRoot});
}

const codegenParser = path.join(
  rnRoot,
  'packages',
  'react-native-codegen',
  'lib',
  'parsers',
  'flow',
  'parser.js',
);
if (!existsSync(codegenParser)) {
  yarnCommand(yarn, ['build'], {
    cwd: path.join(rnRoot, 'packages', 'react-native-codegen'),
  });
}
if (!existsSync(codegenParser)) {
  throw new Error(
    `@react-native/codegen lib was not built. Metro cannot transform RN Tester.\n` +
      `From the RN checkout run: yarn --cwd packages/react-native-codegen build`,
  );
}

mkdirSync(options.outDir, {recursive: true});
const entryFile = `js/RNTesterApp.${options.platform}.js`;
const bundleName = `RNTesterApp.${options.platform}.jsbundle`;
const bundlePath = path.join(options.outDir, bundleName);
const mapPath = path.join(options.outDir, `${bundleName}.map`);
const assetsDir = path.join(options.outDir, 'assets');
mkdirSync(assetsDir, {recursive: true});

const bundleArgs = [
  'react-native',
  'bundle',
  '--entry-file',
  entryFile,
  '--platform',
  options.platform,
  '--dev',
  options.dev ? 'true' : 'false',
  '--minify',
  options.dev ? 'false' : 'true',
  '--bundle-output',
  bundlePath,
  '--sourcemap-output',
  mapPath,
  '--assets-dest',
  assetsDir,
  '--config',
  metroConfig,
];
if (options.resetCache) {
  bundleArgs.push('--reset-cache');
}
yarnCommand(yarn, bundleArgs, {cwd: testerRoot});
if (!existsSync(bundlePath)) {
  throw new Error(`Metro did not write ${bundlePath}`);
}

// Keep the pinned RN checkout pristine while giving the RN Tester inline-image
// cases a stable, decodable source. picsum currently returns a Cloudflare 503
// to this macOS host, so normalize only the two checked-in fixture literals in
// the caller-built output.
let bundleSource = readFileSync(bundlePath, 'utf8');
for (const source of [
  'https://picsum.photos/100',
  'https://picsum.photos/50',
]) {
  bundleSource = bundleSource.replaceAll(
    source,
    'https://loremflickr.com/100/100/nature?lock=1',
  );
}
writeFileSync(bundlePath, bundleSource);

if (options.platform === 'android') {
  const drawableDest = path.join(assetsDir, 'drawable');
  mkdirSync(drawableDest, {recursive: true});
  const nativeDrawableRoots = [
    path.join(testerRoot, 'android', 'app', 'src', 'main', 'public_res', 'drawable'),
    path.join(testerRoot, 'android', 'app', 'src', 'main', 'res', 'drawable'),
  ];
  const raster = new Set(['.png', '.jpg', '.jpeg', '.webp', '.gif']);
  for (const root of nativeDrawableRoots) {
    if (!existsSync(root)) {
      continue;
    }
    for (const entry of readdirSync(root)) {
      const extension = path.extname(entry).toLowerCase();
      if (!raster.has(extension)) {
        continue;
      }
      copyFileSync(path.join(root, entry), path.join(drawableDest, entry));
    }
  }
}

let bytecodePath = null;
if (options.hbc) {
  const hermesc = process.env.HERMESC ||
    path.join(options.buildDir, 'bin', 'hermesc');
  if (!existsSync(hermesc)) {
    throw new Error(
      `hermesc not found at ${hermesc}. Build the simulator first or set HERMESC.`,
    );
  }
  bytecodePath = path.join(
    options.outDir,
    `RNTesterApp.${options.platform}.hbc`,
  );
  run(hermesc, ['-emit-binary', '-out', bytecodePath, bundlePath]);
}

const runtimeBundle = bytecodePath ?? bundlePath;
const addonPath = path.join(
  options.buildDir,
  'runtime',
  'rns-addon-rntester.dylib',
);
const configPath = path.join(options.outDir, 'rnsim.json');
const relativeBundle = path.relative(options.outDir, runtimeBundle);
const relativeAddon = path.relative(options.outDir, addonPath);
const config = {
  schemaVersion: 2,
  reactNative: '0.87.0',
  platform: options.platform,
  appKey: 'RNTesterApp',
  bundle: relativeBundle.split(path.sep).join('/'),
  viewport: {
    width: 392.7273,
    height: 753.4545,
    pointScaleFactor: 2.75,
  },
  addons: [{path: relativeAddon.split(path.sep).join('/')}],
};
writeFileSync(configPath, `${JSON.stringify(config, null, 2)}\n`);

const adapterSource = path.join(
  projectRoot,
  'tests',
  'fixtures',
  'rntester-startup-adapter.js',
);
const adapterCopy = path.join(options.outDir, 'rntester-startup-adapter.js');
copyFileSync(adapterSource, adapterCopy);

const manifest = {
  appKey: 'RNTesterApp',
  platform: options.platform,
  entry: entryFile,
  dev: options.dev,
  bundle: bundlePath,
  bundleSha256: sha256(bundlePath),
  bytecode: bytecodePath,
  assets: assetsDir,
  config: configPath,
  source: path.relative(projectRoot, testerRoot),
};
writeFileSync(
  path.join(options.outDir, 'manifest.json'),
  `${JSON.stringify(manifest, null, 2)}\n`,
);

const rnsim = path.join(options.buildDir, 'runtime', 'rnsim');
const addonFlag = existsSync(addonPath)
  ? ` \\\n  --addon ${path.relative(projectRoot, addonPath)}`
  : '';
process.stdout.write(`RN Tester bundle written to ${bundlePath}
Config: ${configPath}

Interactive:
  ${path.relative(projectRoot, rnsim) || rnsim} --config ${path.relative(projectRoot, configPath)}

Headless startup smoke:
  ${path.relative(projectRoot, rnsim) || rnsim} headless \\
  --profile ${options.platform}-rn87 \\
  --app-key RNTesterApp \\
  --bundle ${path.relative(projectRoot, runtimeBundle)} \\
  --bundle ${path.relative(projectRoot, adapterSource)}${addonFlag} \\
  --timeout-ms 15000

Optional CTest (reconfigure after bundling):
  cmake -S . -B ${path.relative(projectRoot, options.buildDir)} \\
    -DRNS_RNTESTER_BUNDLE=${path.relative(projectRoot, runtimeBundle)}
  ctest --test-dir ${path.relative(projectRoot, options.buildDir)} --output-on-failure -R rntester-android-startup
`);
