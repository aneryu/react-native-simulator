# Skia retained renderer

The phase-2 Skia backend renders the typed retained Fabric scene into a CPU
raster surface. Direct screenshots use the in-process `SceneSnapshot`; they do
not serialize and parse metrics JSON. Dear ImGui remains the inspector shell;
it no longer draws RN text or view backgrounds when this backend is enabled.

The rendering path is:

```text
React reconciler
  -> Fabric ShadowTree / Yoga
  -> MountingTransaction
  -> SceneSnapshot
  -> Skia raster canvas + SkParagraph / HarfBuzz / ICU
  -> SDL_GPU transfer buffer + texture in the inspector
```

Skia is a pinned git submodule. Its official build uses GN and Ninja; CMake only
links the resulting static archives.

The bootstrap keeps ICU static but replaces Skia's full desktop data payload
with ICU's pinned `flutter_desktop` filter. ICU is still required: SkParagraph
uses its Unicode character properties, bidirectional resolution, grapheme/word
boundaries, and multilingual line-breaking data, and the simulator also uses
the same Unicode rules for natural writing direction and text transforms.
CoreText provides macOS fonts and glyph rasterization, not a compatible
replacement for those cross-platform Unicode semantics. The filtered data set
keeps the text dictionaries and break rules used by SkParagraph while omitting
unused date, currency, time-zone, locale-display-name, and transliteration
resources. This preserves one self-contained runtime without shipping an ICU
dylib or depending on a machine-specific Homebrew installation.

`flutter_desktop` is a transitional source for the filtered data, not a Flutter
runtime dependency or a product-level architecture choice. The pinned ICU tree
also contains an Android data package, but it is about 9.3 MB rather than 1.8 MB
and retains locale, currency, time-zone, unit, encoding-conversion, and
transliteration resources that this native macOS process does not use. Selecting
it would not improve Android profile fidelity because Unicode bidi and break
rules are not supplied by an Android client process. The long-term target is an
rnsim-owned `rnsim-text` filter containing only the Unicode properties, bidi,
normalization, break rules, and multilingual dictionaries covered by simulator
conformance tests. Moving to that filter requires certification before further
data removal; the smaller Flutter/Wasm filter is not assumed equivalent.

`HeadlessTextLayoutManager` and the renderer now share `SkiaTextLayoutEngine`.
Yoga measurement marks a cached prepared paragraph, the mounted `SceneNode`
retains that object, and in-process paint uses it directly. The current mapping
preserves attributed fragments, font family/size/weight/style, colors, opacity,
letter spacing, line height, wrapping, tail ellipsis, and inline attachment
metrics. Tail ellipsis uses SkParagraph; head/middle ellipsis binary-search a
truncated UTF-16 slice with U+2026, matching Android `TruncateAt.START/MIDDLE`
rather than silently ignoring the mode. `adjustsFontSizeToFit` uses RN Android's
spannable binary search (shrink to height, maxLines, and the single-character
width check). Paint reuses that search against the laid-out content box, not
infinite height, so `maxHeight` and parent-bounded cases shrink instead of
overflowing.
Android generic `fontFamily` values (`sans-serif`, `serif`, `monospace`, and
the light/thin/medium/condensed aliases) resolve to Roboto / Noto Serif /
Droid Sans Mono the way `fonts.xml` does; unknown families fall back to Roboto.
Yoga measurement (`HeadlessTextLayoutManager`) and Skia paint share one
`TypefaceFontProvider`. Roboto/serif/mono register at startup; Arabic, Hebrew,
Devanagari, Thai, CJK, and Color Emoji register once on first use, before the
layout mutex so file I/O cannot freeze the UI thread. Measure uses
`getLongestLine` (Android `StaticLayout` line width), not `getRectsForRange`.
ASCII digits are not treated as emoji. `overflow:hidden` clips the padding
box after background and border paint; clipping first ate RN Tester Text
red/blue strokes. Shrink-wrapped single-line `textAlign: 'center'` lays out
as start so the last glyph is not clipped (`Text` must not become `Tex`).
Wrapped or wide boxes use the real center/right alignment. SkParagraph
`layout(maxIntrinsic)` can still wrap; wrap_content paint uses
`maxIntrinsic+1` when that happens. CJK runs insert ZWSP between
ideographs. Android line height for Han uses Noto ascent/descent but Roboto
includePad extras.
Natural writing direction now uses
ICU's Unicode first-strong base-direction rule and feeds the same resolved
direction to measurement and paint. Underline/line-through decorations (solid/double/dotted/dashed/wavy) are
painted via SkParagraph. Android-style font padding uses SkFontMetrics top/ascent
gaps when `includeFontPadding` is on. Hyphenation is camelCase soft-hyphen
(`full`/`normal`) or zero-width wrap (`none` on long identifiers), not a
platform hyphenation dictionary. Wavy stroke shape follows Skia, not HWUI.
This is not a claim of Android/iOS text equivalence. Runtime metrics expose these distinctions
under `nativeCapabilities.text`. The selected `fontProfile` is
`configured-font-directory` when a local platform font directory is supplied;
Android without one is explicitly reported as an unverified macOS CoreText
fallback, while iOS uses the system CoreText profile.
Non-Skia migration builds no longer contain character-based approximate text
metrics: an RN text measurement request fails with an explicit Skia-required
error.

## Build on Linux

```bash
git submodule update --init third_party/skia
cmake/bootstrap-skia-linux.sh
cmake --preset release
cmake --build --preset release --target react-native-simulator
```

