#!/usr/bin/env node
// Optional device-vs-simulator screenshot comparator. Core
// configure/build/test/runtime paths still do not require Node.
import {spawnSync} from 'node:child_process';
import {deflateSync, inflateSync} from 'node:zlib';
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

// Pixel 4a: 1080x2340 @ 2.75, RN root 392.7273x753.4545 dp -> 1080x2072 px.
// Chrome (status + nav) is 268 px. Pixel-boundary auditing against glyph and
// non-text View coordinates gives 136 top + 132 bottom for this 3-button
// screenshot; 140/128 left every RN-root child uniformly 4px apart.
const PROFILES = {
  'pixel-4a': {
    label: 'Pixel 4a',
    fullWidth: 1080,
    fullHeight: 2340,
    contentWidth: 1080,
    contentHeight: 2072,
    cropTop: 136,
    cropBottom: 132,
    viewport: {width: 392.7273, height: 753.4545, pointScaleFactor: 2.75},
  },
};

function printHelp() {
  process.stdout.write(`Compare a real-device screenshot with an rnsim Skia PNG.

Usage:
  node tools/diagnostics/compare-screenshots.mjs [options]
  node tools/diagnostics/compare-screenshots.mjs --self-test

This is optional companion tooling. It uses only Node built-ins (no npm
packages, no ImageMagick). It does not claim HWUI / CoreText pixel
equivalence; default thresholds follow the "glance-level" Pixel 4a bar.

Options:
  --device FILE              Real-device PNG (adb screencap or pulled file)
  --simulator FILE           Simulator PNG from rnsim --screenshot
  --out-dir DIR              Output directory (default build/screenshot-compare)
  --pair-dir DIR             Compare matching device-*.png / simulator-*.png
  --only NAME                Substring filter for --pair-dir or --rntester-group

  --rntester-group NAME      Batch RN Tester group: Components, APIs
  --examples KEY,KEY         With --rntester-group, only these module keys
  --list                     Print RN Tester keys for --rntester-group and exit
  --skip-device              Reuse archived / out-dir device PNGs (no adb)
  --skip-simulator           Reuse existing simulator PNGs in the out-dir
  --device-archive DIR       Archived device PNGs (default
                             build/rntester/compare/device-archive/<profile>)
  --recapture-device         Ignore the archive and adb-capture again
  --force-stop               Cold-start RN Tester before each device capture
  --scroll-pages N           Extra scrolled frames per example (default 2)
  --scroll-step N            Logical dp per scroll page (default 360)
  --no-scroll                Do not capture scrolled frames
  --no-interact              Do not capture tap/swipe after-state frames
  --device-package NAME      Android package (default com.facebook.react.uiapp)
  --device-activity NAME     Activity (default .RNTesterActivity)
  --device-settle-ms N       Wait after adb deep-link (default 2500)
  --timeout-ms N             rnsim workload timeout (default 20000)
  --rnsim FILE               rnsim binary
  --config FILE              rnsim.json (default build/rntester/rnsim.json)
  --rntester-bundle FILE     Caller-built RN Tester bundle
  --adapter FILE             Headless adapter (default rntester-example-adapter.js)
  --android-font-dir DIR     Fonts for Skia paint (default build/android-fonts)

  --capture-device           adb exec-out screencap -p into --device / out-dir
  --serial SERIAL            adb -s SERIAL
  --adb PATH                 adb binary (default: adb on PATH)
  --capture-simulator        Run the rnsim command after -- and screenshot it

  --profile pixel-4a         Crop known Pixel 4a status/nav chrome
  --align auto|none|profile  Default auto when sizes differ, none when equal
  --crop top,right,bottom,left
                             Extra CSS-order crop applied to the device image

  --mode glance|strict       glance (default): threshold 24, AA on
                             strict: threshold 8, AA off
  --threshold N              Per-pixel max-channel delta treated as a match
  --aa true|false            Ignore 1px antialiased edge mismatches
  --max-mismatch-percent N   Exit 1 when mismatch exceeds N (unset: report only)

  --device-label TEXT        Side-by-side caption (default device / profile)
  --simulator-label TEXT     Side-by-side caption (default simulator)
  --no-images                Write report.json only
  --json FILE                Report path (default <out-dir>/report.json)
  --quiet                    Do not print a human summary on stderr
  --self-test                Codec + align + diff smoke test
  --help                     Show this help

Outputs (under --out-dir):
  report.json          Machine-readable stats and alignment
  side-by-side.png     Device | simulator, content-aligned
  diff.png             Absolute-difference blend (black = match)
  mismatch.png         Magenta pixels over the threshold
  overlay.png          Simulator with magenta mismatches
  device-content.png   Device image after chrome crop / align

Examples:
  node tools/diagnostics/compare-screenshots.mjs \\
    --device build/rntester/compare/device-home.png \\
    --simulator build/rntester/compare/simulator-home.png \\
    --profile pixel-4a \\
    --out-dir build/rntester/compare/home

  node tools/diagnostics/compare-screenshots.mjs --capture-device \\
    --simulator build/rntester/compare/simulator-view-top.png \\
    --profile pixel-4a \\
    --out-dir build/rntester/compare/view-top

  node tools/diagnostics/compare-screenshots.mjs --pair-dir \\
    build/rntester/compare --out-dir build/rntester/compare/report

  node tools/diagnostics/compare-screenshots.mjs \\
    --rntester-group Components --profile pixel-4a \\
    --out-dir build/rntester/compare/components

  # Top + 2 scroll pages + module-specific taps/swipes
  node tools/diagnostics/compare-screenshots.mjs \\
    --rntester-group Components --profile pixel-4a \\
    --scroll-pages 2
`);
}

function parseBoolean(name, value) {
  if (value === 'true' || value === '1') return true;
  if (value === 'false' || value === '0') return false;
  throw new Error(`${name} must be true or false`);
}

function parseArgs(argv) {
  const passthroughAt = argv.indexOf('--');
  const passthrough = passthroughAt >= 0 ? argv.slice(passthroughAt + 1) : [];
  const args = passthroughAt >= 0 ? argv.slice(0, passthroughAt) : argv.slice();
  const options = {
    device: null,
    simulator: null,
    outDir: path.join(projectRoot, 'build', 'screenshot-compare'),
    pairDir: null,
    only: null,
    captureDevice: false,
    captureSimulator: false,
    serial: null,
    adb: 'adb',
    profile: null,
    align: null,
    crop: null,
    mode: 'glance',
    threshold: null,
    aa: null,
    maxMismatchPercent: null,
    deviceLabel: null,
    simulatorLabel: 'simulator',
    writeImages: true,
    json: null,
    quiet: false,
    selfTest: false,
    rntesterGroup: null,
    examples: null,
    list: false,
    skipDevice: false,
    skipSimulator: false,
    deviceArchive: null,
    recaptureDevice: false,
    forceStop: false,
    devicePackage: 'com.facebook.react.uiapp',
    deviceActivity: '.RNTesterActivity',
    deviceSettleMs: 2500,
    timeoutMs: 20000,
    rnsim: null,
    rntesterConfig: path.join(projectRoot, 'build', 'rntester', 'rnsim.json'),
    rntesterBundle: path.join(
      projectRoot, 'build', 'rntester', 'RNTesterApp.android.jsbundle',
    ),
    rntesterAdapter: path.join(
      projectRoot, 'tests', 'fixtures', 'rntester-example-adapter.js',
    ),
    androidFontDir: path.join(projectRoot, 'build', 'android-fonts'),
    scrollPages: 2,
    scrollStep: 360,
    interact: true,
    noScroll: false,
    noInteract: false,
    passthrough,
  };
  const mappings = {
    '--device': ['device', String],
    '--simulator': ['simulator', String],
    '--out-dir': ['outDir', String],
    '--pair-dir': ['pairDir', String],
    '--only': ['only', String],
    '--rntester-group': ['rntesterGroup', String],
    '--examples': ['examples', String],
    '--serial': ['serial', String],
    '--adb': ['adb', String],
    '--profile': ['profile', String],
    '--align': ['align', String],
    '--crop': ['crop', String],
    '--mode': ['mode', String],
    '--threshold': ['threshold', Number],
    '--aa': ['aa', parseBoolean],
    '--max-mismatch-percent': ['maxMismatchPercent', Number],
    '--device-label': ['deviceLabel', String],
    '--simulator-label': ['simulatorLabel', String],
    '--json': ['json', String],
    '--device-package': ['devicePackage', String],
    '--device-activity': ['deviceActivity', String],
    '--device-settle-ms': ['deviceSettleMs', Number],
    '--timeout-ms': ['timeoutMs', Number],
    '--rnsim': ['rnsim', String],
    '--config': ['rntesterConfig', String],
    '--rntester-bundle': ['rntesterBundle', String],
    '--adapter': ['rntesterAdapter', String],
    '--android-font-dir': ['androidFontDir', String],
    '--device-archive': ['deviceArchive', String],
    '--scroll-pages': ['scrollPages', Number],
    '--scroll-step': ['scrollStep', Number],
  };
  const flags = {
    '--capture-device': 'captureDevice',
    '--capture-simulator': 'captureSimulator',
    '--no-images': 'writeImages',
    '--quiet': 'quiet',
    '--self-test': 'selfTest',
    '--list': 'list',
    '--skip-device': 'skipDevice',
    '--skip-simulator': 'skipSimulator',
    '--recapture-device': 'recaptureDevice',
    '--force-stop': 'forceStop',
    '--no-scroll': 'noScroll',
    '--no-interact': 'noInteract',
  };
  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index];
    if (argument === '--help' || argument === '-h') {
      printHelp();
      process.exit(0);
    }
    if (Object.hasOwn(flags, argument)) {
      options[flags[argument]] = argument !== '--no-images';
      continue;
    }
    const mapping = mappings[argument];
    if (mapping == null || args[index + 1] == null) {
      throw new Error(`Unknown or incomplete option: ${argument}`);
    }
    options[mapping[0]] = mapping[1] === parseBoolean
      ? mapping[1](argument, args[index + 1])
      : mapping[1](args[index + 1]);
    index += 1;
  }
  if (options.mode !== 'glance' && options.mode !== 'strict') {
    throw new Error('--mode must be glance or strict');
  }
  if (options.align != null &&
      options.align !== 'auto' &&
      options.align !== 'none' &&
      options.align !== 'profile') {
    throw new Error('--align must be auto, none, or profile');
  }
  if (options.profile != null && PROFILES[options.profile] == null) {
    throw new Error(
      `--profile must be one of: ${Object.keys(PROFILES).join(', ')}`,
    );
  }
  if (options.threshold == null) {
    options.threshold = options.mode === 'strict' ? 8 : 24;
  }
  if (options.aa == null) {
    options.aa = options.mode !== 'strict';
  }
  if (!Number.isInteger(options.threshold) ||
      options.threshold < 0 || options.threshold > 255) {
    throw new Error('--threshold must be an integer 0..255');
  }
  if (options.maxMismatchPercent != null &&
      !(options.maxMismatchPercent >= 0 && options.maxMismatchPercent <= 100)) {
    throw new Error('--max-mismatch-percent must be between 0 and 100');
  }
  if (options.crop != null) {
    const parts = options.crop.split(',').map(Number);
    if (parts.length !== 4 || parts.some(value => !Number.isInteger(value) ||
        value < 0)) {
      throw new Error('--crop must be top,right,bottom,left non-negative integers');
    }
    options.crop = {top: parts[0], right: parts[1], bottom: parts[2], left: parts[3]};
  }
  if (options.rntesterGroup != null) {
    if (options.rntesterGroup !== 'Components' &&
        options.rntesterGroup !== 'APIs') {
      throw new Error('--rntester-group must be Components or APIs');
    }
    if (options.profile == null) options.profile = 'pixel-4a';
    const defaultOut = path.join(projectRoot, 'build', 'screenshot-compare');
    if (options.outDir === defaultOut) {
      options.outDir = path.join(
        projectRoot,
        'build',
        'rntester',
        'compare',
        options.rntesterGroup.toLowerCase(),
      );
    }
  }
  if (options.examples != null) {
    options.exampleKeys = options.examples.split(',').map(value => value.trim())
      .filter(value => value.length > 0);
    if (options.exampleKeys.length === 0) {
      throw new Error('--examples must list at least one module key');
    }
  } else {
    options.exampleKeys = null;
  }
  if (!Number.isInteger(options.deviceSettleMs) || options.deviceSettleMs < 0) {
    throw new Error('--device-settle-ms must be a non-negative integer');
  }
  if (!Number.isInteger(options.timeoutMs) || options.timeoutMs < 1) {
    throw new Error('--timeout-ms must be a positive integer');
  }
  if (options.noScroll) options.scrollPages = 0;
  if (options.noInteract) options.interact = false;
  if (!Number.isInteger(options.scrollPages) || options.scrollPages < 0) {
    throw new Error('--scroll-pages must be a non-negative integer');
  }
  if (!Number.isInteger(options.scrollStep) || options.scrollStep < 1) {
    throw new Error('--scroll-step must be a positive integer');
  }
  if (options.deviceLabel == null) {
    options.deviceLabel = options.profile
      ? PROFILES[options.profile].label
      : 'device';
  }
  if (options.align == null) {
    options.align = options.profile != null ? 'profile' : 'auto';
  }
  if (options.align === 'profile' && options.profile == null) {
    throw new Error('--align profile requires --profile');
  }
  if (options.deviceArchive == null && options.profile != null) {
    options.deviceArchive = path.join(
      projectRoot,
      'build',
      'rntester',
      'compare',
      'device-archive',
      options.profile,
    );
  }
  if (options.rnsim == null) {
    const requestedBuild = process.env.RNS_BUILD_DIR;
    const candidates = requestedBuild != null
      ? [path.join(path.resolve(requestedBuild), 'runtime', 'rnsim')]
      : [
          path.join(projectRoot, 'build', 'runtime', 'rnsim'),
          path.join(projectRoot, 'build', 'final', 'runtime', 'rnsim'),
        ];
    options.rnsim = candidates.find(candidate => existsSync(candidate)) ??
      candidates[0];
  }
  for (const name of [
    'device', 'simulator', 'outDir', 'pairDir', 'json',
    'rnsim', 'rntesterConfig', 'rntesterBundle', 'rntesterAdapter',
    'androidFontDir', 'deviceArchive',
  ]) {
    if (options[name] != null) {
      options[name] = path.resolve(options[name]);
    }
  }
  return options;
}

