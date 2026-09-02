# Troubleshooting

Start with:

```sh
rnsim --version
rnsim doctor
# Machine-readable form:
rnsim doctor --json
```

The doctor output contains no bundle contents, but its executable and config
paths can identify local projects. Redact those paths before posting publicly.

## “bad CPU type”, “requires a newer version of macOS”, or immediate launch failure

The Nightly binary contract targets Apple Silicon (`arm64`) and macOS 15 or newer.
Source builds may choose another deployment target only if the complete
dependency stack is rebuilt and verified there.

## Gatekeeper or quarantine blocks the downloaded asset

Re-run the installer, which verifies the adjacent checksum, Developer ID
signature, and stapled notarization ticket before copying the executable:

```sh
curl -fsSL https://raw.githubusercontent.com/aneryu/react-native-simulator/main/install.sh | sh
```

The supported installer never bypasses Gatekeeper or removes quarantine. A
failure here means the download, signature, notarization, or local trust state
must be diagnosed; do not work around it with `xattr`.

## `rnsim` is not found

The default installer links the command into `~/.local/bin`. Add that directory
to PATH, or invoke the absolute path printed by the installer.

## Metro times out or the wrong project is running

`rnsim` never starts Metro. From the caller's RN 0.87 project, start Metro with
its own package-manager command, then run doctor from that same project root:

```sh
npm start
# In another terminal, from the same directory:
rnsim doctor
```

Metro's `/status` only proves that some packager is reachable; it does not prove
which project owns port 8081. Doctor uses a sentinel bundle request to compare
Metro's canonical project root with the current directory. This is diagnostic:
interactive launch loads the caller-selected Metro source even when the roots
differ or cannot be verified.

If the diagnostic reports `metro-project-mismatch`, the report includes both
expected and actual paths so an unexpected bundle source is easy to identify. A
`metro-project-unverified` result means Metro was reachable but did not return
usable project-root evidence. Neither identity result blocks launch.

Doctor is a report command and returns exit code 0 for a completed mismatch
report. Scripts must read `project.readyToLaunch` from `rnsim doctor --json`
rather than using the process exit code as the readiness signal.

Only loopback HTTP bundle URLs are accepted. For a non-default path or port,
diagnose the complete URL first, then use the same value for launch:

```sh
rnsim doctor --url \
  'http://localhost:8082/index.bundle?platform=android&dev=true&minify=false'
rnsim --url \
  'http://localhost:8082/index.bundle?platform=android&dev=true&minify=false'
```

Because `--url` supplies the complete bundle source, this check does not require
a conventional local `index.js`. Doctor still verifies the RN project metadata
and reports Metro identity when available.

If `rnsim.json` selects a local bundle that is missing or is a directory,
doctor reports `missing-configured-bundle` and does not fall back to Metro;
repair the path or override the source explicitly with `--url`/`--bundle`.

## RN/Hermes or bytecode mismatch

The current Nightly host supports React Native 0.87.0 and Hermes
`260318099.0.1`. The one-file Nightly intentionally does not ship `hermesc`.
Binary users should load a caller-built source `.jsbundle`; compiling HBC
requires `build/release/bin/hermesc` from the exact source revision. Never mix
HBC produced by another Hermes revision.

## Addon fails to load

Compare `rnsim --version --json` with the addon's build metadata. The addon must
use ABI 2, RN 0.87.0, Hermes `260318099.0.1`, arm64, and the matching engine
major/minor release. Application-specific native contracts belong in an addon,
not in the framework provider.

## Missing UI or “fallback component” diagnostics

Consult the [RN 0.87 capability baseline](../baselines/RN087_CAPABILITY_BASELINE.md).
Several platform components are explicitly adapted, layout-only, or unavailable.
`RNCSafeAreaProvider` is temporarily host-adapted so
`react-native-safe-area-context` can render children. Root insets are 0 because
host notch/nav chrome sits outside the RN window; other third-party components
still need an explicit addon.
This experimental release does not certify arbitrary RN applications. Run a
finite diagnostic workload with explicit requirements when appropriate:

```sh
rnsim headless --bundle app.jsbundle \
  --require-react-fabric true \
  --require-no-pending-work true \
  --fail-on-component-fallback true
```

This is strict runtime validation, not a conformance verdict.

## DevTools frontend does not open

The one-file Nightly includes the Inspector/CDP backend but does not embed the
React Native DevTools web frontend. `installedDevToolsFrontend` is therefore
false unless a trusted external frontend directory is configured.

Source builds do not silently read files from an author checkout. Configure
with `-DRNS_ENABLE_SOURCE_DEVTOOLS_FRONTEND=ON`, or provide a trusted frontend
explicitly with `--devtools-frontend-dir DIR` or
`RNS_DEVTOOLS_FRONTEND_DIR`. See [React Native DevTools](DEVTOOLS.md).

## JavaScript or preparation error

Engine and JavaScript errors remain observable on stderr, but they are not
terminal-only. The live window opens the App panel and its copyable log for
preparation, runtime, rendering, input, and AppRegistry failures. This is host
diagnostic chrome, not an emulated LogBox/redbox overlay.

If the initial bundle reached the Engine, fix the source and use **Reload**,
`⌘R`, or Metro's `r`; the window stays open while a fresh
Hermes/ReactInstance is created and the current application is restarted. If
preparation failed before the first bundle loaded, correct the Metro/bundle
problem and use **Retry** or `⌘R`. Preparation is transactional and repeats its
remote-source fetch; no fetched remote bundle is queued until every remote
source succeeds. Another Retry request is ignored while that attempt is in
flight. Reload is enabled only while the Engine is running, paused after an
error, or waiting for an application choice.

For issue reports, copy the App log and include redacted `rnsim doctor --json`
output. Embedding clients can also query `Engine::runtimeStatus()` for structured
lifecycle, HMR, diagnostic stack, and observed native-module plus mounted
component capability-usage data. The toolbar renders the same current-generation
phase and HMR state; the App panel renders structured errors and stacks plus
observed capabilities or degradations.
