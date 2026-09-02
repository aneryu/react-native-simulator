# Security policy

## Supported version

Only the current rolling `nightly` release receives security fixes. Each
publication replaces the previous Nightly; old binary releases are unsupported
and are not retained by the project.

## Trust boundary

React Native Simulator is **not a security sandbox**. A caller bundle executes
inside the native `rnsim` process with the current user's permissions. A native
addon is loaded with `dlopen` and has the same process, filesystem, and network
access as the host. Only run bundles, addons, configuration files, and DevTools
frontends that you trust.

The production host accepts local bundle files and loopback Metro URLs. Runtime
bundle loading can read paths available to the process. Addons should validate
any application-specific file allowlist themselves.

React Native DevTools is disabled by default. When enabled, its HTTP/CDP server
binds only to `127.0.0.1`; each process uses a random session token and rejects
WebSocket upgrades with a missing token, an invalid Host header, or a foreign
browser Origin. This protects the browser-to-localhost boundary, but it cannot
protect against another process already running as the same user.

## Binary distribution

Nightly assets target Apple Silicon with a declared minimum of
macOS 15. The self-contained `rnsim` executable is Developer ID signed with
Hardened Runtime. Its one-file DMG is signed, notarized, and stapled. The
installer verifies the adjacent SHA-256 file, signatures, Gatekeeper assessment,
notarization ticket, and DMG contents; it never removes quarantine.

The executable reports the exact Git commit and clean source state through
`rnsim --version --json`. Dependency notices and license texts remain in the
repository at the exact commit identified by the binary and are linked from the
Nightly release notes.

## Reporting a vulnerability

Do not open a public issue containing exploit details or private data. After the
GitHub repository is initialized, use its private Security Advisory reporting
form. Until then, the repository owner must configure a private security contact
before publishing the first release.

Include the output of `rnsim doctor --json`, with local paths redacted if they
identify private projects, plus the smallest trusted reproduction. Never attach
proprietary bundles or native addons to a public report.