const CRC_TABLE = new Uint32Array(256);
for (let index = 0; index < 256; index += 1) {
  let value = index;
  for (let bit = 0; bit < 8; bit += 1) {
    value = (value & 1) !== 0 ? (0xedb88320 ^ (value >>> 1)) : (value >>> 1);
  }
  CRC_TABLE[index] = value >>> 0;
}

function crc32(bytes) {
  let crc = 0xffffffff;
  for (let index = 0; index < bytes.length; index += 1) {
    crc = CRC_TABLE[(crc ^ bytes[index]) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function paeth(left, up, upLeft) {
  const estimate = left + up - upLeft;
  const distanceLeft = Math.abs(estimate - left);
  const distanceUp = Math.abs(estimate - up);
  const distanceUpLeft = Math.abs(estimate - upLeft);
  if (distanceLeft <= distanceUp && distanceLeft <= distanceUpLeft) return left;
  if (distanceUp <= distanceUpLeft) return up;
  return upLeft;
}

function pngChunk(type, data) {
  const typeBytes = Buffer.from(type, 'ascii');
  const payload = Buffer.concat([typeBytes, data]);
  const header = Buffer.alloc(8);
  header.writeUInt32BE(data.length, 0);
  typeBytes.copy(header, 4);
  const footer = Buffer.alloc(4);
  footer.writeUInt32BE(crc32(payload), 0);
  return Buffer.concat([header, data, footer]);
}

function encodePng(image) {
  const {width, height, rgba} = image;
  const stride = width * 4 + 1;
  const raw = Buffer.alloc(stride * height);
  for (let row = 0; row < height; row += 1) {
    const dest = row * stride;
    raw[dest] = 0;
    raw.set(rgba.subarray(row * width * 4, (row + 1) * width * 4), dest + 1);
  }
  const compressed = deflateSync(raw, {level: 6});
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = 6;
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    pngChunk('IHDR', ihdr),
    pngChunk('IDAT', compressed),
    pngChunk('IEND', Buffer.alloc(0)),
  ]);
}

function decodePng(bytes) {
  const buffer = Buffer.isBuffer(bytes) ? bytes : Buffer.from(bytes);
  const signature = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  const origin = buffer.indexOf(signature);
  if (origin < 0) {
    throw new Error('not a PNG (missing signature)');
  }
  let offset = origin + 8;
  let width = 0;
  let height = 0;
  let bitDepth = 0;
  let colorType = 0;
  let interlace = 0;
  let palette = null;
  let transparency = null;
  const idat = [];
  while (offset + 12 <= buffer.length) {
    const length = buffer.readUInt32BE(offset);
    const type = buffer.toString('ascii', offset + 4, offset + 8);
    const data = buffer.subarray(offset + 8, offset + 8 + length);
    if (offset + 12 + length > buffer.length) {
      throw new Error('truncated PNG chunk');
    }
    offset += 12 + length;
    if (type === 'IHDR') {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      bitDepth = data[8];
      colorType = data[9];
      interlace = data[12];
    } else if (type === 'PLTE') {
      palette = data;
    } else if (type === 'tRNS') {
      transparency = data;
    } else if (type === 'IDAT') {
      idat.push(data);
    } else if (type === 'IEND') {
      break;
    }
  }
  if (width < 1 || height < 1 || width > 8192 || height > 8192) {
    throw new Error(`unsupported PNG size ${width}x${height}`);
  }
  if (bitDepth !== 8) {
    throw new Error(`unsupported PNG bit depth ${bitDepth} (need 8)`);
  }
  if (interlace !== 0) {
    throw new Error('interlaced PNG is not supported');
  }
  const channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colorType];
  if (channels == null) {
    throw new Error(`unsupported PNG color type ${colorType}`);
  }
  const inflated = inflateSync(Buffer.concat(idat));
  const bpp = Math.max(1, channels);
  const stride = width * bpp;
  const expected = (stride + 1) * height;
  if (inflated.length < expected) {
    throw new Error(
      `PNG IDAT too small: ${inflated.length} < ${expected}`,
    );
  }
  const rows = new Array(height);
  let previous = Buffer.alloc(stride);
  let cursor = 0;
  for (let row = 0; row < height; row += 1) {
    const filter = inflated[cursor];
    cursor += 1;
    const current = Buffer.from(inflated.subarray(cursor, cursor + stride));
    cursor += stride;
    unfilter(filter, current, previous, bpp);
    rows[row] = current;
    previous = current;
  }
  const rgba = new Uint8Array(width * height * 4);
  for (let row = 0; row < height; row += 1) {
    expandRow(
      rows[row],
      rgba,
      row * width * 4,
      width,
      colorType,
      palette,
      transparency,
    );
  }
  return {width, height, rgba};
}

function unfilter(filter, current, previous, bpp) {
  if (filter === 0) return;
  const length = current.length;
  if (filter === 1) {
    for (let index = 0; index < length; index += 1) {
      const left = index >= bpp ? current[index - bpp] : 0;
      current[index] = (current[index] + left) & 255;
    }
    return;
  }
  if (filter === 2) {
    for (let index = 0; index < length; index += 1) {
      current[index] = (current[index] + previous[index]) & 255;
    }
    return;
  }
  if (filter === 3) {
    for (let index = 0; index < length; index += 1) {
      const left = index >= bpp ? current[index - bpp] : 0;
      current[index] = (current[index] + ((left + previous[index]) >> 1)) & 255;
    }
    return;
  }
  if (filter === 4) {
    for (let index = 0; index < length; index += 1) {
      const left = index >= bpp ? current[index - bpp] : 0;
      const upLeft = index >= bpp ? previous[index - bpp] : 0;
      current[index] =
        (current[index] + paeth(left, previous[index], upLeft)) & 255;
    }
    return;
  }
  throw new Error(`unsupported PNG filter ${filter}`);
}

function expandRow(
  row, rgba, dest, width, colorType, palette, transparency,
) {
  if (colorType === 6) {
    rgba.set(row, dest);
    return;
  }
  for (let x = 0; x < width; x += 1) {
    const out = dest + x * 4;
    if (colorType === 2) {
      rgba[out] = row[x * 3];
      rgba[out + 1] = row[x * 3 + 1];
      rgba[out + 2] = row[x * 3 + 2];
      rgba[out + 3] = 255;
      if (transparency != null && transparency.length >= 6 &&
          row[x * 3] === transparency[1] &&
          row[x * 3 + 1] === transparency[3] &&
          row[x * 3 + 2] === transparency[5]) {
        rgba[out + 3] = 0;
      }
    } else if (colorType === 0) {
      const gray = row[x];
      rgba[out] = gray;
      rgba[out + 1] = gray;
      rgba[out + 2] = gray;
      rgba[out + 3] = 255;
      if (transparency != null && transparency.length >= 2 &&
          gray === transparency[1]) {
        rgba[out + 3] = 0;
      }
    } else if (colorType === 4) {
      rgba[out] = row[x * 2];
      rgba[out + 1] = row[x * 2];
      rgba[out + 2] = row[x * 2];
      rgba[out + 3] = row[x * 2 + 1];
    } else if (colorType === 3) {
      const index = row[x];
      rgba[out] = palette[index * 3];
      rgba[out + 1] = palette[index * 3 + 1];
      rgba[out + 2] = palette[index * 3 + 2];
      rgba[out + 3] = transparency != null && index < transparency.length
        ? transparency[index]
        : 255;
    }
  }
}

function readPngFile(filePath) {
  if (!existsSync(filePath)) {
    throw new Error(`PNG not found: ${filePath}`);
  }
  return decodePng(readFileSync(filePath));
}

function writePngFile(filePath, image) {
  mkdirSync(path.dirname(filePath), {recursive: true});
  writeFileSync(filePath, encodePng(image));
}

function createImage(width, height, fill = [255, 255, 255, 255]) {
  const rgba = new Uint8Array(width * height * 4);
  for (let index = 0; index < rgba.length; index += 4) {
    rgba[index] = fill[0];
    rgba[index + 1] = fill[1];
    rgba[index + 2] = fill[2];
    rgba[index + 3] = fill[3];
  }
  return {width, height, rgba};
}

function cropImage(image, left, top, width, height) {
  if (left < 0 || top < 0 || width < 1 || height < 1 ||
      left + width > image.width || top + height > image.height) {
    throw new Error(
      `crop ${left},${top} ${width}x${height} is outside ${image.width}x${image.height}`,
    );
  }
  const rgba = new Uint8Array(width * height * 4);
  for (let row = 0; row < height; row += 1) {
    const source = ((top + row) * image.width + left) * 4;
    rgba.set(image.rgba.subarray(source, source + width * 4), row * width * 4);
  }
  return {width, height, rgba};
}

function blit(destination, source, left, top) {
  for (let row = 0; row < source.height; row += 1) {
    const destRow = top + row;
    if (destRow < 0 || destRow >= destination.height) continue;
    for (let column = 0; column < source.width; column += 1) {
      const destColumn = left + column;
      if (destColumn < 0 || destColumn >= destination.width) continue;
      const from = (row * source.width + column) * 4;
      const to = (destRow * destination.width + destColumn) * 4;
      destination.rgba[to] = source.rgba[from];
      destination.rgba[to + 1] = source.rgba[from + 1];
      destination.rgba[to + 2] = source.rgba[from + 2];
      destination.rgba[to + 3] = source.rgba[from + 3];
    }
  }
}

