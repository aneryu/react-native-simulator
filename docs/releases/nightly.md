# React Native Simulator Nightly

Nightly is the single rolling preview of React Native Simulator. Each update
replaces the assets on this release; old Nightly binaries are not retained.
Use `rnsim --version --json` to record the exact Git commit represented by an
installed build.

## Install the runtime and optional RN Tester demo

Download each desired asset with its adjacent `.sha256` file:

```sh
shasum -a 256 -c rnsim-nightly-macos-arm64.tar.gz.sha256
tar xf rnsim-nightly-macos-arm64.tar.gz
./rnsim/install.sh

shasum -a 256 -c rnsim-rntester-demo-nightly-macos-arm64.tar.gz.sha256
tar xf rnsim-rntester-demo-nightly-macos-arm64.tar.gz
xattr -dr com.apple.quarantine rnsim-rntester-demo
rnsim --config rnsim-rntester-demo/rnsim.json
```

The installer replaces the currently installed Nightly after confirmation. The
runtime and RN Tester demo remain separate packages; the demo owns its caller
bundle and application addon.

## Current scope

- React Native 0.87.0 and Hermes v1 `260318099.0.1` are pinned.
- Android is the first certification profile.
- Interactive and headless frontends share one semantic engine.
- Public conformance commands fail closed.
- The release targets Apple Silicon and macOS 15 or newer.
- Packaged Mach-O files are ad-hoc signed, not Developer ID signed or notarized.
- Capability limitations, mocks, host adapters, and macOS font fallback remain
  explicit in the capability baseline and packaged manifests.

Every update must pass the release and sanitizer suites, runtime/addon isolation,
fresh-extraction verification, RN Tester startup, interactive Skia smoke, license
collection, SBOM generation, signature verification, and reproducible packaging.
