# Multi-bundle loading

React Native Simulator can sequentially execute multiple source or Hermes HBC
bundles in one ReactInstance, Hermes heap, RuntimeScheduler, TurboModule
registry, and Fabric UIManager. Later bundles can read globals and modules
created by earlier bundles.

## Startup bundles

```sh
rnsim headless \
  --bundle /absolute/path/bootstrap.hbc \
  --bundle /absolute/path/application.hbc \
  --bundle /absolute/path/workload.hbc
```

Every `--bundle` is loaded in command-line order through
`ReactInstance::loadScript()`. The host never bypasses RuntimeScheduler to call
Hermes evaluation directly. CLI bundle options replace the bundle list from
`rnsim.json`.

## Runtime API

The host installs a Promise-based API:

```js
await RN$Simulator.loadBundle('/absolute/path/feature.hbc');
```

`loadBundle()` queues a host request. After the current JS task returns, the
host reads the file and schedules evaluation on the same ReactInstance.
Promise resolution or rejection returns through RuntimeScheduler so `.then`,
`await`, and microtask ordering remain consistent.

The path currently resolves relative to the `rnsim` process working directory.
Production hosts should use absolute paths and enforce an allowlist before
exposing this API to untrusted code.

## Completion and errors

- Startup bundles always finish loading in order, even if an earlier bundle
  has already called the workload `complete()` signal.
- Runtime requests are processed while the workload loop is active and may
  enqueue additional requests.
- Missing files, HBC evaluation errors, and global timeout expiry reject the
  Promise and cause a nonzero process result.
- `RN$SimulatorWorkload.complete()` completes the overall caller workload, not
  an individual bundle.

Metrics include total `bundleBytes`, an aggregate hash, and one `bundles[]`
record per request with path, hash, byte count, source, load state, timing, and
error information.

## Boundaries

- Bundles share globals; collision policy belongs to their author.
- Bundles cannot be unloaded and evaluated side effects are not rolled back.
- This is ordered full-bundle loading, not Metro RAM-bundle segment loading.
- Concurrent requests are serialized in host queue order.
- Runtime loading provides local file-read capability and is not a sandbox.
- Reload replays CLI/config bundles; bundles loaded dynamically at runtime are
  not automatically replayed.