function sampleSad(a, b, ax, ay, step) {
  const rowStep = Math.max(1, Math.min(step, Math.ceil(b.height / 16)));
  const columnStep = Math.max(1, Math.min(step, Math.ceil(b.width / 16)));
  let total = 0;
  let count = 0;
  for (let row = 0; row < b.height; row += rowStep) {
    const aRow = ay + row;
    for (let column = 0; column < b.width; column += columnStep) {
      const aIndex = (aRow * a.width + ax + column) * 4;
      const bIndex = (row * b.width + column) * 4;
      total += Math.abs(a.rgba[aIndex] - b.rgba[bIndex]) +
        Math.abs(a.rgba[aIndex + 1] - b.rgba[bIndex + 1]) +
        Math.abs(a.rgba[aIndex + 2] - b.rgba[bIndex + 2]);
      count += 1;
    }
  }
  return total / count;
}

function alignImages(device, simulator, options) {
  const explicit = options.crop;
  let working = device;
  let crop = {top: 0, right: 0, bottom: 0, left: 0};
  if (explicit != null) {
    const width = device.width - explicit.left - explicit.right;
    const height = device.height - explicit.top - explicit.bottom;
    working = cropImage(device, explicit.left, explicit.top, width, height);
    crop = {...explicit};
  }
  if (working.width === simulator.width &&
      working.height === simulator.height) {
    return {device: working, simulator, crop, method: 'none'};
  }
  const profile = options.profile != null ? PROFILES[options.profile] : null;
  if (options.align === 'profile' ||
      (options.align === 'auto' && profile != null &&
        working.width === profile.fullWidth &&
        working.height === profile.fullHeight &&
        simulator.width === profile.contentWidth &&
        simulator.height === profile.contentHeight)) {
    if (profile == null) {
      throw new Error('profile alignment requires --profile');
    }
    if (working.width !== profile.fullWidth ||
        working.height !== profile.fullHeight) {
      throw new Error(
        `${options.profile} expected ${profile.fullWidth}x${profile.fullHeight}, ` +
          `got device ${working.width}x${working.height}`,
      );
    }
    const aligned = cropImage(
      working,
      0,
      profile.cropTop,
      profile.contentWidth,
      profile.contentHeight,
    );
    crop = {
      top: crop.top + profile.cropTop,
      right: crop.right,
      bottom: crop.bottom + profile.cropBottom,
      left: crop.left,
    };
    return {device: aligned, simulator, crop, method: 'profile'};
  }
  if (options.align === 'none') {
    throw new Error(
      `image sizes differ (device ${working.width}x${working.height}, ` +
        `simulator ${simulator.width}x${simulator.height}); use --align auto ` +
        'or --profile pixel-4a',
    );
  }
  if (working.width < simulator.width || working.height < simulator.height) {
    throw new Error(
      `device ${working.width}x${working.height} is smaller than simulator ` +
        `${simulator.width}x${simulator.height}; cannot auto-crop`,
    );
  }
  const maxX = working.width - simulator.width;
  const maxY = working.height - simulator.height;
  let best = {score: Infinity, x: 0, y: 0};
  const xStep = maxX === 0 ? 1 : 4;
  const yStep = maxY === 0 ? 1 : 2;
  for (let y = 0; y <= maxY; y += yStep) {
    for (let x = 0; x <= maxX; x += xStep) {
      const score = sampleSad(working, simulator, x, y, 8);
      if (score < best.score) best = {score, x, y};
    }
  }
  const refineX0 = Math.max(0, best.x - xStep);
  const refineY0 = Math.max(0, best.y - yStep);
  const refineX1 = Math.min(maxX, best.x + xStep);
  const refineY1 = Math.min(maxY, best.y + yStep);
  for (let y = refineY0; y <= refineY1; y += 1) {
    for (let x = refineX0; x <= refineX1; x += 1) {
      const score = sampleSad(working, simulator, x, y, 4);
      if (score < best.score) best = {score, x, y};
    }
  }
  const aligned = cropImage(
    working, best.x, best.y, simulator.width, simulator.height,
  );
  crop = {
    top: crop.top + best.y,
    right: crop.right + (working.width - best.x - simulator.width),
    bottom: crop.bottom + (working.height - best.y - simulator.height),
    left: crop.left + best.x,
  };
  return {device: aligned, simulator, crop, method: 'auto', score: best.score};
}

function neighborWithinThreshold(image, x, y, sourceRgba, sourceIndex, threshold) {
  for (let row = y - 1; row <= y + 1; row += 1) {
    if (row < 0 || row >= image.height) continue;
    for (let column = x - 1; column <= x + 1; column += 1) {
      if (column < 0 || column >= image.width) continue;
      if (row === y && column === x) continue;
      const other = (row * image.width + column) * 4;
      const delta = Math.max(
        Math.abs(sourceRgba[sourceIndex] - image.rgba[other]),
        Math.abs(sourceRgba[sourceIndex + 1] - image.rgba[other + 1]),
        Math.abs(sourceRgba[sourceIndex + 2] - image.rgba[other + 2]),
      );
      if (delta <= threshold) return true;
    }
  }
  return false;
}

function isAntialiased(device, simulator, x, y, index, threshold) {
  return neighborWithinThreshold(
    simulator, x, y, device.rgba, index, threshold,
  ) || neighborWithinThreshold(
    device, x, y, simulator.rgba, index, threshold,
  );
}

function compareImages(device, simulator, options) {
  if (device.width !== simulator.width || device.height !== simulator.height) {
    throw new Error(
      `aligned sizes still differ: ${device.width}x${device.height} vs ` +
        `${simulator.width}x${simulator.height}`,
    );
  }
  const {width, height} = device;
  const pixels = width * height;
  const histogram = new Uint32Array(256);
  let mismatch = 0;
  let ignoredAa = 0;
  let sum = 0;
  let maxDelta = 0;
  const diff = createImage(width, height, [0, 0, 0, 255]);
  const mismatchImage = createImage(width, height, [0, 0, 0, 255]);
  const overlay = {width, height, rgba: Uint8Array.from(simulator.rgba)};
  const tile = 32;
  const tilesX = Math.ceil(width / tile);
  const tilesY = Math.ceil(height / tile);
  const tileHits = new Uint32Array(tilesX * tilesY);
  const tileCounts = new Uint32Array(tilesX * tilesY);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const index = (y * width + x) * 4;
      const dr = Math.abs(device.rgba[index] - simulator.rgba[index]);
      const dg = Math.abs(device.rgba[index + 1] - simulator.rgba[index + 1]);
      const db = Math.abs(device.rgba[index + 2] - simulator.rgba[index + 2]);
      const delta = Math.max(dr, dg, db);
      histogram[delta] += 1;
      sum += (dr + dg + db) / 3;
      if (delta > maxDelta) maxDelta = delta;
      diff.rgba[index] = dr;
      diff.rgba[index + 1] = dg;
      diff.rgba[index + 2] = db;
      const tileIndex = Math.floor(y / tile) * tilesX + Math.floor(x / tile);
      tileCounts[tileIndex] += 1;
      if (delta <= options.threshold) continue;
      if (options.aa && isAntialiased(device, simulator, x, y, index, options.threshold)) {
        ignoredAa += 1;
        continue;
      }
      mismatch += 1;
      tileHits[tileIndex] += 1;
      mismatchImage.rgba[index] = 255;
      mismatchImage.rgba[index + 1] = 0;
      mismatchImage.rgba[index + 2] = 220;
      overlay.rgba[index] = 255;
      overlay.rgba[index + 1] = 0;
      overlay.rgba[index + 2] = 220;
    }
  }
  let remaining = Math.floor(pixels * 0.95);
  let p95 = 255;
  for (let value = 0; value < 256; value += 1) {
    remaining -= histogram[value];
    if (remaining <= 0) {
      p95 = value;
      break;
    }
  }
  const hotspots = [];
  for (let tileY = 0; tileY < tilesY; tileY += 1) {
    for (let tileX = 0; tileX < tilesX; tileX += 1) {
      const tileIndex = tileY * tilesX + tileX;
      if (tileHits[tileIndex] === 0) continue;
      hotspots.push({
        x: tileX * tile,
        y: tileY * tile,
        width: Math.min(tile, width - tileX * tile),
        height: Math.min(tile, height - tileY * tile),
        mismatchPercent: (100 * tileHits[tileIndex]) / tileCounts[tileIndex],
      });
    }
  }
  hotspots.sort((left, right) => right.mismatchPercent - left.mismatchPercent);
  return {
    width,
    height,
    pixels,
    mismatchPixels: mismatch,
    mismatchPercent: (100 * mismatch) / pixels,
    ignoredAaPixels: ignoredAa,
    meanChannelDelta: sum / pixels,
    maxChannelDelta: maxDelta,
    p95ChannelDelta: p95,
    hotspots: hotspots.slice(0, 12),
    diff,
    mismatchImage,
    overlay,
  };
}

const FONT_5X7 = {
  ' ': [0, 0, 0, 0, 0, 0, 0],
  '%': [0x19, 0x1a, 0x04, 0x04, 0x0b, 0x13, 0],
  '(': [0x04, 0x08, 0x10, 0x10, 0x10, 0x08, 0x04],
  ')': [0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08],
  '+': [0, 0x04, 0x04, 0x1f, 0x04, 0x04, 0],
  '-': [0, 0, 0, 0x1f, 0, 0, 0],
  '.': [0, 0, 0, 0, 0, 0, 0x04],
  '/': [0x01, 0x02, 0x04, 0x04, 0x08, 0x10, 0],
  '0': [0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e],
  '1': [0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e],
  '2': [0x0e, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1f],
  '3': [0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e],
  '4': [0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02],
  '5': [0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e],
  '6': [0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e],
  '7': [0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08],
  '8': [0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e],
  '9': [0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c],
  ':': [0, 0x04, 0, 0, 0, 0x04, 0],
  'A': [0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11],
  'B': [0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e],
  'C': [0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e],
  'D': [0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e],
  'E': [0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f],
  'F': [0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10],
  'G': [0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e],
  'H': [0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11],
  'I': [0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e],
  'J': [0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e],
  'K': [0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11],
  'L': [0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f],
  'M': [0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11],
  'N': [0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11],
  'O': [0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e],
  'P': [0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10],
  'Q': [0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d],
  'R': [0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11],
  'S': [0x0e, 0x11, 0x10, 0x0e, 0x01, 0x11, 0x0e],
  'T': [0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04],
  'U': [0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e],
  'V': [0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04],
  'W': [0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11],
  'X': [0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11],
  'Y': [0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04],
  'Z': [0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f],
  a: [0, 0, 0x0e, 0x01, 0x0f, 0x11, 0x0f],
  b: [0x10, 0x10, 0x1e, 0x11, 0x11, 0x11, 0x1e],
  c: [0, 0, 0x0e, 0x11, 0x10, 0x11, 0x0e],
  d: [0x01, 0x01, 0x0f, 0x11, 0x11, 0x11, 0x0f],
  e: [0, 0, 0x0e, 0x11, 0x1f, 0x10, 0x0e],
  f: [0x06, 0x08, 0x1c, 0x08, 0x08, 0x08, 0x08],
  g: [0, 0, 0x0f, 0x11, 0x0f, 0x01, 0x0e],
  h: [0x10, 0x10, 0x1e, 0x11, 0x11, 0x11, 0x11],
  i: [0x04, 0, 0x0c, 0x04, 0x04, 0x04, 0x0e],
  j: [0x02, 0, 0x06, 0x02, 0x02, 0x12, 0x0c],
  k: [0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12],
  l: [0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e],
  m: [0, 0, 0x1a, 0x15, 0x15, 0x15, 0x15],
  n: [0, 0, 0x1e, 0x11, 0x11, 0x11, 0x11],
  o: [0, 0, 0x0e, 0x11, 0x11, 0x11, 0x0e],
  p: [0, 0, 0x1e, 0x11, 0x1e, 0x10, 0x10],
  q: [0, 0, 0x0f, 0x11, 0x0f, 0x01, 0x01],
  r: [0, 0, 0x16, 0x19, 0x10, 0x10, 0x10],
  s: [0, 0, 0x0f, 0x10, 0x0e, 0x01, 0x1e],
  t: [0x08, 0x08, 0x1c, 0x08, 0x08, 0x08, 0x06],
  u: [0, 0, 0x11, 0x11, 0x11, 0x13, 0x0d],
  v: [0, 0, 0x11, 0x11, 0x11, 0x0a, 0x04],
  w: [0, 0, 0x11, 0x15, 0x15, 0x15, 0x0a],
  x: [0, 0, 0x11, 0x0a, 0x04, 0x0a, 0x11],
  y: [0, 0, 0x11, 0x11, 0x0f, 0x01, 0x0e],
  z: [0, 0, 0x1f, 0x02, 0x04, 0x08, 0x1f],
  _: [0, 0, 0, 0, 0, 0, 0x1f],
};

