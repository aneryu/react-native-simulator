# React Native 0.87 capability baseline

Status: Android-first Phase 4 baseline. Implemented behavior is not a claim of
complete Android or iOS equivalence.

## Pinned runtime

| Dependency | Revision |
| --- | --- |
| React Native | `0.87.0` / `4bc2473f5d0233ea5384c1ef24f6a55615de2220` |
| Hermes | `hermes-v260318099.0.1` / `73855a1475e016cec814d67578f860112ef711e1` |
| fast_float | `v8.0.0` / `77cc847c842c49e7e3477c1e95da2b6540166d66` |
| Skia | `cacf77bdba7ba7df8ea7236d7e14b08c658ff368` |

These values come from the checked-out submodules and
`cmake/DependencyVersions.cmake`. The newer Hermes revision is intentional:
RN 0.87 RuntimeScheduler requires `IEventLoopControl`.

## Verified runtime path

The current Release and ASan+UBSan presets pass 27/27 tests. Coverage includes
the embedding API, source/HBC and multi-bundle loading, HTTP/Metro transport,
reload and HMR protocol, DevTools, Fabric/Yoga, typed Skia scenes, traces,
clean CMake package consumption, RN 0.73/0.87 profiles, the RN Tester addon,
and caller-built RN Tester Android startup.

This proves that the current host path runs. It does not prove full platform
parity. Runtime metrics are the authoritative per-session inventory:
`rnFrameworkModules`, `rnFrameworkComponents`, `nativeCapabilities`, and
`fallbackComponents`.

## Capability labels

- **implemented**: uses the pinned RN contract and has direct runtime tests.
- **host-adapted**: preserves the RN API but uses an explicit macOS/headless
  service instead of Android or iOS platform code.
- **mocked**: deterministic test or interactive behavior with no parity claim.
- **layout-only / descriptor-only**: preserves Fabric/Yoga/tree cost but does
  not implement platform pixels or service behavior.
- **unavailable**: the provider returns no module or the command fails closed.

## NativeModule summary

| Surface | Current behavior | Classification |
| --- | --- | --- |
| Feature flags, Microtasks, DOM, IdleCallbacks | Pinned RN accessors and runtime/Fabric adapters | implemented |
| UIManager | Fabric compatibility methods and LayoutAnimation driver | implemented |
| NativeAnimatedModule | RN AnimatedNodesManager with host choreographer | implemented, platform timing uncertified |
| Intersection/Mutation observers | RN observer managers exposed by host TurboModules | implemented |
| Performance | host mark/measure timeline; no full PerformanceObserver engine | host-adapted |
| ExceptionsManager, LogBox, DevLoadingView, RedBox | stderr or shape-preserving no-op adapters | host-adapted |
| DevSettings | interactive in-process reload; menu/inspector methods limited | host-adapted |
| DeviceInfo, SourceCode, AppState, Appearance | viewport, bundle URL, and host environment events | host-adapted |
| I18nManager | host RTL state applied when a surface starts | host-adapted |
| Networking | NSURLSession text/base64/blob responses | host-adapted |
| Blob/FileReader | in-memory blob store | host-adapted |
| WebSocket | NSURLSessionWebSocketTask | host-adapted |
| ImageLoader/Editing/Store | ImageIO, HTTP/file cache, crop, and base64 storage | host-adapted |
| Clipboard | NSPasteboard string access | host-adapted |
| Keyboard and accessibility | explicit host metrics/environment events | host-adapted |
| BackHandler | hardwareBackPress event; unmount fallback | host-adapted |
| Intent, permissions, dialog, share, vibration | deterministic headless mock or interactive ImGui prompt | mocked |
| SoundManager, HeadlessJsTaskSupport, FrameRateLogger, ModalManager | shape-preserving adapters | mocked |
| Unknown modules | `nullptr` / enforcing lookup failure | unavailable |

`android-rn87` and `ios-rn87` provide profile-specific `PlatformConstants`.
The iOS profile also exposes a limited set of iOS-facing service adapters, but
the profile remains uncertified. `macos-rn87` intentionally has no mobile
`PlatformConstants` surface.

## Fabric component summary

The registry explicitly handles:

```text
Root, View, RawText, Text, Paragraph, ScrollView, Image,
AndroidTextInput/TextInput, ActivityIndicatorView, AndroidSwitch, Switch,
AndroidProgressBar, ModalHostView, AndroidDrawerLayout,
AndroidSwipeRefreshLayout, PullToRefreshView,
AndroidHorizontalScrollView, AndroidHorizontalScrollContentView,
SafeAreaView, InputAccessory, VirtualView, VirtualViewExperimental,
DebuggingOverlay, RCTImageView
```

`HeadlessSampleView` is test-only and is not an RN platform capability.
Unregistered components use an observable fallback descriptor. Addons may
declare descriptor-only components, but neither path counts as support.

### Skia-backed behavior

- View layout, background, independent borders, elliptical radii, overflow,
  outline, opacity composition, mount order, backface visibility, elevation,
  ripple, filter chains, gradients, blend modes, isolation, and box shadows.
- Text and Paragraph through shared SkParagraph measurement/paint, ellipsis,
  font scaling, host font profiles, first-strong direction, font features,
  shadows, and baseline measurement.
- Local/Metro/HTTP images with typed resize/tint/blur/default-source state and
  load/error events.
- Activity/progress indicators, Android switch, modal host, drawer, refresh
  control, toast, status-bar chrome, ScrollView state, and TextInput events.

These painters target visual similarity to a Pixel RN Tester baseline. They are
not HWUI pixel equivalence. Known boundaries include:

- `background-repeat` and `background-size` are not painted;
- `rotateY` remains a 2D determinant approximation;
- PlatformColor uses a host AppCompat DayNight token map, not Android Resources;
- Android font padding and hyphenation are deterministic approximations;
- `VirtualView`, `InputAccessory`, generic `Switch`, `PullToRefreshView`,
  `RCTImageView`, and `DebuggingOverlay` remain primarily layout-only;
- no macOS accessibility platform source feeds the interactive frontend.

## Source inventory

Optional diagnostics extract candidates from the pinned RN checkout:

```sh
node tools/diagnostics/inventory-rn-modules.mjs
node tools/diagnostics/inventory-rn-components.mjs
```

The scripts use Node built-ins and are outside core build/runtime paths. Their
output is a source inventory, not a public API list or support claim. Generated
compatibility re-exports, internal descriptors, and platform hints require
manual classification against runtime evidence.

## Certification boundary

The runtime has a production CLI, embedding Engine, ReactInstance,
RuntimeScheduler, Fabric/Yoga mounting, a typed retained scene, Skia paint,
same-process interaction, event dispatch, multi-bundle loading, and DevTools.
It is usable as an experimental simulator, but Android and iOS capability
certification is incomplete.

RN Tester is the official Android demo baseline; repository probes are narrow
tests only. See [RNTESTER_BASELINE.md](RNTESTER_BASELINE.md) and
[ROADMAP.md](../../ROADMAP.md).
