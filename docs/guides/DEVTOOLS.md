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
- `--devtools-open URL`
- `--devtools-frontend-dir DIR`
- `--devtools-shell PATH`

The v0.1.0 runtime archive includes the frontend pinned with RN 0.87, so binary
users can run `rnsim --devtools` without a React Native checkout. Explicit
`--devtools-frontend-dir` or `RNS_DEVTOOLS_FRONTEND_DIR` overrides remain for
diagnostics. Source builds do not silently use author-machine files; opt into
checkout fallback with `-DRNS_ENABLE_SOURCE_DEVTOOLS_FRONTEND=ON`.

## Reload behavior

The HostTarget lives for the interactive process. Fast Refresh-compatible
changes update the current runtime. A full reload from `DevSettings.reload` or
Metro's `r` command destroys the Hermes VM and creates a new InstanceTarget.
Console history, breakpoints, object references, and timers belong to the old
InstanceTarget and are not retained.

Local `--bundle` and HBC paths do not enable HMR or PackagerConnection. Headless
workloads keep their finite measurement boundary even when DevTools is enabled.

`wait-for-debugger` pauses after ReactInstance initialization and before the
first external bundle, allowing the debugger domain to observe script parsing
and `debugger;` statements.

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
