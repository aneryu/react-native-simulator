# Run your own app on `rnsim`

React Native Simulator runs caller-built React Native 0.87 applications on
macOS. The host does not own Metro, Babel, TypeScript, or a production bundle
pipeline. You build the JavaScript; `rnsim` loads it.

The v0.1.0 binary supports Apple Silicon and macOS 15 or newer. It is an
Android-first experimental preview, not a sandbox or a platform conformance
certificate. The iOS profile remains unverified experimental work.

## Install the runtime

Download the v0.1.0 runtime archive and its adjacent checksum, then install it:

```sh
shasum -a 256 -c rnsim-v0.1.0-macos-arm64.tar.gz.sha256
tar xf rnsim-v0.1.0-macos-arm64.tar.gz
./rnsim/install.sh
```

The installer confirms trust before removing quarantine, copies the whole
runtime to `~/.local/lib/react-native-simulator/0.1.0`, and creates
`~/.local/bin/rnsim`. Use `--prefix DIR` to select another user-owned prefix.
It does not use sudo or modify shell startup files. Keep `PREFIX/bin` on PATH.

Use `--reinstall` to replace exactly v0.1.0, `--activate 0.1.0` to switch the
managed `current` link, and `--uninstall 0.1.0` to remove only that installed
version. The installer refuses to overwrite a `PREFIX/bin/rnsim` it does not
manage.

Confirm the exact runtime contract before loading an application:

```sh
rnsim --version
rnsim doctor --json
```

Packaged Mach-O files are ad-hoc signed for Apple Silicon, but this experimental
release is not Developer ID signed or notarized. Verifying SHA-256 and accepting
the installer's quarantine-removal prompt are therefore explicit trust steps.

## Application requirements

- An RN 0.87 app. The host does not sniff other versions or emit a friendly
  mismatch UI; a wrong React Native version fails as a native/JS contract error.
- Official RN components and modules only, unless you provide an explicit
  [addon](ADDONS.md). Unknown native modules stay unavailable.
- The AppRegistry key from your app's `app.json` `name` field (or the name you
  pass to `AppRegistry.registerComponent`).

## Local development (Fast Refresh)

From the app directory, start Metro in one terminal and `rnsim` in another.
Order does not matter: interactive mode waits up to 60s for Metro on
`localhost:8081`.

```sh
npm start
# or: yarn start
```

```sh
rnsim
```

`rnsim` does not start Metro. It probes `http://localhost:8081/status`, then
loads `index.bundle`. If Metro has no `./index`, it reads the packager project
path from the 404 payload and tries entry files found there (`app.json` name,
`package.json` main, `src/index`, and `js/*.android.js` / `js/*.ios.js`).
`app.json`'s `name` is used as `--app-key` when you do not pass one.

`--platform ios` changes the Metro `platform` query. `--url` replaces the
complete bundle URL. `--app-key` still preselects the Pages list when the name
is not in `app.json`.

Save a file in your app. Metro sends a Fast Refresh update over `/hot`. Component
state is preserved when the change is compatible. Export-shape changes fall back
to an in-process reload: the interactive window stays open, the Hermes VM is
replaced, and CLI/config bundles are fetched again. Metro's `r` key sends the
same reload over `ws://host:port/message`.

If Metro never answers `/status`, `rnsim` times out and tells you to start
Metro or pass `--url`/`--bundle`.

JavaScript errors print to stderr. There is no in-window LogBox or redbox
overlay.

## Offline bundle

Build a bundle with the RN CLI, then load the file:

```sh
npx react-native bundle \
  --entry-file index.js \
  --platform android \
  --dev false \
  --bundle-output dist/index.android.jsbundle \
  --assets-dest dist/assets
```

```sh
rnsim --app-key MyApp --bundle dist/index.android.jsbundle
```

Hermes bytecode is optional. Compile with the `hermesc` that belongs to this
runtime if you want an `.hbc` file; do not mix bytecode from a different Hermes
revision. The compact binary archive does not ship a compiler. Use a source
checkout's `build/release/bin/hermesc`, or load the source `.jsbundle` directly.

`--bundle` (local source or HBC) does not enable Fast Refresh or the Metro
packager connection. Reload from `DevSettings` is interactive-only and re-reads
the local files.

`rnsim.json` is the local configuration boundary. Paths are resolved relative to
the config file, unknown fields are rejected, and `--bundle` on the CLI replaces
the config bundle list:

```json
{
  "schemaVersion": 1,
  "reactNative": "0.87.0",
  "platform": "android",
  "appKey": "MyApp",
  "bundle": "./dist/index.android.jsbundle"
}
```

```sh
rnsim --config ./rnsim.json
```

## Third-party native modules

Application, company, and third-party native contracts belong in
`runtime/addons/<name>/` and are loaded with `--addon`. The RN framework
provider never registers application-specific module names. Missing modules stay
unavailable rather than becoming silent mocks. See [ADDONS.md](ADDONS.md) and
its TurboModule and Fabric component implementation guidance.

## Related

- [React Native DevTools](DEVTOOLS.md)
- [Multi-bundle loading](MULTI_BUNDLE.md)
- [RN Tester baseline](../baselines/RNTESTER_BASELINE.md)
- [Troubleshooting](TROUBLESHOOTING.md)
- [`rnsim.json` schema](../schema/rnsim.schema.json)
- [Security boundary](../../SECURITY.md)
