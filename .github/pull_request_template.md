## Summary

Describe the supported runtime, renderer, frontend, or documentation contract
changed by this pull request.

## Runtime assumptions

- React Native revision:
- Hermes revision:
- Platform profile:
- macOS and architecture:
- Caller bundle or addon assumptions:

## Verification

List the narrowest relevant commands and their results. Distinguish source-tree,
installed-package, and release-archive evidence.

## Capability classification

For native behavior, select and explain one classification:

- [ ] Implemented with platform-equivalent evidence
- [ ] Host-adapted and explicitly documented
- [ ] Mocked or descriptor-only
- [ ] Unavailable / rejected
- [ ] Not applicable

## Checklist

- [ ] Core paths do not introduce a Node.js/npm runtime dependency.
- [ ] Caller-owned bundle and application contracts remain outside the engine.
- [ ] Interactive and headless modes still use the same semantic engine.
- [ ] Generated bundles, caches, logs, and machine-specific paths are excluded.
- [ ] User-facing behavior and capability documentation are updated.