Linux uses Fontconfig for the missing-glyph fallback instead of CoreText, and
enables Skia's PNG/JPEG/WebP decoders because the host has no ImageIO.
`cmake/bootstrap-skia-linux.sh` checks out only the Skia DEPS pins required for
that CPU renderer (FreeType, HarfBuzz, ICU, zlib, libpng, libjpeg-turbo,
libwebp, Wuffs). It does not run Skia's full `tools/git-sync-deps`, which also
fetches GPU toolchains this host never links. RAW (piex/dng), bundled expat,
and Perfetto stay off.

## Build on Apple Silicon

```bash
git submodule update --init third_party/skia
cmake/bootstrap-skia-macos.sh
cmake --preset skia
cmake --build --preset skia --target react-native-simulator-inspector
```

To use fonts pulled from an Android device:

```bash
build/skia/runtime/react-native-simulator-inspector \
  --metrics build/rn0732-android-viewport.json \
  --android-font-dir build/android-fonts
```

The same directory must configure Engine text measurement, not only screenshot
export. Pass `--android-font-dir` on the `rnsim` command, set
`EngineConfig::fontDirectory`, or use local configuration:

```json
{
  "schemaVersion": 2,
  "reactNative": "0.87.0",
  "platform": "android",
  "fonts": {"directory": "./android-fonts"}
}
```

Relative configuration paths resolve from the `rnsim.json` directory. An
explicit directory with no loadable fonts fails Engine initialization.

## Direct PNG export

The Skia-enabled runtime can execute the bundles and export the raw surface in
one command without creating an SDL window or starting Dear ImGui:

```bash
cmake --preset skia
cmake --build --preset skia --target react-native-simulator

build/skia/runtime/rnsim headless --bundle /path/to/output.bundle \
  --profile android-rn87 --addon compat-rn73 \
  --bundle tests/fixtures/rn0732-render-workload.js \
  --viewport-width 392.7273 \
  --viewport-height 753.4545 \
  --point-scale-factor 2.75 \
  --android-font-dir build/android-fonts \
  --screenshot build/rn0732-skia.png
```

The output dimensions are the RN root logical dimensions multiplied by the
runtime `pointScaleFactor`. The command writes an 8-bit RGBA PNG and does not
start or depend on the SDL3/Dear ImGui Inspector process, Node.js, or npm.

To compare that PNG with a real-device `adb screencap` (Pixel 4a chrome crop,
side-by-side, difference blend, mismatch map), use the optional
`tools/diagnostics/compare-screenshots.mjs` script. TextExample device shots
are archived under `build/rntester/compare/device-archive/`; pass
`--skip-device` instead of capturing again. See
[RN Tester baseline](../baselines/RNTESTER_BASELINE.md), which also records the
certified Text cases and known renderer pitfalls.

The directory is loaded by Skia's FreeType font manager. It can contain `.ttf`,
`.ttc`, and `.otf` files. CoreText is retained only as a missing-glyph fallback.

View painting follows RN Android Fabric. The bar is that RN Tester `ViewExample`
looks basically the same as Pixel 4a, not HWUI pixel equivalence:

- `boxShadow` keeps every layer. Outset shadows `clipOut` the border box and
  grow radii with spread the way `OutsetBoxShadowDrawable` does. Inset shadows
  clip to the padding box and fill the even-odd ring used by
  `InsetBoxShadowDrawable`. Android `elevation` becomes Material Ambient
  (`offset 0, blur e, alpha 0.12`) plus Spot (`offset 0,e/2, blur e, alpha 0.24`).
- Overflow `hidden` clips descendants to the parent's elliptical `clipRRect`.
- Transforms concat around the view center so they match Android
  `setScaleX`/`setRotation` (default pivot). RN `resolveTransform` already wraps
  `transformOrigin` relative to that center. Paint and hit-testing both use the
  z=0 homography of that pivoted column-major 4x4. When `perspective` is
  present, Android `View` decomposes the 4x4 and reapplies camera +
  `rotationX/Y/Z`, so Transform Styles' `[{rotateX}, {perspective}]` still has
  depth. Without a 3D rotation the same path drops shear, so
  `skewY`+`perspective` paints as a diamond. Pure `skewY` without `perspective`
  still uses the real shear. Paint and
  hit-testing walk root-to-node so a parent `transform` also rotates nested
  Text/Paragraph.
- `backfaceVisibility: 'hidden'` skips a node whose 2D matrix determinant is
  negative (`rotateY(180)` collapses to `scaleX(-1)`).
- `needsOffscreenAlphaCompositing` opens a `saveLayer` so overlapping children
  composite before the group opacity.
- `PlatformColor('?attr/colorError')` and similar theme tokens resolve through
  a host `PlatformColorParser` sampled from Pixel 4a AppCompat DayNight, not a
  Resources lookup. Pressable `nativeBackground` ripple is a pressed overlay.
- CSS `filter` is a Skia image-filter chain (blur, brightness, contrast,
  grayscale, saturate, sepia, invert, hue-rotate, opacity, drop-shadow).
  Multi-function lists compose. This is glance-level Pixel, not HWUI.
- Linear and radial `backgroundImage` paint multi-stop / multi-layer
  gradients. `background-repeat` and `background-size` are not painted.
- `mixBlendMode` maps the 16 RN modes plus `isolation`; Skia blend, not
  Android RenderNode compositing.

The inspector uploads a new texture only when the scene (or offline metrics)
revision changes;
normal UI frames reuse the existing texture. The same-process interactive
frontend consumes typed `SceneSnapshot` directly; the independent versioned
`scene` wire payload and `mountedViewTree` decoding are offline compatibility
paths only. The `release` preset now builds Skia and interactive SDL/ImGui by
default; headless runs do not create a window.
