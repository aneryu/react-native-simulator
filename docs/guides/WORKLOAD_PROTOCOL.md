# Workload protocol

Caller bundles use host-injected globals to declare the real asynchronous
measurement boundary. The host does not treat the initial bundle return or a
fixed drain interval as completion.

## Input

```js
const {iterations, seed, name} = RN$SimulatorWorkload.config;
```

- `iterations` is the caller workload size.
- `seed` must control reproducible input, not just label output.
- `name` is caller-owned metadata; the runtime does not assign semantics to it.

## Lifecycle

```js
RN$SimulatorWorkload.ready();

// Schedule and await all measured JS/native/Fabric work.

globalThis.RN$SimulatorWorkloadResult = {
  iterations,
  checksum,
  callerDefinedField,
};
RN$SimulatorWorkload.complete();
```

Rules:

1. Call `ready()` once after initialization proves the workload can run.
2. Call `complete()` only after every measured timer, microtask, native
   callback, event, and Fabric operation is complete.
3. Write `RN$SimulatorWorkloadResult` before `complete()`.
4. Missing completion fails after `--timeout-ms`.
5. `--settle-ms` is a post-completion observation window, never a substitute
   for an explicit signal.
6. Pending tasks at completion produce `pendingWork:true`; the benchmark runner
   rejects the sample.
7. Fabric workloads register every owned surface with
   `registerRootTag(rootTag)`. The host stops those surfaces and drains ready
   work before final metrics.
8. `mark(name)` emits an instant metrics/trace event without changing
   completion state.

The host advances RuntimeScheduler, TimerManager, microtasks, and native event
callbacks until completion, JS failure, a requirement failure, or timeout.

## Run and output

```sh
rnsim headless \
  --bundle /absolute/path/workload.hbc \
  --iterations 1000 \
  --seed 42 \
  --timeout-ms 5000 \
  --require-no-pending-work true \
  --output result.json \
  --trace trace.json
```

The final stdout line and `--output` file contain host JSON. A zero exit status
means that generic runtime gates passed: bundle loading, lifecycle,
RuntimeScheduler, host Fabric/Yoga checks, requirements, and JS error state.
Caller-specific result fields must still be validated by the external harness.

The trace uses Chrome Trace JSON and shares the same monotonic measurement
boundary as metrics.

## Benchmark runner

```sh
node tools/benchmark/run-benchmark.mjs \
  --binary build/release/runtime/rnsim \
  --bundle /absolute/path/workload.hbc \
  --warmups 3 \
  --runs 20 \
  --output benchmark.json
```

Every warmup and sample launches an isolated process. Reports retain raw samples
and distribution statistics for render, CPU, commit, layout, diff,
initialization, and bundle/drain time, plus Hermes heap/GC, RSS, and process CPU.

For cross-version comparison:

```sh
node tools/benchmark/compare-versions.mjs \
  --baseline-binary /path/to/baseline/rnsim \
  --baseline-bundle /path/to/baseline/workload.hbc \
  --candidate-binary /path/to/candidate/rnsim \
  --candidate-bundle /path/to/candidate/workload.hbc \
  --runs 20 \
  --output comparison.json
```

`runs` must be even. The runner balances independent processes in ABBA/BAAB
order and preserves the true execution order. Fix the machine, power state,
parameters, and seed. Each binary must use its matching caller bundle; mixing a
historical bundle with a current host is not a version comparison.
