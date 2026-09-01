# RN Tester Text archive - Pixel 4a - 2026-09-01

This is the compact RN Tester Android `TextExample` comparison gallery copied
from `build/rntester/compare/text-all-hinting-split-final` during project
closeout. Open `index.html` locally to inspect all 50 named cases.

- Device: Pixel 4a, 1080 x 2340 at 2.75 scale
- Simulator RN window: 1080 x 2072
- Alignment profile: `pixel-4a`, crop 136 px top and 132 px bottom
- Result: 50 shots, 0 skipped, 0 failed
- Mismatch range: 0.029% to 42.425%; mean 2.813%
- Acceptance: broadly consistent appearance, not HWUI/Skia pixel equivalence
- Source base revision: `c6296fb`; the renderer and runtime changes were still
  in the working tree when this evidence was generated
- `index.json` SHA-256:
  `a62911051eab4a10f8fb5914a5053d1e18cd70512c80e24a67e1a0b54d2b5864`
- `index.html` SHA-256:
  `1611ba549b12c5f3bca3446879a70f14668f6565b213de3336d8931a8615dc94`

`SHA256SUMS` covers every archive file except the checksum list itself. PNG
files are tracked through Git LFS so the gallery does not inflate normal Git
objects.

The repository archive intentionally keeps one `side-by-side.png` and one
`report.json` per case, plus the gallery indexes and adapter. Raw device,
simulator, diff, overlay, mismatch, and labeled PNGs remain in the ignored
`build/rntester/compare/text-all-hinting-split-final` working artifact. Store
that full directory in release/object storage when long-term raw evidence is
required; it is not part of the Git archive.
