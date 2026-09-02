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
`react-native-safe-area-context`. The current addon ABI cannot emit the Fabric
`topInsetsChange` event that `SafeAreaProvider` needs before it renders
children, so `android-rn87` and `ios-rn87` temporarily host-adapt
`RNCSafeAreaContext`, `RNCSafeAreaProvider`, and `RNCSafeAreaView`. Insets are
window-relative: the host draws notch/status and nav chrome around the RN
window, so a root provider reports no overlap. This is a transitional exception
to the addon boundary, not Android 15 edge-to-edge certification.

`ios-rn87` selects the iOS-facing module surface while keeping the same macOS
semantic engine. `android-rn73` is retained for explicit external RN 0.73 bundle
conformance. A profile mismatch is reported; the host does not pretend that one
React Native version implements another version's contract.

See [RN 0.87 capability baseline](../baselines/RN087_CAPABILITY_BASELINE.md) for
the current supported, approximated, mocked, and unavailable surfaces.

## RN Tester addon

The default `rns-addon-rntester.dylib` contains only contracts owned by RN's
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

Implement `ReactNativeSimulator::SimulatorAddon` in
`runtime/addons/<name>/`, build it as a dylib, and export the C ABI entry point
`react_native_simulator_addon_v2`. The descriptor declares the addon ABI, RN
version, Hermes version, and create/destroy callbacks. The loader rejects an
incompatible addon before exposing any module.

Return TurboModules from `getTurboModule()`. Declare permitted descriptor-only
component mocks through `componentCapabilities()` so they appear in
`nativeCapabilities.components`. Components not declared by the profile or an
addon remain in `fallbackComponents`; conformance runs should use
`--fail-on-component-fallback true`.

If a legacy component needs application-owned UIManager constants or command
IDs, return plain data from `viewManagerConfigs()`. Do not add its component
name or values to the simulator engine.

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

Use `componentCapabilities()` only when descriptor-only behavior is deliberate.
It can preserve tree, Yoga, diff, and mutation cost, but it must remain labeled
as a mock and cannot satisfy platform visual certification.

Run `node tools/diagnostics/verify-addons.mjs` to verify that the RN profile and
RN Tester addon remain isolated.
