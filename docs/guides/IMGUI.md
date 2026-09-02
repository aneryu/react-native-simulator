# Interactive frontend and offline Inspector

The project provides two native GUI surfaces:

- `rnsim interactive` is the production same-process live frontend;
- `react-native-simulator-inspector` replays retained scenes and diagnostics
  offline.

SDL3 and Dear ImGui implement host chrome only. They do not enter the Engine
semantic layer and never paint ShadowNodes directly. Skia renders the React
Native device scene. All shell UI follows
[CHROME_STYLE.md](../design/CHROME_STYLE.md).

## Build

```sh
cmake --preset release
cmake --build --preset release
```

The release preset enables Skia and the interactive frontend. Dear ImGui is
pinned in `third_party/imgui`; SDL3 is pinned in `third_party/sdl`. CMake verifies
both revisions and does not download or modify them during configuration.

The frontend uses the official SDL3 ImGui backend. Host dialogs use macOS UI,
while the RN device remains a Skia surface.

## Live interactive mode

```sh
build/release/runtime/rnsim interactive \
  --config /path/to/rnsim.json
```

The main thread owns SDL/ImGui and the runtime thread owns JSI. Pointer, wheel,
keyboard, and committed-text input cross a bounded typed queue. Closing the
window cancels Metro preparation (including an in-flight loopback HTTP request),
calls `Engine::requestStop()`, and performs an orderly shutdown.

The live UI is application-first rather than an internal runtime dashboard. The
Device is the default main view. Several `AppRegistry.getAppKeys()` entries or a
blocking error force the App panel open. A newly observed missing-module or
fallback-component warning opens it once. Closing the panel keeps it closed until
another warning appears, and the toolbar can always reopen it. It hides `LogBox`
by default. Launch options expose `initialProps` JSON and Run/Restart App;
`--app-key`, `--initial-props`, and matching `rnsim.json` fields prefill them.
Preparation, runtime, rendering, input, and AppRegistry errors open the copyable
App log and do not cover the device. Engine and JavaScript failures also remain
observable on stderr.

The primary toolbar chip follows the actual lifecycle: `preparing`,
`prepare failed`, `starting runtime`, `loading`, `starting app`, `choose app`,
`live`, `reloading`, `error`, or `stopped`. When HMR is active, a second chip
shows `Enabling Fast Refresh…`, `Fast Refresh enabled`, or `Fast Refresh failed`.
The enabling and enabled states use the informational tone. Empty Device messages
provide the corresponding waiting, start, and recovery action instead of
requiring the App panel to remain visible during a normal single-application
session.

`Fast Refresh enabled` means that RN's HMR client was enabled successfully for
the current runtime generation. It is not a continuous Metro WebSocket health
probe.

Interaction modes:

- Interact dispatches normal RN pointer, scroll, keyboard, and TextInput events
  only while the Engine phase is `Running`; leaving that phase cancels any
  active application pointer instead of queueing input into a paused or
  reloading runtime.
- Inspect opens ShadowTree, cancels an active application pointer, and isolates
  normal canvas pointer, keyboard, and TextInput dispatch while selecting and
  highlighting nodes without replacing Skia pixels.
- `Command-1` selects Interact, `Command-2` selects Inspect, and `Command-R`
  invokes the available Reload or Retry action; these shortcuts remain active in
  Inspect.
- Escape leaves Inspect first. In Interact it dismisses the software keyboard,
  then sends Android `hardwareBackPress`; the unhandled default unmounts the
  running application.

Interactive alerts, sharing, permissions, URL opening, and similar device
services use explicit host dialogs. Headless mode uses deterministic mocks.
Neither path is part of the Fabric tree or a platform-equivalence claim.

### Reload and retry boundary

Once the Engine is running, paused after an error, or waiting for an application
choice, **Reload**, `Command-R`, and Metro's `r` replace the
Hermes/ReactInstance generation, fetch Metro/HTTP sources again, and restart the
current application while keeping the macOS window open.

If preparation fails before the first bundle loads—for example, Metro times out
or the bundle fetch fails—the toolbar offers **Retry**, and `Command-R` invokes
the same action. Preparation is transactional: every remote source is fetched
before any bundle is queued, so the corrected remote attempt can repeat safely
in the same window. Another Retry is ignored while an attempt is in flight.

### Structured engine status

Embedding clients can query `Engine::runtimeStatus()` while `run()` is active.
The thread-safe snapshot contains the current generation and lifecycle phase,
HMR state/error, deduplicated structured diagnostics, and observed native-module
plus mounted official, addon, or fallback-component capability usage. JavaScript
diagnostics include fatal state and file, method, line, and column stack frames;
other kinds cover application errors, missing native modules, and fallback
Fabric components.

This snapshot is usage-driven and scoped to the current generation, not a full
capability inventory or cross-reload history. The toolbar renders its actual
phase and HMR state. The App panel renders structured errors and JavaScript stack
frames plus the capabilities or degradations observed in that snapshot; its
copyable string log also retains preparation, rendering, and input failures
outside the Engine status. The `Compatibility · N used · M limited` section
lists each observed capability's type, name, classification, and fidelity and
opens by default when any use is limited. `limited` counts `mocked`,
`layout-only`, and `unavailable`; `implemented` and `host-adapted` remain visible
without being labeled limited. Classification is supplied by the Engine enum;
the UI does not infer it from the free-form fidelity description. The toolbar App
label keeps the limited count visible after the panel is closed. Consult the
[capability baseline](../baselines/RN087_CAPABILITY_BASELINE.md) for the complete
declared support boundary.

## Device geometry

Use the RN root's logical size and density, excluding system bars. For the
Pixel 4a reference:

```sh
build/release/runtime/rnsim interactive \
  --viewport-width 392.7273 \
  --viewport-height 753.4545 \
  --point-scale-factor 2.75
```

The 1080x2340 device screenshot includes system bars; the RN root is 1080x2072.
The interactive shell reserves status and navigation chrome outside the RN
window. Screenshots contain the RN scene only.

For Android typography comparisons, provide a controlled font directory:

```sh
build/release/runtime/rnsim interactive \
  --android-font-dir /path/to/android-fonts
```

Host font fallback is usable for layout exploration but is not Android text
certification.

## Offline Inspector

```sh
build/release/frontend/react-native-simulator-inspector \
  --metrics /path/to/metrics.json
```

The Inspector consumes the versioned retained-scene wire payload and diagnostic
metrics. It does not attach to, control, or own a live ReactInstance. Live
runtime inspection belongs to the same-process frontend and React Native
DevTools.

The mounting tree and ShadowNode tree may differ because Fabric flattens
layout-only views. The retained renderer consumes mounting state; the shadow
tree remains diagnostic structure.
