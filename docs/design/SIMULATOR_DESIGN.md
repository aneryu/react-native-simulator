# React Native Simulator architecture

Status: current architecture contract for the React Native 0.87 Android-first
experimental release.

## Product boundary

React Native Simulator is a native React Native runtime and renderer for macOS.
It hosts Hermes/JSI, ReactInstance, RuntimeScheduler, TurboModules, Fabric,
Yoga, a typed retained scene, and Skia without an iOS Simulator, Android
Emulator, or mobile client process.

The production entry point is the native `rnsim` executable. The embedding API
is `ReactNativeSimulator::Engine`. A standalone Hermes shell is diagnostic
bootstrap only and is not the product.

The repository does not own an application entry point, Metro configuration,
Babel/TypeScript setup, or a production bundle pipeline. Every runtime and
benchmark invocation consumes caller-built bundles. Core configure, build,
test, and runtime paths must not require Node.js or npm.

## Modes

- `interactive` runs the same engine with an SDL/ImGui macOS shell, input,
  live Skia rendering, application selection, reload, and optional DevTools.
- `headless` runs finite workloads, screenshots, metrics, traces, and CI jobs.
- public `test` and `conformance` commands fail closed in v0.1.0. A future
  conformance mode must compare against canonical platform profiles, fonts,
  devices, and oracles; strict headless validation is not certification.

Frontends may drive the engine but must not reimplement React Native semantics.

## Runtime ownership

RN 0.87 `ReactInstance` owns Hermes/JSI, RuntimeScheduler, error handling,
bundle loading, and shutdown. The host supplies platform adapters, a
deterministic event loop for TimerManager, TurboModule providers, Fabric
bindings, and observable diagnostics.

One runtime can sequentially load multiple source or HBC files. Repeated CLI
`--bundle` options and `RN$Simulator.loadBundle(path)` execute on the same
ReactInstance, RuntimeScheduler, and Hermes VM. Per-bundle metadata and failures
remain observable.

## Rendering pipeline

```text
caller bundle
    |
ReactFabric-prod / UIManagerBinding
    |
Fabric ShadowTree + Yoga
    |
MountingTransaction consumer
    |
typed retained scene
    |
Skia renderer
    +-- interactive SDL/ImGui shell
    +-- headless PNG output
```

Fabric creates and commits real ShadowTrees. Yoga computes layout. The host
consumes MountingTransactions and applies Create, Insert, Update, Remove, and
Delete mutations to a typed retained tree. The retained scene rejects invalid
indices, broken parent links, mounted deletes, cycles, duplicate references,
and unreachable nodes.

Text measurement and paint share cached Skia prepared paragraphs. The ImGui
shell renders controls and device chrome only; Skia owns React Native pixels.
The shell must follow [CHROME_STYLE.md](CHROME_STYLE.md).

## Events and interaction

The runtime owns a real Fabric EventDispatcher, EventQueueProcessor, and
headless EventBeat. The same-process frontend converts pointer, scroll, key,
and committed-text input into typed actions. A bounded queue crosses the UI/JS
thread boundary; JSI values are never shared across threads.

Host dialogs and services are platform adapters outside the Fabric tree. Every
adapter must be classified as implemented, host-adapted, mocked, or unavailable.
Unknown platform modules remain unavailable.

## Profiles and addons

Versioned profiles own upstream React Native contracts. The first profile is
`android-rn87`; `ios-rn87` remains experimental and uncertified. Application,
company, and third-party contracts belong in isolated
`runtime/addons/<name>/` modules and are loaded explicitly.

The generic framework provider must never register application-specific names.
Descriptor-only mocks remain visible in capability output and cannot be counted
as certified pixels or behavior. See [ADDONS.md](../guides/ADDONS.md) and the
[RN 0.87 capability baseline](../baselines/RN087_CAPABILITY_BASELINE.md).

## Configuration

`rnsim.json` is the local configuration boundary. It is versioned, resolves
paths relative to the config file, rejects unknown fields, and must not accept
values the engine ignores. Resolved runtime metadata is reported by the host;
users do not manually duplicate dependency revisions in configuration.

## Workloads and measurement

Finite workloads use explicit ready and complete signals. Timeouts, pending
work, bundle failures, JS errors, and requirement failures return nonzero.
Output includes raw bundle records, engine/profile metadata, Fabric/Yoga state,
Hermes heap and GC statistics, current/peak RSS, and process CPU time.

Benchmark samples run in isolated processes. Warmups are discarded, raw
samples are retained, and cross-version comparisons use complete binary/bundle
pairs with balanced ABBA/BAAB order. Pure JavaScript benchmarks must not
redefine the simulator product.

## Security and distribution

Caller bundles and native addons execute with the current user's permissions;
the simulator is not a security sandbox. DevTools binds only to loopback and
requires a per-session token plus Host and Origin validation.

Release assets pin RN, Hermes, fast_float, Skia, ImGui, and SDL revisions. The
macOS arm64 archive vendors non-system dynamic dependencies, audits relocation
and minimum OS versions, removes author-machine paths, signs final Mach-O bytes,
includes SPDX and license inventories, and is reproducible from a clean commit.

## Correctness rules

1. Preserve React Native semantics before optimizing performance.
2. Keep engine, frontend, platform adapters, caller bundles, and test fixtures
   as explicit boundaries.
3. Do not label a placeholder, approximation, stale test, or source inventory
   as platform certification.
4. Validate native behavior on macOS in addition to static checks.
5. Keep fixtures narrow; HBC fixtures test loading, not an application pipeline.
6. Record exact dependency revisions and verify the runtime copy being built.

Future work and certification priorities live in [ROADMAP.md](../../ROADMAP.md),
not in this architecture contract.
