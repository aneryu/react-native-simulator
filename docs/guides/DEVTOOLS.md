# React Native DevTools

React Native Simulator exposes the RN 0.87 modern inspector and Hermes CDP
directly. The backend does not require Metro, `@react-native/dev-middleware`,
Node.js, or npm.

## Security boundary

The server binds only to loopback. Each launch generates a random session token
that is embedded in discovery and WebSocket URLs. Connections with a missing or
incorrect token, unexpected Host header, or foreign browser Origin are rejected.

This prevents an ordinary web page from connecting to localhost CDP. It does
not isolate malicious software already running as the same macOS user. Caller
bundles and debugging sessions remain trusted local code.

## Start a session

```sh
rnsim interactive --devtools
```

The default interactive flow waits for Metro on localhost:8081, loads the
discovered bundle, opens a compatible DevTools frontend, and advances the event
loop until the window closes. Override the source with `--url`, `--bundle`, or
`--platform ios`.

`--devtools` enables CDP and opens the frontend. Use `--no-open` to expose the
server without opening a UI. Advanced overrides include:

- `--devtools-port PORT`
- `--devtools-open true|false`
- `--devtools-wait-for-debugger-ms N`
- `--devtools-keep-alive-ms N`
- `--devtools-frontend-dir DIR`
- `--devtools-shell PATH`

The one-file Nightly includes the Inspector/CDP backend but not the RN 0.87 web
frontend directory. Supply a trusted frontend explicitly with
`--devtools-frontend-dir` or `RNS_DEVTOOLS_FRONTEND_DIR`. Source builds do not
silently use author-machine files; opt into checkout fallback with
`-DRNS_ENABLE_SOURCE_DEVTOOLS_FRONTEND=ON`.

The toolbar's **Inspect** mode is not DevTools. It is a same-process ShadowTree
element picker: it opens the tree panel, cancels any active application pointer,
and isolates normal device-canvas pointer, keyboard, and TextInput dispatch while
selecting nodes. Mode/recovery shortcuts and Inspect Escape remain available.
DevTools is the separate opt-in JavaScript/CDP debugger.

## Reload behavior

The HostTarget lives for the interactive process. Fast Refresh-compatible
changes update the current runtime. A full reload from `DevSettings.reload` or
Metro's `r` command destroys the Hermes VM and creates a new InstanceTarget.
Console history, breakpoints, object references, and timers belong to the old
InstanceTarget and are not retained.

Local `--bundle` and HBC paths do not enable HMR or PackagerConnection. Headless
workloads keep their finite measurement boundary even when DevTools is enabled.

`--devtools-wait-for-debugger-ms N` pauses after ReactInstance initialization and
before the first external bundle, allowing the debugger domain to observe script
parsing and `debugger;` statements.

If JavaScript evaluation fails after the initial bundle loads, **Reload**, `⌘R`,
or Metro's `r` replaces the InstanceTarget together with the Hermes VM. A Metro
timeout, bundle-fetch error, or other preparation failure happens before an
InstanceTarget exists; correct the problem and use **Retry** or `⌘R` to repeat
the transactional preparation in the same window. Duplicate Retry requests are
ignored while an attempt is in flight. Reload is enabled only while the Engine
is running, paused after an error, or waiting for an application choice. DevTools
receives its first InstanceTarget only after preparation succeeds and the Engine
starts.

## Host diagnostics

DevTools and host diagnostics are complementary. The App panel opens for live
preparation, runtime, rendering, input, and AppRegistry error strings and keeps a
copyable log; it is not an emulated LogBox/redbox. The public embedding API also
provides `Engine::runtimeStatus()`, a thread-safe, current-generation snapshot of
runtime phase, HMR state/error, structured JavaScript/application diagnostics,
and observed native-module plus mounted official, addon, or fallback-component
capability usage. JavaScript diagnostics include fatal state and file, method,
line, and column stack frames.

The interactive toolbar renders the current phase and HMR state. The App panel
renders structured errors and JavaScript stack frames plus observed capabilities
or degradations; its copyable string log remains available for preparation,
rendering, and input failures outside the Engine snapshot. Embedding clients can
consume the typed fields directly. Reload begins a new generation and clears the
previous generation's diagnostic and capability snapshot.

The HMR `Enabled` state means `HMRClient.setup` succeeded for that generation;
it is not a continuous Metro WebSocket liveness signal.

## Source mapping

For source bundles, the host can return the exact loaded bytes by script ID.
HBC does not contain recoverable original JavaScript. TypeScript/JSX and HBC
debugging therefore require an inline source map or a source-map URL reachable
by DevTools. The simulator does not guess caller source paths or replace the
bundler.

## Backend endpoints

- discovery endpoints return tokenized target metadata;
- `GET /debugger-frontend/*` serves an explicitly installed frontend;
- `WS /cdp?token=...` connects the RN HostTarget, InstanceTarget, and Hermes
  RuntimeTarget.

The bundle pipeline remains caller-owned. The simulator can read an existing
Metro server through its native HTTP client but never starts Metro.