function drawText(image, text, left, top, color, scale = 2) {
  let x = left;
  for (const character of text) {
    const glyph = FONT_5X7[character] ?? FONT_5X7[' '];
    for (let row = 0; row < 7; row += 1) {
      const bits = glyph[row];
      for (let column = 0; column < 5; column += 1) {
        if (((bits >> (4 - column)) & 1) === 0) continue;
        for (let dy = 0; dy < scale; dy += 1) {
          for (let dx = 0; dx < scale; dx += 1) {
            const px = x + column * scale + dx;
            const py = top + row * scale + dy;
            if (px < 0 || py < 0 || px >= image.width || py >= image.height) {
              continue;
            }
            const index = (py * image.width + px) * 4;
            image.rgba[index] = color[0];
            image.rgba[index + 1] = color[1];
            image.rgba[index + 2] = color[2];
            image.rgba[index + 3] = 255;
          }
        }
      }
    }
    x += 6 * scale;
  }
}

function textWidth(text, scale = 2) {
  return text.length * 6 * scale;
}

function labeled(image, label) {
  const header = 56;
  const canvas = createImage(image.width, image.height + header, [255, 255, 255, 255]);
  const width = textWidth(label);
  drawText(
    canvas,
    label,
    Math.max(8, Math.floor((image.width - width) / 2)),
    20,
    [120, 120, 128],
  );
  blit(canvas, image, 0, header);
  return canvas;
}

function composeSideBySide(device, simulator, deviceLabel, simulatorLabel) {
  const gap = 12;
  const header = 56;
  const width = device.width + gap + simulator.width;
  const height = header + Math.max(device.height, simulator.height);
  const canvas = createImage(width, height, [255, 255, 255, 255]);
  for (let row = 0; row < height; row += 1) {
    for (let column = device.width; column < device.width + gap; column += 1) {
      const index = (row * width + column) * 4;
      canvas.rgba[index] = 228;
      canvas.rgba[index + 1] = 228;
      canvas.rgba[index + 2] = 230;
    }
  }
  drawText(
    canvas,
    deviceLabel,
    Math.max(8, Math.floor((device.width - textWidth(deviceLabel)) / 2)),
    20,
    [120, 120, 128],
  );
  drawText(
    canvas,
    simulatorLabel,
    device.width + gap +
      Math.max(8, Math.floor((simulator.width - textWidth(simulatorLabel)) / 2)),
    20,
    [120, 120, 128],
  );
  blit(canvas, device, 0, header);
  blit(canvas, simulator, device.width + gap, header);
  return canvas;
}

function captureDevicePng(options, destination) {
  const args = [];
  if (options.serial) args.push('-s', options.serial);
  args.push('exec-out', 'screencap', '-p');
  const result = spawnSync(options.adb, args, {
    encoding: 'buffer',
    maxBuffer: 64 * 1024 * 1024,
  });
  if (result.error) {
    throw new Error(`failed to spawn adb: ${result.error.message}`);
  }
  if (result.status !== 0) {
    const stderr = result.stderr?.toString() ?? '';
    throw new Error(
      `adb screencap failed (status ${result.status}): ${stderr.trim()}`,
    );
  }
  const signature = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  const origin = result.stdout.indexOf(signature);
  if (origin < 0) {
    throw new Error('adb screencap did not return a PNG');
  }
  mkdirSync(path.dirname(destination), {recursive: true});
  writeFileSync(destination, result.stdout.subarray(origin));
}

function captureSimulatorPng(options, destination) {
  if (options.passthrough.length === 0) {
    throw new Error('--capture-simulator requires an rnsim command after --');
  }
  const command = [...options.passthrough];
  const screenshotFlag = command.indexOf('--screenshot');
  if (screenshotFlag >= 0 && command[screenshotFlag + 1] != null) {
    command[screenshotFlag + 1] = destination;
  } else {
    command.push('--screenshot', destination);
  }
  mkdirSync(path.dirname(destination), {recursive: true});
  const binary = command[0];
  const args = command.slice(1);
  const result = spawnSync(binary, args, {
    cwd: projectRoot,
    stdio: 'inherit',
    env: process.env,
  });
  if (result.error) {
    throw new Error(`failed to spawn rnsim: ${result.error.message}`);
  }
  if (result.status !== 0) {
    throw new Error(`rnsim exited with status ${result.status}`);
  }
  if (!existsSync(destination)) {
    throw new Error(`rnsim did not write ${destination}`);
  }
}

function discoverPairs(directory, only) {
  const names = readdirSync(directory).filter(name => name.endsWith('.png'));
  const pairs = [];
  for (const deviceName of names) {
    if (!deviceName.startsWith('device-')) continue;
    const rest = deviceName.slice('device-'.length);
    const match = names.includes(`simulator-${rest}`)
      ? `simulator-${rest}`
      : (names.includes(`sim-${rest}`) ? `sim-${rest}` : null);
    if (match == null) continue;
    const name = rest.replace(/\.png$/, '');
    if (only != null && !name.includes(only)) continue;
    pairs.push({
      name,
      device: path.join(directory, deviceName),
      simulator: path.join(directory, match),
    });
  }
  pairs.sort((left, right) => left.name.localeCompare(right.name));
  return pairs;
}

function relative(filePath) {
  const rel = path.relative(projectRoot, filePath);
  if (rel.startsWith('..')) return filePath;
  return rel || filePath;
}

function comparePair(options, devicePath, simulatorPath, outDir) {
  mkdirSync(outDir, {recursive: true});
  const deviceSource = readPngFile(devicePath);
  const simulatorSource = readPngFile(simulatorPath);
  const aligned = alignImages(deviceSource, simulatorSource, options);
  const stats = compareImages(aligned.device, aligned.simulator, options);
  const passed = options.maxMismatchPercent == null
    ? null
    : stats.mismatchPercent <= options.maxMismatchPercent;
  const outputs = {};
  if (options.writeImages) {
    outputs.deviceContent = path.join(outDir, 'device-content.png');
    outputs.sideBySide = path.join(outDir, 'side-by-side.png');
    outputs.diff = path.join(outDir, 'diff.png');
    outputs.mismatch = path.join(outDir, 'mismatch.png');
    outputs.overlay = path.join(outDir, 'overlay.png');
    writePngFile(outputs.deviceContent, aligned.device);
    outputs.deviceLabeled = path.join(outDir, 'device-labeled.png');
    outputs.simulatorLabeled = path.join(outDir, 'simulator-labeled.png');
    writePngFile(outputs.deviceLabeled, labeled(aligned.device, options.deviceLabel));
    writePngFile(
      outputs.simulatorLabeled,
      labeled(aligned.simulator, options.simulatorLabel),
    );
    writePngFile(
      outputs.sideBySide,
      composeSideBySide(
        aligned.device,
        aligned.simulator,
        options.deviceLabel,
        options.simulatorLabel,
      ),
    );
    writePngFile(outputs.diff, stats.diff);
    writePngFile(outputs.mismatch, stats.mismatchImage);
    writePngFile(outputs.overlay, stats.overlay);
  }
  const report = {
    schemaVersion: 1,
    generatedAt: new Date().toISOString(),
    mode: options.mode,
    threshold: options.threshold,
    aa: options.aa,
    maxMismatchPercent: options.maxMismatchPercent,
    device: {
      path: relative(devicePath),
      width: deviceSource.width,
      height: deviceSource.height,
    },
    simulator: {
      path: relative(simulatorPath),
      width: simulatorSource.width,
      height: simulatorSource.height,
    },
    alignment: {
      method: aligned.method,
      crop: aligned.crop,
      score: aligned.score ?? null,
      profile: options.profile,
    },
    stats: {
      width: stats.width,
      height: stats.height,
      pixels: stats.pixels,
      mismatchPixels: stats.mismatchPixels,
      mismatchPercent: Number(stats.mismatchPercent.toFixed(4)),
      ignoredAaPixels: stats.ignoredAaPixels,
      meanChannelDelta: Number(stats.meanChannelDelta.toFixed(4)),
      maxChannelDelta: stats.maxChannelDelta,
      p95ChannelDelta: stats.p95ChannelDelta,
      hotspots: stats.hotspots.map(spot => ({
        ...spot,
        mismatchPercent: Number(spot.mismatchPercent.toFixed(2)),
      })),
    },
    outputs: Object.fromEntries(
      Object.entries(outputs).map(([key, value]) => [key, relative(value)]),
    ),
    passed,
    note: 'Glance-level comparison, not HWUI / CoreText pixel equivalence.',
  };
  const reportPath = options.json ?? path.join(outDir, 'report.json');
  mkdirSync(path.dirname(reportPath), {recursive: true});
  writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
  report.report = relative(reportPath);
  return report;
}

function formatSummary(report) {
  const verdict = report.passed == null
    ? 'report'
    : (report.passed ? 'PASS' : 'FAIL');
  return `${verdict}  mismatch=${report.stats.mismatchPercent.toFixed(3)}%  ` +
    `meanΔ=${report.stats.meanChannelDelta.toFixed(2)}  ` +
    `p95Δ=${report.stats.p95ChannelDelta}  ` +
    `maxΔ=${report.stats.maxChannelDelta}  ` +
    `align=${report.alignment.method} ` +
    `${report.stats.width}x${report.stats.height}`;
}

function assert(condition, message) {
  if (!condition) throw new Error(`self-test failed: ${message}`);
}

