# Contributing

React Native Simulator is an experimental native runtime. Contributions must
preserve the production `rnsim` entry point, caller-owned bundle boundary, and
the single semantic engine shared by interactive, headless, and conformance
modes.

## Before opening a change

1. Read [SIMULATOR_DESIGN.md](docs/design/SIMULATOR_DESIGN.md) and the relevant
   capability baseline.
2. Keep upstream React Native contracts in versioned profiles. Put application
   and third-party contracts in `runtime/addons/<name>/`.
3. Do not report a placeholder, mock, fallback, or macOS host adapter as Android
   or iOS equivalent behavior.
4. Do not add an application entry point or bundle pipeline to the core build.

## Build and test

```sh
cmake/bootstrap-skia-macos.sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Run the narrowest relevant smoke first. Runtime changes should normally include
the full release CTest result; memory-safety changes should also run:

```sh
cmake --preset sanitized
cmake --build --preset sanitized
ctest --preset sanitized
```

Generated bundles, screenshots, caches, logs, and machine-specific config must
not be committed unless they are deliberate, licensed baseline evidence.

## Issues and pull requests

Bug reports must include the React Native version, bundle source and build mode,
the complete `rnsim.json` with secrets removed (or the release launcher and
version when no config was exposed), exact command, output/error, and a
screenshot or trace. State whether the result came from source, installed
runtime, or a release archive.

Pull requests should state the runtime/host assumptions, describe the supported
contract rather than only the implementation, and include the narrowest
verification command and result.
