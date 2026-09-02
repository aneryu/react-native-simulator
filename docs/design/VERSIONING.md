# Nightly versioning policy

## Decision

React Native Simulator is distributed as **Nightly** until the runtime,
capability baseline, conformance oracles, packaging, and compatibility promises
are stable enough for a deliberate release. During this phase, `0.x` numbers do
not communicate a useful stability contract and must not be used as the public
product identity.

`nightly` is the only public release name. Every build must also expose the
exact Git commit and clean/dirty state for diagnosis, but the commit is not part
of the release name, archive name, or installation directory. Published assets
must come from a clean checkout.

## Version boundaries

Nightly replaces only the simulator's product release number. These independent
compatibility contracts remain explicitly versioned:

- React Native revision and JavaScript-facing version;
- Hermes revision and HBC compatibility;
- simulator addon ABI;
- `rnsim.json` schema;
- retained-scene or Inspector wire formats;
- minimum macOS version and architecture.

Changing the Nightly build identity must never disguise a change in one of
these contracts. Addons continue to validate their ABI, React Native, and Hermes
versions rather than accepting another build merely because both say Nightly.

## CLI and machine-readable metadata

The target CLI form is:

```text
react-native-simulator nightly (commit 61510f45e723f9d2, clean)
React Native 0.87.0, Hermes 260318099.0.1, addon ABI 2
```

Machine-readable metadata should separate fields instead of parsing the display
string:

```json
{
  "channel": "nightly",
  "commit": "61510f45e723f9d2aaee5020fb720e30e8f0c223",
  "dirty": false
}
```

The commit is authoritative when diagnosing which Nightly bytes were used.

## Packaging and installation

Runtime assets use stable rolling names:

```text
rnsim-nightly-macos-arm64.dmg
rnsim-nightly-macos-arm64.dmg.sha256
```

There is one moving GitHub Release and tag named `nightly`. Publishing replaces
its two assets. The DMG contains exactly one self-contained executable named
`rnsim`; RN Tester, addons, headers, libraries, DevTools frontend files, and
GitHub CI artifacts do not participate in public distribution. Installing a
newer package atomically replaces the previous Nightly. The project does not
retain or manage old Nightly binaries. Git history and the commit in runtime
metadata provide source traceability, not an end-user rollback service.

CMake still requires a numeric project/package version. During Nightly migration
that value is transport metadata only; it is not the public simulator version.
Source-tree embedders use the `ReactNativeSimulator::Engine` target and validate
runtime/addon compatibility through the explicit contracts above. The Nightly
DMG is the CLI distribution and is not an embedding SDK.

## Rolling update policy

The CLI, CMake metadata, packaging, and installer use the single `nightly`
channel. No numbered simulator release or old Nightly asset is retained.
Updating Nightly means locally rebuilding from the new clean commit, signing the
executable with Developer ID and Hardened Runtime, signing and notarizing the
DMG, stapling its ticket, then replacing the `nightly` tag and Release assets.
GitHub Actions is a code-validation lane only and cannot publish Nightly.

Maintainers publish an already verified `dist/` from clean `main` with
`tools/release/publish-nightly.sh`. The script re-verifies the extracted
single-file runtime, signature, notarization, dependency closure, metadata, and
headless smoke; it then pushes `main`, moves the `nightly` tag, overwrites the
single Latest GitHub Release, and removes obsolete assets. Nightly is not
marked as a GitHub prerelease so the repository About sidebar can surface the
download; the rolling-preview contract still lives in this policy and the
release notes.

## Leaving Nightly

A numbered release requires an explicit decision after, at minimum:

- the supported RN/profile matrix and compatibility policy are frozen;
- conformance claims have canonical device, font, profile, and oracle manifests;
- public API and addon ABI evolution rules are documented;
- reproducible packaging, signing, upgrade, rollback, and support boundaries are
  validated.

Choosing that first numbered release and its SemVer guarantees is a separate
design decision; it must not be inferred from Nightly's duration or commit count.
