# Run your own app on `rnsim`

React Native Simulator runs caller-built React Native 0.87 applications on
macOS. Within the documented Android capability baseline, it can replace the
Android Emulator for part of the edit, run, inspect, and diagnose loop. It does
not emulate Android OS services or replace real-device, OEM, native-integration,
or release validation.

The host does not own Metro, Babel, TypeScript, or a production bundle pipeline.
You build the JavaScript; `rnsim` loads it.

The Nightly binary contract targets Apple Silicon and macOS 15 or newer. It is an
Android-first experimental preview, not a sandbox or a platform conformance
certificate. The iOS profile remains unverified experimental work. Nightly
builds expose their exact Git commit; see the
[versioning policy](../design/VERSIONING.md).

## Before you install

- Use an RN 0.87 app and run `rnsim doctor` from its root before launch. Doctor
  compares the installed or declared React Native version with the host contract
  and provides a next action. Skipping that preflight can still leave a wrong
  version to fail later as a native/JavaScript contract error.
- Official RN components and modules are the supported starting point. Provide
  an explicit [addon](ADDONS.md) for application or third-party native
  contracts; unknown modules stay unavailable.
- A normal app registers the name from `app.json` with
  `AppRegistry.registerComponent`. `rnsim` discovers that name automatically.

## Install the runtime

