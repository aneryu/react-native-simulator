<p align="center">
  <img src="docs/assets/icon.png" width="128" height="128" alt="React Native Simulator icon">
</p>

# React Native Simulator

**This project is implemented entirely by AI.** Its purpose is to run React
Native applications without Android or iOS physical devices, emulators, or
simulators. Removing that dependency reduces friction in the React Native AI
agent loop, so development and optimization iterations can run faster and more
smoothly.

React Native Simulator is an experimental native React Native runtime and
renderer for macOS. It runs caller-provided React Native applications with
Hermes, ReactInstance, RuntimeScheduler, Fabric, Yoga, and Skia, without an iOS
Simulator, Android Emulator, or mobile client process.

![React Native Simulator running RN Tester interactively](docs/assets/rnsim-interactive.png)

## Status

The project is in Phase 4 of the accepted architecture plan: the engine,
retained scene, Skia renderer, and same-process interactive frontend are
connected, and Android RN 0.87 behavior is being certified against RN Tester.
It is an Android-first experimental preview, not a claim of complete Android
or iOS equivalence. Public conformance verdicts are intentionally disabled in
v0.1.0 until the canonical profile, font, and oracle manifests are complete.

Current pinned runtime:

- React Native 0.87.0 (`4bc2473f5d0233ea5384c1ef24f6a55615de2220`)
- Hermes v1 `260318099.0.1`
- Apple Silicon (`arm64`), macOS 15 or newer

See the [capability baseline](docs/baselines/RN087_CAPABILITY_BASELINE.md) for
implemented, approximated, mocked, and unavailable surfaces. The
[RN Tester baseline](docs/baselines/RNTESTER_BASELINE.md) records the Android
demo and visual certification boundary.

## Quick start

The supported v0.1.0 path is intentionally narrow: Apple Silicon, macOS 15 or
newer, React Native 0.87.0, and the Android profile. Check the
[capability baseline](docs/baselines/RN087_CAPABILITY_BASELINE.md) before using
an application with platform or third-party native modules; unsupported native
contracts require an explicit addon and never become silent mocks.

### Install once