function runSelfTest() {
  const red = createImage(4, 2, [200, 10, 10, 255]);
  red.rgba[4] = 11;
  red.rgba[5] = 22;
  red.rgba[6] = 33;
  const decoded = decodePng(encodePng(red));
  assert(decoded.width === 4 && decoded.height === 2, 'roundtrip size');
  assert(decoded.rgba[0] === 200 && decoded.rgba[4] === 11, 'roundtrip pixels');

  const a = createImage(8, 8, [240, 240, 240, 255]);
  const b = createImage(8, 8, [240, 240, 240, 255]);
  const identical = compareImages(a, b, {threshold: 8, aa: false});
  assert(identical.mismatchPixels === 0, 'identical images match');

  b.rgba[0] = 0;
  b.rgba[1] = 0;
  b.rgba[2] = 0;
  const different = compareImages(a, b, {threshold: 8, aa: false});
  assert(different.mismatchPixels === 1, 'single-pixel mismatch');
  const tolerated = compareImages(a, b, {threshold: 255, aa: false});
  assert(tolerated.mismatchPixels === 0, 'threshold swallows mismatch');

  const tall = createImage(6, 12, [0, 0, 0, 255]);
  const content = createImage(6, 8, [180, 40, 40, 255]);
  for (let x = 0; x < 6; x += 1) {
    content.rgba[x * 4] = 20;
    content.rgba[x * 4 + 1] = 200;
    content.rgba[x * 4 + 2] = 20;
  }
  blit(tall, content, 0, 3);
  const aligned = alignImages(tall, content, {align: 'auto', crop: null, profile: null});
  assert(aligned.method === 'auto', 'auto align method');
  assert(
    aligned.crop.top === 3 && aligned.crop.bottom === 1,
    `auto crop ${JSON.stringify(aligned.crop)}`,
  );

  const profileDevice = createImage(1080, 2340, [10, 10, 10, 255]);
  const profileSim = createImage(1080, 2072, [200, 200, 200, 255]);
  blit(profileDevice, profileSim, 0, 136);
  const profileAligned = alignImages(profileDevice, profileSim, {
    align: 'profile',
    profile: 'pixel-4a',
    crop: null,
  });
  assert(profileAligned.method === 'profile', 'profile align');
  assert(profileAligned.device.height === 2072, 'profile crop height');
  assert(isActionPrefix([], scroll(10)), 'empty action prefix');
  assert(
    isActionPrefix(scroll(10), [...scroll(10), ...scroll(10)]),
    'scroll action prefix',
  );
  assert(!isActionPrefix(scroll(10), tap(1, 2)), 'unrelated actions');
  const buttonShots = shotsForModule('ButtonExample', {
    scrollPages: 2, scrollStep: 420, interact: true,
  });
  assert(buttonShots[0].id === 'top', 'first shot is top');
  assert(buttonShots.some(shot => shot.kind === 'interact'), 'button interact shot');
  assert(buttonShots.some(shot => shot.id === 'scroll-2'), 'two scroll pages');
  assert(
    buttonShots.filter(shot => shot.kind === 'scroll').every(shot => shot.remount),
    'button scroll remounts after tap',
  );
  const listShots = shotsForModule('FlatListExampleIndex', {
    scrollPages: 0, scrollStep: 360, interact: true,
  });
  assert(
    listShots.some(shot => shot.deepLink === 'FlatListExampleIndex/basic'),
    'FlatList inner deep link',
  );
  const refreshShots = shotsForModule('RefreshControlExample', {
    scrollPages: 2, scrollStep: 360, interact: true,
  });
  assert(
    refreshShots.some(shot => shot.kind === 'interact' && shot.id === 'pull'),
    'refresh interact shot',
  );
  assert(
    refreshShots.filter(shot => shot.kind === 'scroll').every(shot => shot.remount),
    'scroll shots remount after interact',
  );
  assert(!refreshShots[0].remount, 'top shot does not remount');
  const refreshScroll1 = refreshShots.find(shot => shot.id === 'scroll-1');
  const refreshScroll2 = refreshShots.find(shot => shot.id === 'scroll-2');
  assert(
    refreshScroll1 && refreshScroll1.actions.every(action => action.type === 'scroll'),
    'refresh scroll-1 is a cold scroll, not pull+scroll',
  );
  assert(
    refreshScroll2 && refreshScroll2.remount &&
      refreshScroll2.actions.length === 2 &&
      refreshScroll2.actions.every(action => action.type === 'scroll'),
    'refresh scroll-2 remounts with only that shot\'s scroll pages',
  );
  assert(
    shouldReopenDevice(
      {remount: true, actions: scroll(10)},
      'RefreshControlExample',
      scroll(10),
      'RefreshControlExample',
    ),
    'remount reopens even when previousUrl matches',
  );
  assert(
    !shouldReopenDevice(
      {remount: false, actions: [...scroll(10), ...scroll(10)]},
      'RefreshControlExample',
      scroll(10),
      'RefreshControlExample',
    ),
    'incremental scroll does not reopen without remount',
  );
  const dragScript = buildMotionDragScript(100, 200, 100, 800);
  assert(dragScript.includes('motionevent DOWN'), 'drag starts DOWN');
  assert(dragScript.includes('motionevent MOVE'), 'drag moves');
  assert(dragScript.trim().endsWith('motionevent UP 100 800'), 'drag ends UP');
  assert(!/\bswipe\b/.test(dragScript), 'drag is motionevent, not swipe/fling');
  assert(/sleep /.test(dragScript), 'drag dwells so velocity is ~0');
  const nestedScroll = shotsForModule('ScrollViewSimpleExample', {
    scrollPages: 1, scrollStep: 360, interact: false,
  });
  const nestedAction = nestedScroll.find(shot => shot.id === 'scroll-1')?.actions[0];
  assert(nestedAction && nestedAction.y >= 480, 'nested scroll lands on inner SV');
  assert(nestedAction && nestedAction.deltaX > 0 && nestedAction.deltaY === 0,
    'nested scroll is horizontal on the inner SV');
  const drawerTap = MODULE_INTERACTIONS.DrawerLayoutAndroid[0].actions
    .find(action => action.type === 'pointerDown');
  assert(drawerTap && drawerTap.x === 196 && drawerTap.y === 450,
    `drawer Open tap ${drawerTap?.x},${drawerTap?.y}`);
  const ok = parseDialogButtonCenter(
    'resource-id="android:id/button1" bounds="[800,1268][976,1417]"',
  );
  assert(ok && ok.x === 888 && ok.y === 1343, `alert OK bounds ${JSON.stringify(ok)}`);
  process.stdout.write(`${JSON.stringify({selfTest: 'passed'})}\n`);
}

const DEEP_LINK_KEY = /^[a-zA-Z0-9_-]+$/;

function listRntesterGroup(group) {
  const file = path.join(
    projectRoot,
    'third_party',
    'react-native',
    'packages',
    'rn-tester',
    'js',
    'utils',
    'RNTesterList.android.js',
  );
  const source = readFileSync(file, 'utf8');
  const startToken = group === 'APIs'
    ? 'const APIs: Array<RNTesterModuleInfo>'
    : 'const Components: Array<RNTesterModuleInfo>';
  const start = source.indexOf(startToken);
  if (start < 0) {
    throw new Error(`cannot find ${group} in RNTesterList.android.js`);
  }
  const endToken = group === 'Components' ? 'const APIs' : 'const Playgrounds';
  const end = source.indexOf(endToken, start + startToken.length);
  const body = source.slice(start, end < 0 ? source.length : end);
  const keys = [];
  const pattern = /key:\s*'([^']+)'/g;
  let match;
  while ((match = pattern.exec(body))) {
    keys.push(match[1]);
  }
  if (keys.length === 0) {
    throw new Error(`no module keys found in RN Tester ${group}`);
  }
  return keys;
}

function sleepMs(ms) {
  if (ms <= 0) return;
  spawnSync('sleep', [String(ms / 1000)]);
}

function resolveAdbSerial(options) {
  if (options.serial) return options.serial;
  const result = spawnSync(options.adb, ['devices'], {encoding: 'utf8'});
  if (result.error) {
    throw new Error(`failed to spawn adb: ${result.error.message}`);
  }
  const devices = (result.stdout ?? '')
    .split('\n')
    .slice(1)
    .map(line => line.trim())
    .filter(line => line.endsWith('\tdevice') || /\tdevice\b/.test(line))
    .map(line => line.split(/[\s\t]/)[0])
    .filter(Boolean);
  if (devices.length === 0) {
    throw new Error('no adb device attached; connect Pixel 4a or pass --serial');
  }
  if (devices.length > 1) {
    throw new Error(
      `multiple adb devices (${devices.join(', ')}); pass --serial`,
    );
  }
  return devices[0];
}

function adb(options, serial, args, extra = {}) {
  return spawnSync(options.adb, ['-s', serial, ...args], {
    encoding: extra.encoding ?? 'utf8',
    maxBuffer: extra.maxBuffer,
  });
}

function wakeDevice(options, serial) {
  adb(options, serial, ['shell', 'input', 'keyevent', 'KEYCODE_WAKEUP']);
  adb(options, serial, ['shell', 'wm', 'dismiss-keyguard']);
}

function isMostlyBlank(image) {
  let sum = 0;
  let sumSquares = 0;
  let count = 0;
  const top = Math.min(200, Math.floor(image.height * 0.1));
  const bottom = Math.max(top + 1, image.height - 160);
  for (let row = top; row < bottom; row += 8) {
    for (let column = 0; column < image.width; column += 8) {
      const index = (row * image.width + column) * 4;
      const luma = 0.299 * image.rgba[index] +
        0.587 * image.rgba[index + 1] +
        0.114 * image.rgba[index + 2];
      sum += luma;
      sumSquares += luma * luma;
      count += 1;
    }
  }
  if (count === 0) return true;
  const mean = sum / count;
  const variance = sumSquares / count - mean * mean;
  return mean > 228 && variance < 90;
}

function exampleUrl(keyOrPath) {
  return `rntester://example/${keyOrPath}`;
}

function packagePids(options, serial, packageName) {
  const result = adb(options, serial, ['shell', 'pidof', packageName]);
  const text = `${result.stdout ?? ''}`.trim();
  if (text.length === 0) return [];
  return text.split(/\s+/).filter(pid => /^\d+$/.test(pid));
}

function waitForPackagePids(options, serial, packageName, running, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (true) {
    const pids = packagePids(options, serial, packageName);
    if ((pids.length > 0) === running) return pids;
    if (Date.now() >= deadline) return pids;
    sleepMs(150);
  }
}

// RN Tester is singleTask. A VIEW intent to a live process is onNewIntent +
// Linking, which keeps the mounted example (RefreshControl stays on
// "Loaded row"). BACK + bounce through another module is the same no-op.
function forceStopDevicePackage(options, serial) {
  const pkg = options.devicePackage;
  for (let attempt = 0; attempt < 5; attempt += 1) {
    adb(options, serial, ['shell', 'am', 'force-stop', pkg]);
    const pids = waitForPackagePids(options, serial, pkg, false, 800);
    if (pids.length === 0) {
      sleepMs(200);
      if (packagePids(options, serial, pkg).length === 0) return;
    }
  }
}

function startDeviceExample(options, serial, key, remount = false) {
  const url = exampleUrl(key);
  const component = `${options.devicePackage}/${options.deviceActivity}`;
  // -S force-stops again immediately before VIEW so a dying singleTask
  // process cannot swallow the intent via onNewIntent.
  const extra = remount ? ['-S', '--activity-clear-task'] : [];
  return adb(options, serial, [
    'shell', 'am', 'start', '-W',
    ...extra,
    '-a', 'android.intent.action.VIEW',
    '-d', url,
    '-n', component,
  ]);
}

function openDeviceExample(options, serial, key, remount = false) {
  dismissDeviceDialogs(options, serial);
  const killActivity = options.forceStop || remount;
  if (killActivity) {
    forceStopDevicePackage(options, serial);
  } else {
    // Leave the current example so the next VIEW opens at scroll offset 0.
    adb(options, serial, ['shell', 'input', 'keyevent', 'KEYCODE_BACK']);
    sleepMs(350);
  }
  const result = startDeviceExample(options, serial, key, remount);
  if (result.status !== 0) {
    throw new Error(
      `adb am start failed for ${key}: ${(result.stderr || result.stdout || '').trim()}`,
    );
  }
  if (killActivity) {
    waitForPackagePids(options, serial, options.devicePackage, true, 4000);
  }
  // Remount polls for a non-blank frame instead of sleeping the full
  // --device-settle-ms. RefreshControlExample.componentDidMount auto-prepends
  // "Loaded row" after 5s; scroll actions must run on Initial rows before then.
  if (remount) {
    sleepMs(Math.min(400, options.deviceSettleMs));
  } else {
    sleepMs(options.deviceSettleMs);
  }
}

function prepareDeviceForCapture(options, serial) {
  // Hide the IME without BACK, which would pop the example.
  adb(options, serial, ['shell', 'cmd', 'input_method', 'hide']);
  sleepMs(200);
}

