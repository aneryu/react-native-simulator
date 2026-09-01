import {execFileSync} from 'node:child_process';
import {readFile, readdir} from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';

const projectRoot = process.cwd();
let reactNativeRoot = path.join(
  projectRoot,
  'third_party',
  'react-native',
  'packages',
  'react-native',
);
let pretty = true;

for (let index = 2; index < process.argv.length; index += 1) {
  const argument = process.argv[index];
  if (argument === '--rn-root') {
    reactNativeRoot = path.resolve(process.argv[++index]);
  } else if (argument === '--compact') {
    pretty = false;
  } else {
    throw new Error(`Unknown argument: ${argument}`);
  }
}

async function collectJavaScriptFiles(directory) {
  const result = [];
  for (const entry of await readdir(directory, {withFileTypes: true})) {
    const absolute = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      result.push(...await collectJavaScriptFiles(absolute));
    } else if (entry.isFile() && entry.name.endsWith('.js')) {
      result.push(absolute);
    }
  }
  return result;
}

function relativeSource(file) {
  return path.relative(reactNativeRoot, file).split(path.sep).join('/');
}

function sourceScope(name, source) {
  const normalized = source.toLowerCase();
  if (source.includes('/__tests__/') || source.includes('/__mocks__/') ||
      source.includes('/testing/') || source.includes('/samples/') ||
      name === 'SampleTurboModule') {
    return 'test';
  }
  if (normalized.includes('/devsupport/') ||
      normalized.includes('/debuggingoverlay/') ||
      /^(?:DevLoadingView|DevMenu|DevSettings|LogBox|RedBox|ReactDevTools)/
        .test(name)) {
    return 'devtools';
  }
  if (normalized.includes('/featureflags/') ||
      normalized.includes('/layoutconformance/') ||
      normalized.includes('/unimplementedview/')) {
    return 'internal';
  }
  return 'runtime';
}

function platformHints(name, source, contents) {
  const hints = new Set();
  const combined = `${name} ${source}`;
  if (/Android/.test(combined)) hints.add('android');
  if (/(?:IOS|iOS|ActionSheetIOS|PushNotificationIOS)/.test(combined)) {
    hints.add('ios');
  }
  for (const match of contents.matchAll(
    /excludedPlatforms\s*:\s*\[([^\]]*)\]/g,
  )) {
    for (const platform of match[1].matchAll(/['"]([^'"]+)['"]/g)) {
      hints.add(`not-${platform[1]}`);
    }
  }
  return [...hints].sort();
}

function addEntry(entries, name, source, contents) {
  const existing = entries.get(name) ?? {
    name,
    scopes: new Set(),
    platformHints: new Set(),
    sources: new Set(),
  };
  existing.scopes.add(sourceScope(name, source));
  for (const hint of platformHints(name, source, contents)) {
    existing.platformHints.add(hint);
  }
  existing.sources.add(source);
  entries.set(name, existing);
}

function serialize(entries) {
  return [...entries.values()]
    .sort((left, right) => left.name.localeCompare(right.name))
    .map(entry => ({
      name: entry.name,
      scopes: [...entry.scopes].sort(),
      platformHints: [...entry.platformHints].sort(),
      sources: [...entry.sources].sort(),
    }));
}

const packageJson = JSON.parse(
  await readFile(path.join(reactNativeRoot, 'package.json'), 'utf8'),
);
const scanRoots = [
  path.join(reactNativeRoot, 'Libraries'),
  path.join(reactNativeRoot, 'src', 'private'),
];
const files = (await Promise.all(scanRoots.map(collectJavaScriptFiles)))
  .flat()
  .sort();
const modules = new Map();
const components = new Map();

for (const file of files) {
  const contents = await readFile(file, 'utf8');
  const source = relativeSource(file);
  if (source.includes('/__tests__/') || source.includes('/__mocks__/')) {
    continue;
  }

  for (const match of contents.matchAll(
    /TurboModuleRegistry\s*\.\s*(?:get|getEnforcing)(?:<[^;]*?>)?\s*\(\s*['"]([^'"]+)['"]/g,
  )) {
    addEntry(modules, match[1], source, contents);
  }

  const componentPatterns = [
    /(?:export\s+default|=)\s*codegenNativeComponent[\s\S]{0,300}?\(\s*['"]([^'"]+)['"]/g,
    /NativeComponentRegistry\s*\.\s*get(?:<[^;]*?>)?\s*\(\s*['"]([^'"]+)['"]/g,
    /createReactNativeComponentClass\s*\(\s*['"]([^'"]+)['"]/g,
  ];
  for (const pattern of componentPatterns) {
    for (const match of contents.matchAll(pattern)) {
      addEntry(components, match[1], source, contents);
    }
  }
}

const reactNativeRepository = path.resolve(reactNativeRoot, '..', '..');
const revision = execFileSync(
  'git',
  ['-C', reactNativeRepository, 'rev-parse', 'HEAD'],
  {encoding: 'utf8'},
).trim();
const result = {
  schemaVersion: 1,
  source: 'react-native-js-codegen-spec-scan',
  reactNativeVersion: packageJson.version,
  reactNativeRevision: revision,
  scanRoots: scanRoots.map(relativeSource),
  notes: [
    'This is a source inventory, not a public-API or support claim.',
    'Scopes and platform hints are mechanical and require review.',
    'Duplicate compatibility re-exports are retained as source evidence.',
  ],
  nativeModules: serialize(modules),
  nativeComponents: serialize(components),
};

for (const name of [
  'ExceptionsManager',
  'NativeMicrotasksCxx',
  'Networking',
  'UIManager',
]) {
  if (!modules.has(name)) {
    throw new Error(`RN NativeModule inventory missed required anchor: ${name}`);
  }
}
for (const name of [
  'AndroidTextInput',
  'RCTImageView',
  'RCTScrollView',
  'RCTText',
  'RCTView',
]) {
  if (!components.has(name)) {
    throw new Error(
      `RN NativeComponent inventory missed required anchor: ${name}`,
    );
  }
}
for (const name of ['ComponentName', '../codegenNativeComponent']) {
  if (components.has(name)) {
    throw new Error(`RN NativeComponent inventory contains test noise: ${name}`);
  }
}

process.stdout.write(`${JSON.stringify(result, null, pretty ? 2 : 0)}\n`);
