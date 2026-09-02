# Run your own app on `rnsim`

React Native Simulator runs caller-built React Native 0.87 applications on
macOS. The host does not own Metro, Babel, TypeScript, or a production bundle
pipeline. You build the JavaScript; `rnsim` loads it.

The v0.1.0 binary supports Apple Silicon and macOS 15 or newer. It is an
Android-first experimental preview, not a sandbox or a platform conformance
certificate. The iOS profile remains unverified experimental work.

## Before you install

- Use an RN 0.87 app. The host does not sniff other versions or emit a friendly
  mismatch UI yet; a wrong React Native version fails as a native/JS contract
  error.
- Official RN components and modules are the supported starting point. Provide
  an explicit [addon](ADDONS.md) for application or third-party native
  contracts; unknown modules stay unavailable.
- A normal app registers the name from `app.json` with
  `AppRegistry.registerComponent`. `rnsim` discovers that name automatically.

## Install the runtime

Download the runtime archive and adjacent checksum from the
[v0.1.0 GitHub Release](https://github.com/aneryu/react-native-simulator/releases/tag/v0.1.0),
then install it. If that page does not contain the named assets, no supported
binary distribution has been published yet; build from source instead.

```sh
shasum -a 256 -c rnsim-v0.1.0-macos-arm64.tar.gz.sha256
tar xf rnsim-v0.1.0-macos-arm64.tar.gz
./rnsim/install.sh
export PATH="$HOME/.local/bin:$PATH"
```

The installer confirms trust before removing quarantine, copies the whole
runtime to `~/.local/lib/react-native-simulator/0.1.0`, and creates
`~/.local/bin/rnsim`. Use `--prefix DIR` to select another user-owned prefix.
It does not use sudo or modify shell startup files. Keep `PREFIX/bin` on PATH.

Confirm the exact runtime contract before loading an application:

```sh
rnsim --version
rnsim doctor --json
```

Run `doctor` from the application root. It reports the declared/installed RN
version, entry candidates, AppRegistry key, local config, and whether Metro is
reachable. Metro `/status` does not identify which project it serves, so project
ownership is intentionally verified only when `rnsim` loads the bundle.

Packaged Mach-O files are ad-hoc signed for Apple Silicon, but this experimental
release is not Developer ID signed or notarized. Verifying SHA-256 and accepting
the installer's quarantine-removal prompt are therefore explicit trust steps.

### Manage installed versions

Keep the extracted installer when you need to manage this release. Use
`--reinstall` to replace exactly v0.1.0, `--activate 0.1.0` to switch the managed
`current` link, and `--uninstall 0.1.0` to remove only that installed version.
The installer refuses to overwrite a `PREFIX/bin/rnsim` it does not manage.

## Local development (Fast Refresh)

From the app directory, start Metro in one terminal and `rnsim` in another.
Order does not matter: the interactive window opens first and Pages shows its
loading state while the runtime waits up to 60s for Metro on `localhost:8081`.
Closing the window cancels the wait.

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
`app.json`'s `name` is used as `--app-key` when you do not pass one. After the
bundle registers that key, `rnsim` runs it automatically. A bundle with one
non-LogBox AppRegistry key is also run automatically; only an ambiguous
multi-key bundle waits in the Pages chooser.

`--platform ios` changes the Metro `platform` query, but the iOS profile remains
unverified. `--url` replaces the complete bundle URL. Use `--app-key` when the
application name is not available from `app.json` or several keys are
registered.

Save a file in your app. Metro sends a Fast Refresh update over `/hot`. Component
state is preserved when the change is compatible. Export-shape changes fall back
to an in-process reload: the interactive window stays open, the Hermes VM is
replaced, and CLI/config bundles are fetched again. Metro's `r` key sends the
same reload over `ws://host:port/message`.

If Metro never answers `/status`, `rnsim` times out and tells you to start
Metro or pass `--url`/`--bundle`; the preparation error remains visible in the
Pages log instead of failing before a window exists.

JavaScript errors print to stderr and appear in the Pages log. The interactive
runtime pauses instead of closing, so after fixing the source you can click
**Reload** or press `r` in Metro to create a fresh Hermes/ReactInstance in the
same window. This is host diagnostics, not an emulated in-window LogBox or
Android redbox overlay.

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
packager connection. The toolbar's **Reload** action and reload from
`DevSettings` are interactive-only and re-read the local files.

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
