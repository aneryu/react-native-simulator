# Troubleshooting

Start with:

```sh
rnsim --version
rnsim doctor --json
```

The doctor output contains no bundle contents, but its executable and config
paths can identify local projects. Redact those paths before posting publicly.

## “bad CPU type”, “requires a newer version of macOS”, or immediate launch failure

The v0.1.0 binary release supports Apple Silicon (`arm64`) and macOS 15 or
newer. Source builds may choose another deployment target only if the complete
dependency stack is rebuilt and verified there.

## Gatekeeper or quarantine blocks the downloaded asset

Verify the adjacent checksum first:

```sh
shasum -a 256 -c rnsim-v0.1.0-macos-arm64.tar.gz.sha256
```

The v0.1.0 alpha is ad-hoc signed, not Developer ID signed or notarized. The
runtime installer asks before removing quarantine. For the optional demo,
remove quarantine only after verifying its checksum:

```sh
xattr -dr com.apple.quarantine rnsim-rntester-demo
```

## `rnsim` is not found

The default installer links the command into `~/.local/bin`. Add that directory
to PATH, or invoke the absolute path printed by the installer.

## Metro times out

`rnsim` never starts Metro. From the caller's RN 0.87 project, start Metro with
its own package-manager command, then verify:

```sh
curl http://localhost:8081/status
```

The response must contain `packager-status:running`. Only loopback HTTP bundle
URLs are accepted. Use `--url` when Metro uses a non-default path or port.

## RN/Hermes or bytecode mismatch

The v0.1.0 host supports React Native 0.87.0 and Hermes
`260318099.0.1`. The runtime archive intentionally does not ship `hermesc`.
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
This experimental release does not certify arbitrary RN applications. Run a
finite diagnostic workload with explicit requirements when appropriate:

```sh
rnsim headless --bundle app.jsbundle \
  --require-react-fabric true \
  --require-no-pending-work true \
  --fail-on-component-fallback true
```

This is strict runtime validation, not a conformance verdict.

## DevTools frontend is missing

The compact release contains the Inspector/CDP backend but not the React Native
DevTools web frontend. Supply the frontend explicitly with
`--devtools-frontend-dir DIR` or `RNS_DEVTOOLS_FRONTEND_DIR`. It must come from
the pinned RN 0.87 checkout or another frontend you trust.

## JavaScript error is only visible in the terminal

v0.1.0 reports JavaScript errors and stacks to stderr. It does not yet provide
an in-window LogBox/redbox. Capture stderr together with the redacted doctor
output when reporting a problem.
