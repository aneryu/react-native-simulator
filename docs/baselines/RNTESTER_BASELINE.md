# RN Tester baseline

Status: RN 0.87 `packages/rn-tester` is the official Android demo and visual
reference. Repository-created probes never replace platform evidence.

## Ownership

The repository does not embed RN Tester JavaScript in the runtime. The caller
bundle remains external, and core configure/build/test paths remain independent
of Node.js. Optional tooling invokes the pinned RN checkout and writes generated
files below `build/`.

The `rns-addon-rntester` target contains only RN Tester-owned contracts:

| Contract | Owner | Current behavior |
| --- | --- | --- |
| `RNTReportFullyDrawnView` | RN Tester | descriptor-only |
| `RNTMyNativeView` | NativeComponentExample | descriptor-only |
| `RNTMyLegacyNativeView` | Paper interop example | descriptor-only plus legacy constants |
| `AndroidPopupMenu` | RN Tester dependency | descriptor-only |
| `NativeCxxModuleExampleCxx` | RN Tester | tester stub |
| `ScreenshotManager` | RN Tester | tester stub |

The addon is built and tested by default but is installed only by the separate
`rntester-demo` component. It never enters the generic runtime archive.

## Inventory

```sh
node tools/diagnostics/verify-rntester.mjs
```

The inventory script uses Node built-ins, parses Android example keys from the
pinned RN Tester source, and requires the checked baseline table to match. A
new or removed upstream example therefore requires an explicit classification.

## Build the caller bundle

```sh
node tools/rntester/bundle.mjs \
  --install \
  --dev false \
  --out-dir build/release-rntester \
  --build-dir build/release
```

The tool builds missing React Native Codegen output, invokes RN's Metro/Yarn
toolchain, writes `RNTesterApp.android.jsbundle`, assets, a startup adapter, and
a relative `rnsim.json`. `--hbc` additionally compiles with the exact `hermesc`
from the selected build. Failures never fall back to a repository probe.

## Run RN Tester

Interactive:

```sh
build/release/runtime/rnsim \
  --config build/release-rntester/rnsim.json
```

Finite headless startup:

```sh
build/release/runtime/rnsim headless \
  --config build/release-rntester/rnsim.json \
  --bundle build/release-rntester/RNTesterApp.android.jsbundle \
  --bundle build/release-rntester/rntester-startup-adapter.js \
  --timeout-ms 15000
```

The CLI bundle list intentionally replaces the single config bundle, so both
ordered bundles are explicit. A passing startup requires ReactFabric,
AppRegistry launch, no JS errors, no pending work, and the expected addon.

To require the caller-built bundle in CTest:

```sh
cmake --preset release \
  -DRNS_RNTESTER_BUNDLE="$PWD/build/release-rntester/RNTesterApp.android.jsbundle" \
  -DRNS_REQUIRE_RNTESTER_BUNDLE=ON
cmake --build --preset release
ctest --preset release -R rntester-android-startup --output-on-failure
```

## Android oracle

Deep links use `rntester://example/<moduleKey>`. `IntentAndroid.getInitialURL`
reads `RNSIM_INITIAL_URL`, which allows a headless run to open a specific
example directly.

Use a release Android build for the device oracle. RN Tester debug variants
connect to Metro and do not package the same offline application bundle.

```sh
cd third_party/react-native
./gradlew :packages:rn-tester:android:app:assembleRelease \
  -PreactNativeArchitectures=arm64-v8a
adb install -r packages/rn-tester/android/app/build/outputs/apk/release/app-release.apk
```

Capture the device with `adb exec-out screencap -p`. A Pixel 4a screenshot is
1080x2340, while the simulator screenshot covers only the RN root
(392.7273 x 753.4545 points at scale 2.75, or 1080x2072) and excludes system
status/navigation bars.

## Visual comparison

```sh
RNSIM_INITIAL_URL=rntester://example/ViewExample \
node tools/diagnostics/compare-screenshots.mjs \
  --capture-device --capture-simulator --profile pixel-4a \
  --out-dir build/release-rntester/compare/view -- \
  build/release/runtime/rnsim headless \
    --config build/release-rntester/rnsim.json \
    --bundle build/release-rntester/RNTesterApp.android.jsbundle \
    --bundle tests/fixtures/rntester-view-adapter.js \
    --android-font-dir build/android-fonts \
    --timeout-ms 15000
```

`glance` mode uses a tolerance intended for human visual review. `strict` mode
reduces the threshold, but neither mode turns deterministic Skia paint into
HWUI pixel equivalence. A mismatch limit must be supplied explicitly before the
tool can fail certification.

The component gallery can capture initial, interaction, and scroll states:

```sh
node tools/diagnostics/compare-screenshots.mjs \
  --rntester-group Components --profile pixel-4a
```

Generated screenshots, device archives, reports, and galleries stay below
`build/` and are not release assets. The sealed Text baseline under
`baselines/visual/` is the checked visual reference.

## Current evidence and limitations

- The RN Tester launcher starts through `NativeDOMCxx.linkRootNode`, Fabric,
  Yoga, the mounting consumer, and the RN Tester addon without component
  fallback or JS errors.
- The Text gallery covers all 50 named TextExample cases in the archived run.
  Wrapping, alignment, borders, CJK line breaking, CRLF, inline attachments,
  ellipsis, and major baseline behavior are structurally aligned; some
  multiline vertical metrics still differ.
- View painting covers independent borders, elliptical radii, overflow,
  opacity, mount order, outline, elevation, ripple, filters, gradients, blend
  modes, and inset/outset shadows. Acceptance is visual similarity, not HWUI.
- Pixel font comparison requires an explicit Android font directory containing
  the relevant Roboto, Noto, emoji, and RN Tester fonts. These fonts are local
  test inputs and are not committed or distributed.
- Device services implemented through macOS/ImGui adapters require separate
  behavior evidence; launcher success does not certify them.

Every RN Tester example remains a capability item until its modules/components
are classified, its visual surfaces have real Skia behavior or an explicit
nonvisual boundary, and its evidence is recorded without calling a mock
equivalent.