function parseDialogButtonCenter(dump) {
  const patterns = [
    /resource-id="android:id\/button1"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"/,
    /text="OK"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"/,
    /bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"[^>]*text="OK"/,
    /text="Close"[^>]*bounds="\[(\d+),(\d+)\]\[(\d+),(\d+)\]"/,
  ];
  for (const pattern of patterns) {
    const match = dump.match(pattern);
    if (!match) continue;
    return {
      x: Math.round((Number(match[1]) + Number(match[3])) / 2),
      y: Math.round((Number(match[2]) + Number(match[4])) / 2),
    };
  }
  return null;
}

// RN Alert.alert is a system Dialog; BACK often does not dismiss it.
function dismissDeviceDialogs(options, serial) {
  const dump = adb(options, serial, ['exec-out', 'uiautomator', 'dump', '/dev/tty']);
  const xml = `${dump.stdout ?? ''}${dump.stderr ?? ''}`;
  const center = parseDialogButtonCenter(xml);
  if (center) {
    adb(options, serial, [
      'shell', 'input', 'tap', String(center.x), String(center.y),
    ]);
    sleepMs(300);
    return true;
  }
  return false;
}

function captureDeviceExample(options, serial, key, destination, remount = false) {
  openDeviceExample(options, serial, key, remount);
  prepareDeviceForCapture(options, serial);
  // Cold-start splash is a near-white frame. Poll until content exists so a
  // remount does not screenshot the previous activity or a blank launch.
  const attempts = options.forceStop || remount ? 10 : 4;
  const retryMs = remount ? 400 : 1500;
  for (let attempt = 0; attempt < attempts; attempt += 1) {
    captureDevicePng({...options, serial}, destination);
    const image = readPngFile(destination);
    if (!isMostlyBlank(image)) {
      if (remount) sleepMs(250);
      return;
    }
    if (!options.quiet) {
      process.stderr.write(`  blank device frame, retry ${attempt + 1}/${attempts}\n`);
    }
    sleepMs(retryMs);
  }
}

function tap(x, y) {
  return [
    {type: 'pointerDown', x, y, buttons: 1},
    {type: 'pointerUp', x, y, buttons: 0},
  ];
}

function swipe(x1, y1, x2, y2) {
  return [
    {type: 'pointerDown', x: x1, y: y1, buttons: 1},
    {type: 'pointerMove', x: x2, y: y2, buttons: 1},
    {type: 'pointerUp', x: x2, y: y2, buttons: 0},
  ];
}

function scroll(deltaY, x = 196, y = 420, deltaX = 0) {
  return [{type: 'scroll', x, y, deltaX, deltaY}];
}

// Logical dp on the RN root. Default (196, 420) is the page ScrollView;
// ScrollViewSimpleExample's nested horizontal row sits below items 0-3
// (title 56 + margin 10 + ~100dp/item → y≈466-606). A vertical delta on
// that row is consumed by the outer page; horizontal delta stays on-screen.
const MODULE_SCROLL = {
  ScrollViewSimpleExample: {x: 300, y: 500, deltaX: 220, deltaY: 0},
};

function scrollPage(key, step) {
  const target = MODULE_SCROLL[key];
  if (target == null) return scroll(step);
  return [{
    type: 'scroll',
    x: target.x,
    y: target.y,
    deltaX: target.deltaX ?? 0,
    deltaY: target.deltaY ?? (target.deltaX != null ? 0 : step),
  }];
}

// Logical dp on the RN root (392.7 x 753.5). Chosen from the Pixel 4a
// first-screen captures so device adb and rnsim hit the same control.
const MODULE_INTERACTIONS = {
  ButtonExample: [
    {id: 'tap-submit', label: 'tap Submit Application',
      actions: [...tap(196, 220), {type: 'wait', ms: 600}]},
  ],
  SwitchExample: [
    {id: 'toggle', label: 'toggle first switch',
      actions: [...tap(56, 255), {type: 'wait', ms: 400}]},
  ],
  PressableExample: [
    {id: 'press', label: 'press first row',
      actions: [...tap(196, 250), {type: 'wait', ms: 400}]},
  ],
  TouchableExample: [
    {id: 'press', label: 'press first touchable',
      actions: [...tap(196, 240), {type: 'wait', ms: 400}]},
  ],
  ModalExample: [
    {id: 'show-modal', label: 'tap Show Modal',
      actions: [...tap(196, 250), {type: 'wait', ms: 700}]},
  ],
  DrawerLayoutAndroid: [
    // Closed default. Pixel 4a capture: CHANGE DRAWER POSITION is 327-361dp
    // (center 344); OPEN DRAWER is 432-467dp (center 450). y=430 sits in the
    // gap above the lower button; y=360 hits Change. LogBox toast is ~700dp.
    {id: 'open-drawer', label: 'tap Open drawer',
      actions: [...tap(196, 450), {type: 'wait', ms: 800}]},
  ],
  RefreshControlExample: [
    {id: 'pull', label: 'pull to refresh',
      actions: [...swipe(196, 200, 196, 430), {type: 'wait', ms: 700}]},
  ],
  SwipeableCardExample: [
    {id: 'swipe', label: 'swipe card left',
      actions: [...swipe(310, 380, 48, 380), {type: 'wait', ms: 500}]},
  ],
  TextInputExample: [
    {id: 'focus', label: 'focus first input',
      actions: [...tap(196, 230), {type: 'wait', ms: 400}]},
  ],
  TextExample: [
    {id: 'background-border', label: 'background + border',
      deepLink: 'TextExample/background-border-width', actions: []},
    {id: 'ajusting-font-size', label: 'adjustsFontSizeToFit',
      deepLink: 'TextExample/ajustingFontSize', actions: []},
    {id: 'wrap', label: 'wrap',
      deepLink: 'TextExample/wrap', actions: []},
    {id: 'hyphenation', label: 'hyphenation',
      deepLink: 'TextExample/hyphenation', actions: []},
    {id: 'padding', label: 'padding',
      deepLink: 'TextExample/padding', actions: []},
    {id: 'legend', label: 'text metrics legend',
      deepLink: 'TextExample/textMetricLegend', actions: []},
    {id: 'font-family', label: 'fontFamily',
      deepLink: 'TextExample/fontFamily', actions: []},
    {id: 'material-fonts', label: 'Material Design fonts',
      deepLink: 'TextExample/androidMaterialDesignFonts', actions: []},
    {id: 'custom-fonts', label: 'custom fonts',
      deepLink: 'TextExample/customFonts', actions: []},
    {id: 'font-size', label: 'fontSize',
      deepLink: 'TextExample/fontSize', actions: []},
    {id: 'color', label: 'color',
      deepLink: 'TextExample/color', actions: []},
    {id: 'font-weight', label: 'fontWeight 100-900',
      deepLink: 'TextExample/fontWeight', actions: []},
    {id: 'font-style', label: 'fontStyle',
      deepLink: 'TextExample/fontStyle', actions: []},
    {id: 'font-style-weight', label: 'italic bold',
      deepLink: 'TextExample/fontStyleAndWeight', actions: []},
    {id: 'decoration', label: 'textDecoration',
      deepLink: 'TextExample/textDecoration', actions: []},
    {id: 'nested', label: 'nested color/weight/size spans',
      deepLink: 'TextExample/nested', actions: []},
    {id: 'text-align', label: 'textAlign + Arabic RTL',
      deepLink: 'TextExample/textAlign', actions: []},
    {id: 'unicode', label: 'unicode CJK',
      deepLink: 'TextExample/unicode', actions: []},
    {id: 'spaces', label: 'spaces',
      deepLink: 'TextExample/spaces', actions: []},
    {id: 'line-height', label: 'mixed nested lineHeight',
      deepLink: 'TextExample/lineHeight', actions: []},
    {id: 'letter-spacing', label: 'letterSpacing',
      deepLink: 'TextExample/letterSpacing', actions: []},
    {id: 'bg-attr', label: 'backgroundColor attribute',
      deepLink: 'TextExample/backgroundColorAttr', actions: []},
    {id: 'container-bg', label: 'containerBackgroundColor',
      deepLink: 'TextExample/containerBackgroundColorAttribute', actions: []},
    {id: 'number-of-lines', label: 'numberOfLines',
      deepLink: 'TextExample/numberOfLines', actions: []},
    {id: 'allow-font-scaling', label: 'allowFontScaling',
      deepLink: 'TextExample/allowFontScaling', actions: []},
    {id: 'max-font-multiplier', label: 'maxFontSizeMultiplier',
      deepLink: 'TextExample/maxFontSizeMultiplier', actions: []},
    {id: 'inline-views', label: 'inline views',
      deepLink: 'TextExample/inlineViewsBasic', actions: []},
    {id: 'inline-nested', label: 'nested inline texts',
      deepLink: 'TextExample/inlineViewsMultiple', actions: []},
    {id: 'inline-clipped', label: 'inline image clipped by Text',
      deepLink: 'TextExample/inlineViewsClipped', actions: []},
    {id: 'inline-image', label: 'relayout inline image',
      deepLink: 'TextExample/relayoutInlineImage', actions: []},
    {id: 'inline-view-relayout', label: 'relayout inline view',
      deepLink: 'TextExample/relayoutInlineView', actions: []},
    {id: 'inline-nested-relayout', label: 'relayout nested inline view',
      deepLink: 'TextExample/relayoutNestedInlineView', actions: []},
    {id: 'text-shadow', label: 'textShadow',
      deepLink: 'TextExample/textShadow', actions: []},
    {id: 'ellipsize', label: 'ellipsizeMode',
      deepLink: 'TextExample/ellipsizeMode', actions: []},
    {id: 'font-variants', label: 'fontVariants',
      deepLink: 'TextExample/fontVariants', actions: []},
    {id: 'include-font-padding', label: 'includeFontPadding',
      deepLink: 'TextExample/includeFontPadding', actions: []},
    {id: 'text-transform', label: 'textTransform',
      deepLink: 'TextExample/textTransform', actions: []},
    {id: 'emoji-substr', label: 'substring emoji',
      deepLink: 'TextExample/substringEmoji', actions: []},
    {id: 'linkify', label: 'dataDetector / linkify',
      deepLink: 'TextExample/textLinkify', actions: []},
    {id: 'baseline', label: 'alignItems baseline',
      deepLink: 'TextExample/alignItemsBaseline', actions: []},
    {id: 'text-alignment', label: 'textAlignVertical',
      deepLink: 'TextExample/textAlignment', actions: []},
    {id: 'clipping', label: 'rounded clip overflow',
      deepLink: 'TextExample/clipping', actions: []},
    {id: 'box-shadow', label: 'boxShadow',
      deepLink: 'TextExample/boxShadow', actions: []},
    {id: 'disabled', label: 'disabled',
      deepLink: 'TextExample/disabled', actions: []},
    {id: 'empty-text', label: 'empty Text',
      deepLink: 'TextExample/emptyText', actions: []},
    {id: 'shared-inline', label: 'shared inline views',
      deepLink: 'TextExample/inlineViews', actions: []},
    {id: 'rtl-inline', label: 'RTL inline views',
      deepLink: 'TextExample/rtlInlineViews', actions: []},
    {id: 'ontextlayout', label: 'numberOfLines onTextLayout',
      deepLink: 'TextExample/numberOfLinesLayout', actions: []},
    {id: 'link-role', label: 'role=link',
      deepLink: 'TextExample/textWithLinkRole', actions: []},
  ],
  FlatListExampleIndex: [
    {id: 'open-first', label: 'inner example basic',
      deepLink: 'FlatListExampleIndex/basic', actions: []},
  ],
  SectionListExample: [
    {id: 'open-first', label: 'inner example contentInset',
      deepLink: 'SectionListExample/contentInset', actions: []},
  ],
};

