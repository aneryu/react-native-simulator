# Roadmap

React Native Simulator follows the phased architecture in
[SIMULATOR_DESIGN.md](docs/design/SIMULATOR_DESIGN.md). The rolling Nightly is
experimental and Android-first.

## Addon host ABI 4

- The engine hosts addons through ABI 4, a generated catalog, and a
  transactional launch planner. `safe-area` auto-loads; `compat-rn73` is the
  only RN 0.73.x JS adapter on the RN 0.87 native engine.
- Deferred: addon-painted Skia components, dynamic viewport/inset updates, and
  addon-declared Fabric name aliases. See
  [ADDON_HOST_ARCHITECTURE.md](docs/design/ADDON_HOST_ARCHITECTURE.md).

## Expo host-adapter

- Expo project identity, AppEntry / expo-router entry discovery, and AppRegistry
  key `main` are implemented. The built-in addon host-adapts boot modules only.
- Expo Router, Reanimated, Screens, Gesture Handler, Expo Go, and the rest of
  the Expo SDK remain unavailable. Do not treat the host-adapter as SDK
  certification.

## Phase 4: RN 0.87 Android capability certification

- close the remaining NativeModule inventory with explicit implemented,
  host-adapter, mock, or unavailable judgments;
- replace remaining layout-only component entries where observable platform
  semantics matter (`VirtualView`, `InputAccessory`, generic `Switch`,
  `PullToRefreshView`, `RCTImageView`, and `DebuggingOverlay`);
- implement or explicitly reject `background-repeat` and `background-size`;
- replace the current 2D `rotateY` approximation with a certified transform
  boundary;
- expand RN Tester visual and interaction evidence beyond the sealed Text cases;
- add live Android conformance for host-adapted device services such as
  Vibration, Toast, permissions, intents, and segmented bundle loading.

## Phase 5: iOS profile and conformance

- certify the RN 0.87 iOS module/component inventory;
- validate CoreText behavior and iOS event/service adapters against real iOS
  execution;
- publish platform-specific capability matrices without sharing platform mocks.

## Phase 6: distribution and automation

- keep ordinary macOS CI independent from local signing and publication;
- maintain the Developer ID signed, notarized, and stapled one-file arm64 DMG,
  then evaluate a universal binary once the Skia bootstrap supports Intel;
- maintain checksum generation, static-dependency audits, sanitizer gates, and
  release provenance while keeping caller bundles outside the runtime artifact.

## Phase 7: stable embedding contract

- stabilize `ReactNativeSimulator::Engine`, retained-scene, action, and metrics
  schemas;
- document compatibility and deprecation policy;
- certify repeated long-running interactive sessions and embedding hosts.
