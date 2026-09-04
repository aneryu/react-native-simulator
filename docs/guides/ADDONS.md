# React Native profiles and application addons

React Native Simulator separates compatibility into two explicit layers:

- a versioned profile owns upstream React Native platform contracts;
- an addon owns application, company SDK, or third-party contracts.

Application-specific names must never be registered by the framework provider.
Unknown TurboModules remain unavailable, and unknown components remain observable
fallbacks rather than silently becoming certified behavior.

## Profiles

`android-rn87` is the default profile for RN 0.87 Android bundles. It hosts the
runtime scheduler, Fabric/Yoga, Android-facing framework TurboModules, the
official component descriptor set, host environment events, and the Skia-backed
visual adapters documented in the capability baseline.

RN 0.87 deprecates official `SafeAreaView` in favor of
`react-native-safe-area-context`. Official `SafeAreaView` stays a framework
component. `RNCSafeAreaContext`, `RNCSafeAreaProvider`, and `RNCSafeAreaView`
are served only by the built-in `safe-area` addon, which auto-loads for every
project (`AUTO always`). Disable it with `--no-addon safe-area`. v1 insets are
zero; `initialWindowMetrics.frame` is the snapshot viewport at `(0,0)`.
Provider `topInsetsChange` carries the committed layout frame. This is not
Android 15 edge-to-edge certification.

`ios-rn87` selects the iOS-facing module surface while keeping the same macOS
semantic engine. There is no `android-rn73` profile. RN 0.73.x JavaScript runs
on the RN 0.87 native engine through `--profile android-rn87 --addon compat-rn73`.
`compat-rn73` never auto-loads. A profile mismatch is reported; the host does
not pretend that one React Native version implements another version's contract.

See [RN 0.87 capability baseline](../baselines/RN087_CAPABILITY_BASELINE.md) for
the current supported, approximated, mocked, and unavailable surfaces.

## Expo addon

Expo is a third-party contract, not an RN profile. The host detects an Expo
project from `package.json` (`dependencies.expo`) or an `expo` key in
`app.json` / `app.config.json`. Detection does not require Node.js.

`registerRootComponent` always registers AppRegistry key `main`. `expo.name`
is a display string and is never used as `--app-key`. Metro entry discovery
resolves `package.json` `main` through `node_modules` (`expo/AppEntry`,
`expo-router/entry`) and prefers those files when `./index.js` is absent.

The built-in `expo` addon is loaded automatically from an Expo project root,
or explicitly with `--addon expo` / `--addon /path/to/rns-addon-expo.dylib`.
It host-adapts only the modules needed to boot Expo's JS runtime:

- `global.expo` (`EventEmitter`, `NativeModule`, `SharedObject`, `SharedRef`, `modules`)
- `ExpoAsset`, `ExpoKeepAwake`, `ExpoSplashScreen`, `ExpoFontLoader`,
  `ExpoSystemUI`, `ExponentConstants`, `ExpoModulesCore`, `ExpoFetchModule`,
  `ExpoLinking`

`ExponentConstants` reports `executionEnvironment: "bare"` and does not
install `ExpoGo`, so `isRunningInExpoGo()` stays false. This is not Expo Go
and not an Expo SDK certificate. Expo Router, `react-native-screens`,
`react-native-reanimated`, `react-native-gesture-handler`, `expo-image`, and
other SDK modules remain unavailable unless a separate addon provides them.

## RN Tester addon

The default `rns-addon-rntester` MODULE (`rns-addon-rntester.dylib` on macOS,
`rns-addon-rntester.so` on Linux) contains only contracts owned by RN's
official demo application:

- `RNTReportFullyDrawnView`, `RNTMyNativeView`, `RNTMyLegacyNativeView`, and
  `AndroidPopupMenu` as descriptor-only component mocks;
- `NativeCxxModuleExampleCxx` and `ScreenshotManager` as tester stubs.

The caller still owns the JavaScript bundle. Build and run it explicitly:

```sh
node tools/rntester/bundle.mjs --install
build/release/runtime/rnsim --config build/rntester/rnsim.json
```

## Adding an application addon

Nightly addon authoring requires the exact source checkout and pinned React
Native/Hermes headers. The one-file DMG installs no ABI headers or upstream C++
header trees. Build addons in-tree (or reproduce the same pinned toolchain and
header inputs) and distribute the resulting compatible dylib separately.

Add `runtime/addons/<name>/CMakeLists.txt` (or an out-of-tree directory in
`RNS_ADDON_DIRS`) and call `rns_declare_addon` with `BUILTIN` and/or `MODULE`.
There is no root-owned allowlist. The Nightly catalog is exactly `expo`,
`safe-area`, and `compat-rn73`. A MODULE exports
`react_native_simulator_addon_v4`. The descriptor carries ABI 4, the
configure-time API fingerprint, RN, Hermes, and create/destroy. The loader
opens each MODULE once, rejects an incompatible descriptor before `create()`,
and `dlclose`s only after `destroy`.

Implement every `SimulatorAddon` slot (`= 0`). Return owned TurboModules from
`getTurboModule()`. Use `wrapTurboModule` only for declared overlay targets
(identity `return framework` is the unused no-op). Register real Fabric
descriptors through `AddonFabricRegistrar` in `configureFabric`. Declare
modules, overlays, and components in `manifest()`. Components not declared by
the profile or an addon remain in `fallbackComponents`; conformance runs should
use `--fail-on-component-fallback true`.

If a legacy component needs application-owned UIManager constants or command
IDs, return them from `manifest().viewManagerConfigs`. Do not add those names
to the framework provider.

Load an addon explicitly and inspect the selected profile/addon contract in the
metrics output:

```sh
build/release/runtime/rnsim headless \
  --profile android-rn87 \
  --bundle /path/to/application.bundle \
  --addon /path/to/application-addon.dylib \
  --timeout-ms 5000
```

The base application bundle may load additional bundles sequentially through
another `--bundle` option or `RN$Simulator.loadBundle(path)`. All loads stay on
the same ReactInstance, RuntimeScheduler, and Hermes VM.

### TurboModule implementation

Implement real C++ behavior for pure computation and serialization. Device
services must use an explicit host adapter or remain unavailable. Promise and
callback completion must return through `RuntimeSchedulerCallInvoker`; never
move JSI values across threads.

When Codegen output exists, compile the generated spec and inherit its
`NativeXXXCxxSpec` type so argument conversion and Promise/callback contracts
remain versioned. Application Codegen output belongs to the addon and must not
introduce npm as a core runtime build dependency.

### Fabric component implementation

A real component needs Props parsing, a ShadowNode, ComponentDescriptor,
mounting state, commands, and EventEmitter behavior. It participates in the
same Fabric ShadowTree and Yoga layout as framework components. Visual behavior
must ultimately produce typed retained-scene data for Skia.

Declare `AddonComponentKind::DescriptorOnlyMock` only when descriptor-only
behavior is deliberate.
It can preserve tree, Yoga, diff, and mutation cost, but it must remain labeled
as a mock and cannot satisfy platform visual certification.

Run `node tools/diagnostics/verify-addons.mjs` to verify that the RN profile and
RN Tester addon remain isolated.