function shotsForModule(key, options) {
  const shots = [{id: 'top', kind: 'top', label: 'top', actions: []}];
  let interacted = false;
  if (options.interact) {
    for (const extra of MODULE_INTERACTIONS[key] ?? []) {
      shots.push({
        id: extra.id,
        kind: 'interact',
        label: extra.label,
        actions: extra.actions ?? [],
        deepLink: extra.deepLink ?? null,
      });
      interacted = true;
    }
  }
  if (options.scrollPages > 0) {
    const cumulative = [];
    const pageActions = scrollPage(key, options.scrollStep);
    const pageDelta = Math.abs(pageActions[0].deltaX) ||
      Math.abs(pageActions[0].deltaY) ||
      options.scrollStep;
    for (let page = 1; page <= options.scrollPages; page += 1) {
      cumulative.push(...pageActions);
      shots.push({
        id: `scroll-${page}`,
        kind: 'scroll',
        label: `scroll +${pageDelta * page}dp`,
        actions: [...cumulative],
        // Interact mutates example JS (RefreshControl pull -> Loaded rows).
        // Later scroll shots remount to a cold Initial example, then apply
        // only this shot's scroll actions. Simulator already starts a new
        // rnsim per shot; device must force-stop or previousUrl skips it.
        remount: interacted,
      });
    }
  }
  return shots;
}

function isActionPrefix(previous, next) {
  if (previous.length === 0) return true;
  if (previous.length > next.length) return false;
  return JSON.stringify(previous) ===
    JSON.stringify(next.slice(0, previous.length));
}

function shouldReopenDevice(shot, previousUrl, previousActions, urlKey) {
  if (shot.remount) return true;
  if (previousUrl !== urlKey) return true;
  if (previousActions == null) return true;
  return !isActionPrefix(previousActions, shot.actions);
}

function profileOf(options) {
  return PROFILES[options.profile] ?? PROFILES['pixel-4a'];
}

function toDevicePx(options, x, y) {
  const profile = profileOf(options);
  return {
    x: Math.round(x * profile.viewport.pointScaleFactor),
    y: Math.round(profile.cropTop + y * profile.viewport.pointScaleFactor),
  };
}

// Linear drag then dwell at the end so ScrollView sees ~0 velocity and does
// not fling. `adb input swipe` keeps constant velocity through ACTION_UP.
// One `adb shell` script so two remounted scroll pages finish before
// RefreshControlExample's 5s componentDidMount auto-load prepends Loaded rows.
function buildMotionDragScript(startX, startY, endX, endY) {
  const distance = Math.hypot(endX - startX, endY - startY);
  const steps = Math.max(6, Math.min(18, Math.round(distance / 40)));
  const lines = [`input motionevent DOWN ${startX} ${startY}`];
  for (let step = 1; step <= steps; step += 1) {
    const t = step / steps;
    const x = Math.round(startX + (endX - startX) * t);
    const y = Math.round(startY + (endY - startY) * t);
    lines.push('sleep 0.02');
    lines.push(`input motionevent MOVE ${x} ${y}`);
  }
  lines.push('sleep 0.16');
  for (let dwell = 0; dwell < 3; dwell += 1) {
    lines.push(`input motionevent MOVE ${endX} ${endY}`);
  }
  lines.push(`input motionevent UP ${endX} ${endY}`);
  return lines.join('; ');
}

function deviceDrag(options, serial, x1, y1, x2, y2) {
  const profile = profileOf(options);
  const minX = 40;
  const maxX = profile.fullWidth - 40;
  const minY = profile.cropTop + 60;
  const maxY = profile.fullHeight - profile.cropBottom - 40;
  const clampX = value => Math.max(minX, Math.min(maxX, value));
  const clampY = value => Math.max(minY, Math.min(maxY, value));
  const startX = clampX(Math.round(x1));
  const startY = clampY(Math.round(y1));
  const endX = clampX(Math.round(x2));
  const endY = clampY(Math.round(y2));
  adb(options, serial, ['shell', buildMotionDragScript(startX, startY, endX, endY)]);
  sleepMs(280);
}

function replayDeviceActions(options, serial, actions) {
  let down = null;
  let last = null;
  for (let index = 0; index < actions.length; index += 1) {
    const action = actions[index];
    if (action.type === 'wait') {
      sleepMs(action.ms || 300);
      continue;
    }
    if (action.type === 'scroll') {
      const start = toDevicePx(options, action.x, action.y);
      const scale = profileOf(options).viewport.pointScaleFactor;
      const dx = Math.round((action.deltaX || 0) * scale);
      const dy = Math.round((action.deltaY || 0) * scale);
      deviceDrag(
        options, serial, start.x, start.y, start.x - dx, start.y - dy,
      );
      continue;
    }
    const point = toDevicePx(options, action.x, action.y);
    if (action.type === 'pointerDown') {
      down = point;
      last = point;
      continue;
    }
    if (action.type === 'pointerMove') {
      last = point;
      continue;
    }
    if (action.type === 'pointerUp') {
      last = point;
      if (down == null) {
        adb(options, serial, [
          'shell', 'input', 'tap', String(last.x), String(last.y),
        ]);
      } else {
        const distance = Math.hypot(last.x - down.x, last.y - down.y);
        if (distance < 18) {
          adb(options, serial, [
            'shell', 'input', 'tap', String(last.x), String(last.y),
          ]);
        } else {
          deviceDrag(options, serial, down.x, down.y, last.x, last.y);
        }
      }
      sleepMs(200);
      down = null;
      last = null;
    }
  }
  prepareDeviceForCapture(options, serial);
}

function writeActionAdapter(filePath, actions, settleMs) {
  const source = `const registry = globalThis.RN$AppRegistry;
if (!registry) {
  throw new Error('RN Tester bundle did not install RN$AppRegistry');
}
const appKeys = registry.getAppKeys();
if (!appKeys || appKeys.indexOf('RNTesterApp') < 0) {
  throw new Error('RNTesterApp was not registered');
}
const ACTIONS = ${JSON.stringify(actions)};
const SETTLE_MS = ${settleMs};
RN$SimulatorWorkload.ready();
RN$SimulatorWorkload.registerRootTag(21);
registry.runApplication('RNTesterApp', {
  rootTag: 21,
  initialProps: {},
  fabric: true,
});
function finish(extra) {
  globalThis.RN$SimulatorWorkloadResult = Object.assign({
    appKey: 'RNTesterApp',
    opened: 'example',
    actionCount: ACTIONS.length,
  }, extra || {});
  RN$SimulatorWorkload.complete();
}
function isPointer(step) {
  return step && (
    step.type === 'pointerDown' ||
    step.type === 'pointerMove' ||
    step.type === 'pointerUp' ||
    step.type === 'pointerCancel'
  );
}
function run(index) {
  if (index >= ACTIONS.length) {
    setTimeout(function () { finish({ok: true}); }, 500);
    return;
  }
  const step = ACTIONS[index];
  if (step.type === 'wait') {
    setTimeout(function () { run(index + 1); }, step.ms || 300);
    return;
  }
  if (!globalThis.RN$Simulator || !RN$Simulator.dispatchActions) {
    finish({ok: false, reason: 'no-dispatch'});
    return;
  }
  const batch = [];
  let next = index;
  if (isPointer(step)) {
    while (next < ACTIONS.length && isPointer(ACTIONS[next])) {
      batch.push(ACTIONS[next]);
      next += 1;
    }
  } else {
    batch.push(step);
    next = index + 1;
  }
  RN$Simulator.dispatchActions(batch).then(function () {
    run(next);
  }).catch(function (error) {
    console.log(String(error));
    finish({ok: false, reason: String(error)});
  });
}
setTimeout(function () { run(0); }, SETTLE_MS);
`;
  mkdirSync(path.dirname(filePath), {recursive: true});
  writeFileSync(filePath, source);
}

function imagesNearlyEqual(leftPath, rightPath) {
  const left = readPngFile(leftPath);
  const right = readPngFile(rightPath);
  if (left.width !== right.width || left.height !== right.height) return false;
  const stats = compareImages(left, right, {threshold: 12, aa: true});
  return stats.mismatchPercent < 0.6;
}

function archivedDevicePath(options, key, shotId) {
  if (options.deviceArchive == null) {
    return null;
  }
  const slug = key.replace(/[^a-zA-Z0-9_-]+/g, '-');
  return path.join(options.deviceArchive, slug, shotId, 'device.png');
}

function reuseArchivedDevice(options, key, shotId, devicePath) {
  if (options.recaptureDevice) {
    return false;
  }
  if (existsSync(devicePath)) {
    return true;
  }
  const archived = archivedDevicePath(options, key, shotId);
  if (archived == null || !existsSync(archived)) {
    return false;
  }
  mkdirSync(path.dirname(devicePath), {recursive: true});
  copyFileSync(archived, devicePath);
  return true;
}

function saveDeviceArchive(options, key, shotId, devicePath) {
  const archived = archivedDevicePath(options, key, shotId);
  if (archived == null || !existsSync(devicePath)) {
    return;
  }
  mkdirSync(path.dirname(archived), {recursive: true});
  copyFileSync(devicePath, archived);
}

function captureRntesterSimulator(options, key, destination, actions = []) {
  if (!existsSync(options.rnsim)) {
    throw new Error(`rnsim not found at ${options.rnsim}`);
  }
  if (!existsSync(options.rntesterBundle)) {
    throw new Error(
      `RN Tester bundle not found at ${options.rntesterBundle}. ` +
        'Run: node tools/rntester/bundle.mjs',
    );
  }
  mkdirSync(path.dirname(destination), {recursive: true});
  const adapterPath = path.join(options.outDir, '_adapter.js');
  writeActionAdapter(adapterPath, actions, 2200);
  const args = [
    'headless',
    '--config', options.rntesterConfig,
    '--bundle', options.rntesterBundle,
    '--bundle', adapterPath,
    '--timeout-ms', String(options.timeoutMs),
    '--screenshot', destination,
  ];
  if (existsSync(options.androidFontDir)) {
    args.push('--android-font-dir', options.androidFontDir);
  }
  const result = spawnSync(options.rnsim, args, {
    cwd: projectRoot,
    env: {
      ...process.env,
      RNSIM_INITIAL_URL: exampleUrl(key),
    },
    encoding: 'utf8',
    maxBuffer: 16 * 1024 * 1024,
  });
  if (result.error) {
    throw new Error(`failed to spawn rnsim: ${result.error.message}`);
  }
  if (result.status !== 0) {
    const tail = (result.stderr || result.stdout || '').trim().split('\n').slice(-8)
      .join('\n');
    throw new Error(`rnsim failed for ${key} (status ${result.status}): ${tail}`);
  }
  if (!existsSync(destination)) {
    throw new Error(`rnsim did not write ${destination}`);
  }
}

function escapeHtml(text) {
  return String(text).replace(/[&<>"']/g, character => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;',
  }[character]));
}

function writeGallery(outDir, group, reports) {
  const rows = reports.map(report => {
    const image = report.outputs?.sideBySide
      ? `<a href="${escapeHtml(path.relative(outDir, path.resolve(projectRoot, report.outputs.sideBySide)))}"><img src="${escapeHtml(path.relative(outDir, path.resolve(projectRoot, report.outputs.sideBySide)))}" alt="${escapeHtml(report.name)}"></a>`
      : '';
    const percent = report.stats
      ? report.stats.mismatchPercent.toFixed(2)
      : '';
    const tone = report.error
      ? 'err'
      : (report.stats && report.stats.mismatchPercent < 5
          ? 'ok'
          : (report.stats && report.stats.mismatchPercent < 15 ? 'warn' : 'bad'));
    const status = report.error
      ? escapeHtml(report.error)
      : `${percent}%  meanΔ=${report.stats.meanChannelDelta.toFixed(2)}`;
    return `<section class="card ${tone}"><h2>${escapeHtml(report.name)}</h2><p>${status}</p>${image}</section>`;
  }).join('\n');
  const html = `<!doctype html>
<meta charset="utf-8">
<title>RN Tester ${escapeHtml(group)} screenshot compare</title>
<style>
body{font-family:ui-sans-serif,system-ui,sans-serif;margin:24px;background:#f6f6f7;color:#222}
h1{font-size:20px}
.card{background:#fff;margin:20px 0;padding:16px;border-radius:12px;box-shadow:0 1px 2px #0001}
.card img{width:100%;height:auto;border:1px solid #eee}
.ok h2{color:#0a7}
.warn h2{color:#b80}
.bad h2{color:#c00}
.err h2{color:#666}
</style>
<h1>RN Tester ${escapeHtml(group)}</h1>
<p>Device vs simulator, glance-level. Not HWUI pixel equivalence.</p>
${rows}
`;
  writeFileSync(path.join(outDir, 'index.html'), html);
}