Download the runtime archive and its adjacent checksum from the
[v0.1.0 GitHub Release](https://github.com/aneryu/react-native-simulator/releases/tag/v0.1.0).
If that page does not list the assets below, no supported binary distribution
has been published yet; use [Build from source](#build-from-source) instead.

```sh
shasum -a 256 -c rnsim-v0.1.0-macos-arm64.tar.gz.sha256
tar xf rnsim-v0.1.0-macos-arm64.tar.gz
./rnsim/install.sh
export PATH="$HOME/.local/bin:$PATH"
rnsim --version
```

The installer removes quarantine after confirmation, copies the complete tree
to `~/.local/lib/react-native-simulator/0.1.0`, and links `rnsim` into
`~/.local/bin`. It does not use sudo or edit shell configuration. The runtime is
self-contained and does not require a React Native checkout, Node.js, npm, or
Homebrew.

### Run your React Native app

From the root of an RN 0.87 app, keep Metro in one terminal:

```sh
npm start
# or: yarn start
```

Then open another terminal in the same app directory:

```sh
rnsim
```

The default target is Android. `rnsim` opens the host window immediately while
it connects to Metro on localhost:8081; closing the window cancels the wait. It
reads the standard project entry and `app.json`, and automatically runs the
configured or only registered AppRegistry application. It opens the Pages
chooser only when multiple application keys require a decision. Save a
component to exercise Fast Refresh; Metro's `r` command requests an in-process
reload while the interactive window stays open. If initial evaluation fails,
the error appears in the Pages log and the session remains recoverable: fix the
bundle, then click **Reload** or press `r` in Metro.

`rnsim` deliberately does not launch Metro or own the caller's Babel,
TypeScript, or bundle configuration. See
[running your own app](docs/guides/GETTING_STARTED.md) for non-standard Metro
ports, offline bundles, local configuration, addons, and troubleshooting.

### Explore RN Tester

If you do not have an RN 0.87 app ready, the optional RN Tester support package
contains a caller-built HBC, assets, and an isolated application addon:

```sh
shasum -a 256 -c rnsim-rntester-demo-v0.1.0-macos-arm64.tar.gz.sha256
tar xf rnsim-rntester-demo-v0.1.0-macos-arm64.tar.gz
xattr -dr com.apple.quarantine rnsim-rntester-demo
rnsim --config rnsim-rntester-demo/rnsim.json
```

The compact demo uses host macOS font fallback, so it is a functional tour, not
an Android typography or pixel-certification artifact. Manual version
management, Gatekeeper details, DevTools usage, and dependency
provenance live in the [getting-started guide](docs/guides/GETTING_STARTED.md),
[v0.1.0 release notes](docs/releases/v0.1.0.md), and
[security policy](SECURITY.md), rather than the first-run path.

## Build from source

Requirements:

- Apple Silicon Mac running macOS 15 or newer, with current Xcode Command Line Tools
- CMake 3.22+, Ninja, Python 3, Git, and Git LFS
- Homebrew packages: `boost`, `double-conversion`, `fmt`, `folly`, and `glog`

Use the repository's GitHub **Code** menu to copy its clone URL, then:

```sh
git clone --recurse-submodules REPOSITORY_URL react-native-simulator
cd react-native-simulator
git lfs pull

brew install cmake ninja boost double-conversion fmt folly glog
cmake/bootstrap-skia-macos.sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The Skia bootstrap currently supports Apple Silicon only. A complete checkout
and build needs approximately 16 GB for the pinned RN/Skia dependency trees.
The measured Release tree is about 0.4 GB; the instrumented sanitizer tree is
about 4.9 GB. Core
configure/build/test/runtime paths do not invoke Node.js; Node is only used by
optional RN Tester, diagnostics, and benchmark tooling.

If Git LFS is absent, the visual-baseline PNGs remain pointer files. This does
not block the core build, but visual galleries and comparison evidence will be
incomplete.

The production executable is `build/release/runtime/rnsim`. Install the CLI,
shared engine, headers, and CMake package with:

```sh
cmake --install build/release \
  --prefix /absolute/install/prefix \
  --component react-native-simulator
```

External CMake projects can use
`find_package(ReactNativeSimulator 0.1.0 EXACT CONFIG REQUIRED)` and link
`ReactNativeSimulator::Engine`.
The installed CMake package supports the embedding Engine API. Native addon
authoring still requires the exact source checkout and pinned RN/Hermes headers;
the compact archive is not a standalone addon SDK.

## Advanced runtime usage

The examples below use an installed `rnsim`; source builds can substitute
`build/release/runtime/rnsim`. Offline, supply a caller-built bundle explicitly:

```sh
rnsim interactive \
  --platform android \
  --app-key MyApp \
  --bundle /absolute/path/to/application.jsbundle
```

`rnsim` also reads `rnsim.json`. Paths are resolved relative to the config file,
and unknown fields are rejected:

```json
{
  "schemaVersion": 1,
  "reactNative": "0.87.0",
  "platform": "android",
  "appKey": "MyApp",
  "bundle": "./dist/application.hbc",
  "viewport": {
    "width": 392.7273,
    "height": 753.4545,
    "pointScaleFactor": 2.75
  },
  "addons": []
}
```

Interactive and headless modes use the same semantic engine. Headless
workloads are strict and finite:

```sh
rnsim headless \
  --bundle /absolute/path/to/workload.hbc \
  --timeout-ms 5000 \
  --require-react-fabric true \
  --require-no-pending-work true \
  --fail-on-component-fallback true
```

These requirements validate one runtime execution; they are not a platform
conformance certificate. `rnsim test`/`rnsim conformance` fail closed in
v0.1.0 instead of producing a misleading pass.

Multiple `--bundle` options load sequentially on one ReactInstance,
RuntimeScheduler, and Hermes VM. JavaScript may also call the Promise-based
`RN$Simulator.loadBundle(path)` API. See
[multi-bundle loading](docs/guides/MULTI_BUNDLE.md) and the
[workload protocol](docs/guides/WORKLOAD_PROTOCOL.md).

## Architecture

```text
caller bundle
    |
Hermes / JSI / ReactInstance / RuntimeScheduler
    |
TurboModules + Fabric ShadowTree + Yoga + EventDispatcher
    |
typed retained scene + cached Skia prepared paragraphs
    |
interactive frontend | headless screenshots | future conformance oracle
```

The frontend never duplicates React Native semantics. SDL3 and Dear ImGui host
the macOS shell; Skia paints the retained RN device scene. Pointer, scroll, key,
and committed-text actions cross a bounded typed queue and are dispatched back
through RN event contracts.

Application and third-party contracts live in isolated addons. The versioned RN
profiles own upstream framework contracts. See
[profiles and addon implementation guidance](docs/guides/ADDONS.md).

## Verification and release assets

```sh
ctest --preset release
ctest --preset sanitized

node tools/diagnostics/verify-runtime.mjs
node tools/diagnostics/verify-addons.mjs

node tools/rntester/bundle.mjs \
  --dev false --out-dir build/release-rntester --build-dir build/release
tools/release/package-macos.sh build/release dist
tools/release/package-rntester-demo.sh \
  build/release build/release-rntester dist
tools/release/verify-release.sh dist
```

The scripts create independent runtime and RN Tester support assets plus their
SHA-256 files under `dist/`. Runtime packaging recursively vendors non-system
dylibs, rewrites Mach-O install names, strips local symbols without removing
embedding exports, ad-hoc signs the final bytes, and fails on absolute
dependencies/rpaths or application-contract leakage. Demo packaging compiles
HBC with the same Hermes tree, installs the versioned dynamic addon, records a
compatibility manifest, and does not duplicate the runtime.
Official package scripts reject dirty source trees and mismatched build
commits, include SPDX SBOMs and complete third-party license inventories, fix
the deployment target at macOS 15, reject author-machine paths, and create
reproducible archives.

## Non-goals

- owning Metro, Babel, TypeScript, or an application bundle pipeline;
- replacing the caller's application with a repository sample;
- claiming mocks, placeholders, or macOS adapters are Android/iOS equivalent;
- maintaining separate semantic engines for GUI, headless, and conformance;
- becoming a standalone Hermes runner or benchmark-only product.

The product boundary is defined in
[SIMULATOR_DESIGN.md](docs/design/SIMULATOR_DESIGN.md); future certification
work is tracked in [ROADMAP.md](ROADMAP.md).

## Community

Read [CONTRIBUTING.md](CONTRIBUTING.md) before proposing a change. Usage help is
covered by [SUPPORT.md](SUPPORT.md), vulnerabilities by
[SECURITY.md](SECURITY.md), and participation by
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

## License

React Native Simulator is available under the MIT License. See [LICENSE](LICENSE)
and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for upstream attribution.
Documentation starts at [docs/README.md](docs/README.md); common failures are
covered by [Troubleshooting](docs/guides/TROUBLESHOOTING.md).