Download the runtime archive and adjacent checksum from the rolling
[Nightly GitHub Release](https://github.com/aneryu/react-native-simulator/releases/tag/nightly),
then install it. If that page does not contain the named assets, no supported
binary distribution has been published yet; build from source instead.

```sh
shasum -a 256 -c rnsim-nightly-macos-arm64.tar.gz.sha256
tar xf rnsim-nightly-macos-arm64.tar.gz
./rnsim/install.sh
export PATH="$HOME/.local/bin:$PATH"
```

When Gatekeeper assessment requires quarantine removal, the installer confirms
trust first. It copies the whole runtime to
`~/.local/lib/react-native-simulator/nightly` and creates
`~/.local/bin/rnsim`. Use `--prefix DIR` to select another user-owned prefix. It
does not use sudo or modify shell startup files. Keep `PREFIX/bin` on PATH.

Confirm the exact runtime contract before loading an application:

```sh
rnsim --version
rnsim doctor
# Machine-readable form:
rnsim doctor --json
```

Run `doctor` from the application root. It reports the declared and installed RN
versions, entry candidates, AppRegistry key, local config, Metro reachability,
the expected Metro project root, and the actual root when Metro provides one. A
sentinel bundle request makes project identity observable, but a mismatch does
not block the caller-selected Metro source. The JSON report includes
`project.metro.projectVerified`, its verification method or error, and one
`project.nextAction`. Doctor is a report command, so automation must inspect
`project.readyToLaunch` rather than treating exit code 0 as launch readiness.

For a non-default Metro port, pass the complete bundle URL to doctor:

```sh
rnsim doctor --url \
  'http://localhost:8082/index.bundle?platform=android&dev=true&minify=false'
```

An explicit URL is the complete selected bundle source, so doctor can validate
it without requiring a conventional local `index.js`. The RN project metadata
is still required for doctor readiness; Metro project identity remains
diagnostic.

Nightly packaging ad-hoc signs Mach-O files for Apple Silicon; those
artifacts are not Developer ID signed or notarized. Verifying SHA-256 and, when
requested, accepting the installer's quarantine-removal prompt are therefore
explicit trust steps.

### Update Nightly

Run the installer from the newest verified archive. It replaces the existing
`nightly` installation after confirmation and updates the managed `current`
link. Old Nightly binaries are not retained. The installer refuses to overwrite
a `PREFIX/bin/rnsim` it does not manage.

## Local development (Fast Refresh)

From the app directory, start Metro in one terminal and `rnsim` in another.
Order does not matter: the interactive window opens first and Device shows its
loading state while the runtime waits up to 60s for Metro on `localhost:8081`.
Closing the window cancels both that wait and an in-flight loopback bundle or
project-probe request.

```sh
npm start
# or: yarn start
```

```sh
rnsim
```

`rnsim` does not start Metro. On the default no-argument path, it probes
`http://localhost:8081/status` and loads the selected bundle without requiring
Metro's project root to match the launch directory. If Metro has no `./index`,
`rnsim` tries entry files discovered from the project path Metro reports
(`app.json` name, `package.json` main, `src/index`, and
`js/*.android.js` / `js/*.ios.js`).

`app.json`'s `name` is used as `--app-key` when you do not pass one. After the
bundle registers that key, `rnsim` runs it automatically. A bundle with one
non-LogBox AppRegistry key is also run automatically; only an ambiguous
multi-key bundle waits in the App chooser.

`--platform ios` changes the Metro `platform` query, but the iOS profile remains
unverified. `--url` is an explicit caller-selected complete bundle URL. The
launch path treats both the default URL and an explicit URL as caller-selected
sources; neither is blocked by project-root identity. Diagnose a custom source
with `rnsim doctor --url URL`, then pass the same URL to `rnsim --url URL`. Use
`--app-key` when the application name is not available from `app.json` or
several keys are registered.

The interactive workspace keeps the application in front: Device fills the
window by default. An ambiguous AppRegistry choice or blocking error forces the
App panel open. A newly observed missing-module or fallback-component warning
opens it once but does not prevent you from closing it; the toolbar can open it
again. **Interact** sends normal input only while the Engine phase is `Running`;
leaving that phase cancels an active pointer rather than queueing work into a
paused or reloading runtime. **Inspect** (`⌘2`) opens the ShadowTree picker,
cancels any active application pointer, and isolates normal canvas pointer,
keyboard, and TextInput dispatch. `⌘1`, `⌘2`, `⌘R`, and Inspect Escape remain
available. Toolbar lifecycle and Fast Refresh chips come from the Engine's
current structured status rather than an inferred `live` flag.

Save a file in your app. Metro sends a Fast Refresh update over `/hot`. Component
state is preserved when the change is compatible. Export-shape changes fall back
to an in-process reload: the interactive window stays open, the Hermes VM is
replaced, and CLI/config bundles are fetched again. Metro's `r` key sends the
same reload over `ws://host:port/message`.

If Metro never answers `/status` or the bundle fetch fails, preparation fails
closed. The window remains open, the App panel shows the diagnostic, and stderr
retains the same failure instead of exiting before a window exists. A detected
project-root mismatch remains a non-blocking doctor diagnostic.

JavaScript errors print to stderr and open the App log. The interactive
runtime pauses instead of closing. Once the initial bundle has loaded, fix the
source and use **Reload**, `⌘R`, or Metro's `r` to create a fresh
Hermes/ReactInstance in the same window and restart the current application.
If preparation failed before the Engine loaded its first bundle, **Reload** is
replaced by **Retry**: correct the reported problem and click it or press `⌘R`.
Another Retry is ignored while that attempt is in flight. Preparation fetches
and verifies every remote source before queuing any bundle, so a failed remote
attempt can run again in the same window without leaving its fetched bundle in
the Engine. Reload is enabled only while the Engine is running, paused after an
error, or waiting for an application choice. This is host diagnostics, not an
emulated in-window LogBox or Android redbox overlay.

The public embedding API exposes the same lifecycle separately from the App
log. `Engine::runtimeStatus()` returns a thread-safe snapshot containing the
runtime generation and phase, HMR state/error, structured JavaScript and
application diagnostics (including JavaScript stack frames), and observed
native-module plus mounted official, addon, or fallback-component capability
usage. The live interface consumes that snapshot: the toolbar renders lifecycle
and HMR state, while the App panel renders structured errors and stacks plus
capabilities or degradations encountered by the running application.

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

When `rnsim.json` points to an existing local bundle, doctor reports Metro as
not required and skips Metro project-root verification. The local config and
bundle path remain part of the caller-owned offline contract. If that selected
path is missing or is not a regular file, doctor reports
`missing-configured-bundle`; it does not silently fall back to Metro.

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
