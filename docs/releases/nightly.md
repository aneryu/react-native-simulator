# React Native Simulator Nightly

Nightly is the single rolling preview. Every update replaces the assets on this
release; old Nightly binaries are not retained. Record `rnsim --version --json`
when reporting an issue so the exact source commit remains identifiable.

## Install

```sh
curl -fsSL https://raw.githubusercontent.com/aneryu/react-native-simulator/main/install.sh | sh
export PATH="$HOME/.local/bin:$PATH"
rnsim --version
```

The installer downloads `rnsim-nightly-macos-arm64.dmg` and its SHA-256 file,
then verifies the checksum, Developer ID signatures, Hardened Runtime,
notarization ticket, and Gatekeeper assessment. The DMG contains exactly one
self-contained file named `rnsim` and targets Apple Silicon with macOS 15 or
newer.

RN Tester and GitHub CI artifacts are not published. RN Tester remains a local
caller-built conformance fixture, and GitHub Actions only validates source
changes. Nightly is built, signed, notarized, verified, and published locally.

React Native 0.87.0 and Hermes v1 `260318099.0.1` are pinned. Review the
[capability baseline](../baselines/RN087_CAPABILITY_BASELINE.md),
[third-party notices](https://github.com/aneryu/react-native-simulator/blob/nightly/THIRD_PARTY_NOTICES.md),
and [repository license](https://github.com/aneryu/react-native-simulator/blob/nightly/LICENSE)
at the commit reported by the installed executable.