function runRntesterGroup(options) {
  let keys = listRntesterGroup(options.rntesterGroup);
  if (options.exampleKeys != null) {
    const unknown = options.exampleKeys.filter(key => !keys.includes(key));
    if (unknown.length > 0) {
      throw new Error(
        `unknown ${options.rntesterGroup} keys: ${unknown.join(', ')}`,
      );
    }
    keys = options.exampleKeys;
  }
  if (options.only != null) {
    keys = keys.filter(key => key.includes(options.only));
  }
  if (options.list) {
    process.stdout.write(`${keys.join('\n')}\n`);
    return 0;
  }
  mkdirSync(options.outDir, {recursive: true});
  if (!options.quiet) {
    process.stderr.write(
      `rnsim ${relative(options.rnsim)}\n` +
        `${keys.length} ${options.rntesterGroup} examples -> ${relative(options.outDir)}\n`,
    );
  }
  const willReuseArchive =
    !options.recaptureDevice &&
    options.deviceArchive != null &&
    existsSync(options.deviceArchive);
  const serial = options.skipDevice || willReuseArchive
    ? null
    : resolveAdbSerial(options);
  if (serial != null) {
    wakeDevice(options, serial);
  }
  const reports = [];
  let failed = 0;
  for (const key of keys) {
    const slug = key.replace(/[^a-zA-Z0-9_-]+/g, '-');
    const moduleOut = path.join(options.outDir, slug);
    mkdirSync(moduleOut, {recursive: true});
    if (!options.quiet) {
      process.stderr.write(`${key}\n`);
    }
    if (!DEEP_LINK_KEY.test(key)) {
      const skipped = {
        name: key,
        error: 'deep-link key is not [A-Za-z0-9_-]; RN Tester URL regex cannot open it',
        outputs: {},
      };
      reports.push(skipped);
      failed += 1;
      if (!options.quiet) {
        process.stderr.write(`  SKIP  ${skipped.error}\n`);
      }
      continue;
    }
    const shots = shotsForModule(key, options);
    let previousActions = null;
    let previousSimulator = null;
    let previousUrl = null;
    let skipRemainingScroll = false;
    for (const shot of shots) {
      const shotName = `${key}/${shot.id}`;
      const urlKey = shot.deepLink || key;
      const pairOut = path.join(moduleOut, shot.id);
      mkdirSync(pairOut, {recursive: true});
      const devicePath = path.join(pairOut, 'device.png');
      const simulatorPath = path.join(pairOut, 'simulator.png');
      // After in-example interact, remount even when the deep-link URL is
      // unchanged. previousActions=[] is a prefix of every later scroll, so
      // reopen must key off shot.remount, not previousUrl. Drop
      // previousSimulator so skip-no-delta does not treat the interact frame
      // as the pre-scroll baseline (simulator is a new process).
      if (shot.remount) {
        previousActions = null;
        previousUrl = null;
        previousSimulator = null;
        skipRemainingScroll = false;
      }
      if (skipRemainingScroll && shot.kind === 'scroll') {
        reports.push({
          name: shotName,
          kind: shot.kind,
          skipped: true,
          error: 'skipped: previous scroll did not change the scene',
          outputs: {},
        });
        if (!options.quiet) {
          process.stderr.write(`  ${shot.id.padEnd(16)} SKIP no-scroll-delta\n`);
        }
        continue;
      }
      try {
        const reopen = shouldReopenDevice(
          shot, previousUrl, previousActions, urlKey,
        );
        const suffix = reopen
          ? shot.actions
          : shot.actions.slice(previousActions.length);
        if (!options.skipSimulator) {
          captureRntesterSimulator(options, urlKey, simulatorPath, shot.actions);
          if (shot.kind === 'scroll' && previousSimulator != null &&
              existsSync(previousSimulator) &&
              imagesNearlyEqual(previousSimulator, simulatorPath)) {
            skipRemainingScroll = true;
            reports.push({
              name: shotName,
              kind: shot.kind,
              skipped: true,
              error: 'skipped: scroll did not change the scene',
              outputs: {},
            });
            if (!options.quiet) {
              process.stderr.write(`  ${shot.id.padEnd(16)} SKIP no-scroll-delta\n`);
            }
            previousActions = shot.actions;
            previousUrl = urlKey;
            continue;
          }
        } else if (!existsSync(simulatorPath)) {
          throw new Error(`--skip-simulator but missing ${simulatorPath}`);
        }
        const reusedDevice = reuseArchivedDevice(
          options, key, shot.id, devicePath,
        );
        if (!reusedDevice && !options.skipDevice) {
          if (reopen) {
            captureDeviceExample(
              options, serial, urlKey, devicePath, Boolean(shot.remount),
            );
            if (suffix.length > 0) {
              replayDeviceActions(options, serial, suffix);
              captureDevicePng({...options, serial}, devicePath);
            }
          } else {
            replayDeviceActions(options, serial, suffix);
            captureDevicePng({...options, serial}, devicePath);
          }
          if (shot.kind === 'interact') {
            dismissDeviceDialogs(options, serial);
          }
          saveDeviceArchive(options, key, shot.id, devicePath);
        } else if (!reusedDevice) {
          throw new Error(
            `--skip-device but missing ${devicePath}` +
              (archivedDevicePath(options, key, shot.id)
                ? ` and archive ${archivedDevicePath(options, key, shot.id)}`
                : ''),
          );
        }
        const pairOptions = {
          ...options,
          json: path.join(pairOut, 'report.json'),
        };
        const report = comparePair(
          pairOptions, devicePath, simulatorPath, pairOut,
        );
        report.name = shotName;
        report.kind = shot.kind;
        report.label = shot.label;
        reports.push(report);
        if (report.passed === false) failed += 1;
        if (!options.quiet) {
          process.stderr.write(
            `  ${shot.id.padEnd(16)} ${formatSummary(report)}\n`,
          );
        }
        if (shot.id === 'top') {
          writeFileSync(path.join(moduleOut, 'device.png'), readFileSync(devicePath));
          writeFileSync(
            path.join(moduleOut, 'simulator.png'),
            readFileSync(simulatorPath),
          );
        }
        previousActions = shot.actions;
        previousSimulator = simulatorPath;
        previousUrl = urlKey;
      } catch (error) {
        failed += 1;
        reports.push({
          name: shotName,
          kind: shot.kind,
          error: error.message,
          outputs: {},
        });
        if (!options.quiet) {
          process.stderr.write(`  ${shot.id.padEnd(16)} ERROR  ${error.message}\n`);
        }
        previousActions = null;
        previousSimulator = null;
        previousUrl = null;
      }
    }
  }
  const summary = {
    schemaVersion: 2,
    generatedAt: new Date().toISOString(),
    group: options.rntesterGroup,
    mode: options.mode,
    threshold: options.threshold,
    aa: options.aa,
    scrollPages: options.scrollPages,
    scrollStep: options.scrollStep,
    interact: options.interact,
    deviceSerial: serial,
    examples: reports.map(report => ({
      name: report.name,
      kind: report.kind ?? null,
      skipped: report.skipped ?? false,
      error: report.error ?? null,
      mismatchPercent: report.stats?.mismatchPercent ?? null,
      meanChannelDelta: report.stats?.meanChannelDelta ?? null,
      p95ChannelDelta: report.stats?.p95ChannelDelta ?? null,
      passed: report.passed ?? null,
      report: report.report ?? null,
      sideBySide: report.outputs?.sideBySide ?? null,
    })),
  };
  const summaryPath = path.join(options.outDir, 'index.json');
  writeFileSync(summaryPath, `${JSON.stringify(summary, null, 2)}\n`);
  writeGallery(options.outDir, options.rntesterGroup, reports);
  process.stdout.write(`${JSON.stringify({
    output: relative(summaryPath),
    gallery: relative(path.join(options.outDir, 'index.html')),
    shots: reports.length,
    skipped: reports.filter(report => report.skipped).length,
    failed,
  })}\n`);
  return failed > 0 && options.maxMismatchPercent != null ? 1 : 0;
}

function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.selfTest) {
    runSelfTest();
    return 0;
  }
  if (options.rntesterGroup != null) {
    return runRntesterGroup(options);
  }
  if (options.pairDir != null) {
    const pairs = discoverPairs(options.pairDir, options.only);
    if (pairs.length === 0) {
      throw new Error(`no device-/simulator- PNG pairs in ${options.pairDir}`);
    }
    const reports = [];
    let failed = false;
    for (const pair of pairs) {
      const pairOut = path.join(options.outDir, pair.name);
      const pairOptions = {
        ...options,
        json: path.join(pairOut, 'report.json'),
      };
      const report = comparePair(
        pairOptions, pair.device, pair.simulator, pairOut,
      );
      report.name = pair.name;
      reports.push(report);
      if (report.passed === false) failed = true;
      if (!options.quiet) {
        process.stderr.write(`${pair.name.padEnd(28)} ${formatSummary(report)}\n`);
      }
    }
    const summary = {
      schemaVersion: 1,
      generatedAt: new Date().toISOString(),
      pairDir: relative(options.pairDir),
      mode: options.mode,
      threshold: options.threshold,
      aa: options.aa,
      pairs: reports.map(report => ({
        name: report.name,
        mismatchPercent: report.stats.mismatchPercent,
        meanChannelDelta: report.stats.meanChannelDelta,
        p95ChannelDelta: report.stats.p95ChannelDelta,
        passed: report.passed,
        report: report.report,
        sideBySide: report.outputs.sideBySide ?? null,
      })),
    };
    mkdirSync(options.outDir, {recursive: true});
    const summaryPath = path.join(options.outDir, 'index.json');
    writeFileSync(summaryPath, `${JSON.stringify(summary, null, 2)}\n`);
    process.stdout.write(`${JSON.stringify({
      output: relative(summaryPath),
      pairs: reports.length,
      failed: reports.filter(report => report.passed === false).length,
    })}\n`);
    return failed ? 1 : 0;
  }

  if (options.captureSimulator) {
    options.simulator = options.simulator ?? path.join(options.outDir, 'simulator.png');
    captureSimulatorPng(options, options.simulator);
  }
  if (options.captureDevice) {
    options.device = options.device ?? path.join(options.outDir, 'device.png');
    captureDevicePng(options, options.device);
  }
  if (options.device == null || options.simulator == null) {
    throw new Error('need --device and --simulator, or --pair-dir, or capture flags');
  }
  const report = comparePair(
    options, options.device, options.simulator, options.outDir,
  );
  if (!options.quiet) {
    process.stderr.write(`${formatSummary(report)}\n`);
  }
  process.stdout.write(`${JSON.stringify({
    output: report.report,
    mismatchPercent: report.stats.mismatchPercent,
    meanChannelDelta: report.stats.meanChannelDelta,
    p95ChannelDelta: report.stats.p95ChannelDelta,
    maxChannelDelta: report.stats.maxChannelDelta,
    alignment: report.alignment.method,
    passed: report.passed,
  })}\n`);
  return report.passed === false ? 1 : 0;
}

try {
  process.exit(main());
} catch (error) {
  process.stderr.write(`${error.message}\n`);
  process.exit(1);
}
