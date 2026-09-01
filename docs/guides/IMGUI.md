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
window calls `Engine::requestStop()` and performs an orderly shutdown.

The Pages panel lists `AppRegistry.getAppKeys()` and hides `LogBox` by default.
Select an application, provide `initialProps` JSON, and choose Run/Re-run.
`--app-key`, `--initial-props`, and matching `rnsim.json` fields prefill the UI.
Runtime errors appear in the Pages log and stderr without covering the device.

Interaction modes:

- Interact dispatches normal RN pointer, scroll, keyboard, and TextInput events.
- Select overlays hit-test highlighting without replacing Skia pixels.
- Alt-click selects a node without dispatching a pointer event.
- Escape first dismisses the software keyboard, then sends Android
  `hardwareBackPress`. The unhandled default unmounts the running application.

Interactive alerts, sharing, permissions, URL opening, and similar device
services use explicit host dialogs. Headless mode uses deterministic mocks.
Neither path is part of the Fabric tree or a platform-equivalence claim.

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
