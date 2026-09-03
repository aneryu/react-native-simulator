# react-native-simulator

## Project goal

This project builds React Native Simulator, a native React Native runtime and renderer.
It runs caller-provided React Native applications without an iOS or Android client process and
supports interactive, headless, and conformance modes through one semantic engine.
The Nightly binary targets macOS arm64. Linux is a source-build host for the same engine.

The target is `react-native-simulator`, not a standalone Hermes runner, benchmark tool, or a
headless-only product. The completed engine must include Hermes/JSI, React Native runtime
initialization, RuntimeScheduler, TurboModule hosting, Fabric ShadowTree/Yoga, a typed retained
scene, Skia text/layout rendering, event dispatch, and frontends that do not duplicate semantics.
See `docs/design/SIMULATOR_DESIGN.md` for the architecture contract and `ROADMAP.md` for future certification work.

## Current repository state

- The runtime uses React Native 0.87.0 and Hermes v1 260318099.0.1. React JS belongs to the
  caller's external bundle; the host reports RN's React peer range rather than claiming a loaded
  React version. RN,
  Hermes, and fast_float are pinned Git submodules. The Hermes revision is intentionally newer
  than RN's stale `.hermesv1version` because RN 0.87 RuntimeScheduler requires
  `IEventLoopControl`; keep the validated combination in `cmake/DependencyVersions.cmake`.
- The production CLI is the native `rnsim` executable (CMake target `react-native-simulator`), and
  the embedding API is `ReactNativeSimulator::Engine`. RN 0.87 ReactInstance owns
  Hermes/JSI, RuntimeScheduler, error handling, bundle loading, and shutdown. The standalone
  Hermes CLI is diagnostic bootstrap only.
- This repository does not own an application entry point, Metro/Babel/TypeScript configuration, or a
  production bundle pipeline. Every runtime and benchmark invocation must receive caller-built bundles
  explicitly. Core configure/build/test/runtime paths must not require Node.js or npm. Optional
  reporting tools may live under `tools/benchmark/` or `tools/diagnostics/`
  and use only Node built-ins.
- One runtime can sequentially load multiple HBC files through repeated CLI `--bundle` options or
  the Promise-based `RN$Simulator.loadBundle(path)` API. Loads stay on ReactInstance and
  RuntimeScheduler; per-bundle metadata and failures are observable, and all bundles share one VM.
- Fabric/Yoga is connected end-to-end: ReactFabric-prod calls the native UIManagerBinding,
  UIManager commits a ShadowTree, RN Yoga computes layout, and a headless consumer pulls and
  applies MountingTransactions. Startup verifies two JS commits plus Create/Insert/Update and
  the expected 100/200 -> 120/180 Yoga widths.
- The host also runs a native-only Fabric self-test to distinguish reconciler failures from
  ShadowTree/Yoga/mounting failures.
- Versioned profiles expose `android`, `ios`, or `macos` while public instances have no DOM/native
  view. Viewport culling is disabled and a deterministic event loop backs RN's real TimerManager.
- RN TurboModuleBinding hosts native FeatureFlags/Microtasks plus explicit headless UIManager and
  ExceptionsManager adapters. Fabric has a real EventDispatcher/EventQueueProcessor and headless
  EventBeat. The same-process frontend supplies pointer, scroll, key, and committed-text events;
  accessibility platform sources remain absent. Unknown platform modules stay unavailable.
- The workload protocol has explicit ready/complete signals, timeout and pending-work diagnostics,
  and persisted JSON output. Workload names and result fields belong to the caller's bundle, not to a
  repository sample application. The benchmark runner launches isolated
  processes, discards warmups, preserves raw samples, and reports distribution statistics.
  Hermes heap/GC, host current/peak RSS, and process CPU are sampled by the native host. Cross-
  version comparison uses complete binary+bundle pairs and independent-process ABBA/BAAB order.
  Pure-JavaScript benchmark features must not displace or redefine the simulator goal.
- `rnsim.json` is the local configuration boundary. It is versioned, resolves paths relative to
  the config file, rejects unknown fields, and must not silently accept values the engine ignores.
- The mode model is interactive/headless/conformance. The release preset enables Skia and the
  same-process SDL/ImGui frontend. Direct screenshots and interactive frames consume typed retained
  scenes, while Yoga measurement plus paint share cached Skia prepared paragraphs. The standalone
  Inspector is offline replay only. A bounded action queue and Promise batch JS API drive RN
  pointer/scroll/TextInput events without sharing JSI across threads.
  Retained scene mutations reject invalid insert/remove indices and parent links, mounted deletes,
  cycles, duplicate references, and unreachable nodes; an optional RN 0.73.2 external-bundle test
  covers create/insert/update/reorder/remove/delete without adding a bundle pipeline.
- Do not infer an implementation choice from the neighboring `host-less-dev-server` project.
  That project may provide historical context, but it is not this runtime.

## Development guidance

- Make the engine/frontend/platform boundary explicit. Keep platform adapters separate from
  React Native JavaScript, bundling, frontends, and test fixtures.
- Interactive and Inspector chrome follows `docs/design/CHROME_STYLE.md`. Dear ImGui is host shell only;
  Skia paints the RN device. Do not add a second brand accent, a light-mode chrome,
  decorative gradients, or restyle Fabric/Skia pixels to match the shell.
- Keep upstream RN contracts in versioned profiles and application/company/third-party contracts
  in isolated `runtime/addons/<name>/` directories. Never register application-specific module
  names in the RN framework provider.
- Record the exact React Native revision and JavaScript engine once selected. Runtime-source
  claims must be checked against the installed/runtime copy, not only a local reference copy.
- Preserve React Native semantics where possible. Official visual components must ultimately use
  Skia; device services may use explicit, user-modifiable platform adapters. Never silently treat
  a placeholder, mock, or unknown component/module as certified behavior.
- Prefer small, observable milestones following `ROADMAP.md`: preserve the engine,
  replace the mounting/rendering boundary, add the interactive frontend, then certify Android and iOS.
- Validate behavior on the host OS in addition to static checks. Tests cover startup,
  shutdown, exceptions, timers/microtasks, module loading, Fabric/EventDispatcher, and repeated
  isolated workloads.
- Keep test fixtures narrow. Compiling a fixture to HBC may test the bytecode loader, but must not grow
  into an application bundle pipeline or a default runtime bundle.
- Keep generated bundles, caches, logs, and machine-specific settings out of version control.

## Handoff expectations

Every implementation change should state the chosen runtime/host assumptions, preserve the
production entry point, and include the narrowest relevant verification command and result.
