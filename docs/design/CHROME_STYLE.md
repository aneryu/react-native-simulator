---
version: alpha
name: react-native-simulator
description: Dark-only macOS simulator chrome. Linear surface ladder and scarce lavender CTA, Raycast keyboard chrome and keycaps, Expo device-as-protagonist, Apple SF Pro + receding shell. Dear ImGui draws the host; Skia paints the RN device. Not a marketing site.
colors:
  canvas: "#07080A"
  window: "#0C0D10"
  panel: "#101114"
  panel-alt: "#0D0E11"
  elevated: "#16171C"
  frame: "#1C1E26"
  frame-hover: "#252833"
  hairline: "#23252A"
  hairline-strong: "#34343A"
  ink: "#F7F8F8"
  muted: "#8A8F98"
  accent: "#5E6AD2"
  accent-hover: "#828FFF"
  accent-active: "#4F5BC0"
  on-accent: "#FFFFFF"
  inspect: "#F5C542"
  success: "#4ADE80"
  danger: "#F07178"
  info: "#57C1FF"
typography:
  ui:
    fontFamily: "SF Pro Text, SFNS, Helvetica Neue, system-ui, sans-serif"
    fontSize: 14px
    fontWeight: 400
  ui-strong:
    fontFamily: "SF Pro Text, SFNS, Helvetica Neue, system-ui, sans-serif"
    fontSize: 14px
    fontWeight: 500
  panel-title:
    fontFamily: "SF Pro Text, SFNS, Helvetica Neue, system-ui, sans-serif"
    fontSize: 14px
    fontWeight: 600
  caption:
    fontFamily: "SF Pro Text, SFNS, Helvetica Neue, system-ui, sans-serif"
    fontSize: 12px
    fontWeight: 400
  keycap:
    fontFamily: "SF Pro Text, SFNS, Helvetica Neue, system-ui, sans-serif"
    fontSize: 11px
    fontWeight: 500
  mono:
    fontFamily: "SF Mono, Menlo, ui-monospace, monospace"
    fontSize: 12px
    fontWeight: 400
rounded:
  xs: 4px
  sm: 6px
  md: 8px
  lg: 12px
  device: 16px
  pill: 9999px
spacing:
  xxs: 4px
  xs: 8px
  sm: 12px
  md: 16px
  lg: 24px
components:
  button-primary:
    backgroundColor: "{colors.accent}"
    textColor: "{colors.on-accent}"
    rounded: "{rounded.sm}"
    padding: 8px 14px
  button-neutral:
    backgroundColor: "{colors.frame}"
    textColor: "{colors.ink}"
    rounded: "{rounded.sm}"
  button-ghost:
    backgroundColor: transparent
    textColor: "{colors.muted}"
    rounded: "{rounded.sm}"
  button-danger:
    backgroundColor: "rgba(240,113,120,0.16)"
    textColor: "{colors.danger}"
    rounded: "{rounded.sm}"
  segmented:
    backgroundColor: "{colors.frame}"
    selectedBackground: "{colors.elevated}"
    selectedText: "{colors.ink}"
    mutedText: "{colors.muted}"
    rounded: "{rounded.sm}"
  keycap:
    backgroundColor: "{colors.elevated}"
    textColor: "{colors.muted}"
    borderColor: "{colors.hairline}"
    rounded: "{rounded.xs}"
  panel:
    backgroundColor: "{colors.panel}"
    borderColor: "{colors.hairline}"
    rounded: "{rounded.md}"
  device-bezel:
    backgroundColor: "{colors.elevated}"
    borderColor: "{colors.hairline-strong}"
    rounded: "{rounded.device}"
  device-well:
    backgroundColor: "{colors.canvas}"
    textColor: "{colors.muted}"
    rounded: "{rounded.device}"
  status-chip:
    rounded: "{rounded.pill}"
    padding: 3px 8px
  text-input:
    backgroundColor: "{colors.frame}"
    textColor: "{colors.ink}"
    borderColor: "{colors.hairline}"
    rounded: "{rounded.sm}"
---

# React Native Simulator DESIGN.md

Status: interactive + inspector chrome. Skia device pixels are out of scope.

This file is the visual source of truth for `rnsim interactive` and
`react-native-simulator-inspector`. Coding agents must restyle host chrome
against these tokens, not against neighboring marketing sites.

Architecture and RN semantics stay in `SIMULATOR_DESIGN.md`. This file
only answers: how should the macOS host look?

## Why these systems (and not the rest)

Reviewed against [VoltAgent/awesome-design-md](https://github.com/VoltAgent/awesome-design-md).
The host is a dark macOS engineering tool whose job is to present a live
React Native raster. Marketing light canvases, photography heroes, and
decorative gradients do not apply.

| Source | Take | Leave |
|---|---|---|
| **Linear** | Near-black canvas, four-step surface ladder, hairline elevation, scarce lavender CTA `#5E6AD2`, compact 6–8px radii, SF Pro fallback | Marketing display type, 80px heroes, inverse white CTAs |
| **Raycast** | Keyboard-first chrome, segmented control as surface lift (not a filled CTA), keycaps, 36px actions, dark-only continuity | Red hero stripes, white primary pill, category accent illustration colors on chrome |
| **Expo** | Device mockup is the page chrome; RN pixels are the protagonist; 8px developer CTAs; 16px device radius | White marketing canvas, sky-blue hero wash, black CTA, Inter |
| **Apple** | SF Pro on macOS, Dark Aqua titlebar, receding shell, one product shadow on the device only | Pill CTAs, 17px body, photography tiles, Action Blue `#0066CC` |

Rejected for this host: Cursor cream/orange, Warp warm charcoal, Vercel
black-and-white marketing, Superhuman glow, Sentry pink fill, Expo/Apple
light-mode sites, any retro or automotive look.

## 1. Visual Theme & Atmosphere

Quiet dark instrument. The shell recedes; the Skia device does the talking.

- One continuous dark mode. No light chrome, no theme toggle.
- Density is tool-like (Raycast/Linear in-product), not editorial.
- Hierarchy is surface lift + hairlines, not drop shadows.
- One chromatic brand voltage: Linear lavender. Use it for **Run / primary
  host actions, focus, and hover-inspect**. Nothing else.
- Inspect-on-device uses amber `{colors.inspect}` so the overlay stays
  readable on arbitrary RN pixels. That is an inspector color, not a second
  brand.
- Semantic green / red / info appear only on status chips and host dialogs.

## 2. Color Palette & Roles

### Surfaces (ladder, dark → lifted)

| Token | Hex | Role |
|---|---|---|
| `{colors.canvas}` | `#07080A` | Window floor, toolbar, SDL clear, macOS titlebar fill |
| `{colors.window}` | `#0C0D10` | Unused outer chrome; keep one step above canvas |
| `{colors.panel}` | `#101114` | ShadowTree + Pages side panels |
| `{colors.panel-alt}` | `#0D0E11` | Device column — darker so the raster pops |
| `{colors.elevated}` | `#16171C` | Dialogs, selected segment, bezel, keycaps |
| `{colors.frame}` | `#1C1E26` | Inputs, segmented track, neutral buttons |
| `{colors.frame-hover}` | `#252833` | Hovered frames |
| `{colors.hairline}` | `#23252A` | 1px borders and toolbar rule |
| `{colors.hairline-strong}` | `#34343A` | Device bezel stroke, focused input |

### Text

| Token | Hex | Role |
|---|---|---|
| `{colors.ink}` | `#F7F8F8` | Primary labels, selected text |
| `{colors.muted}` | `#8A8F98` | Captions, unselected segments, property keys, hints |

### Accent & semantic

| Token | Hex | Role |
|---|---|---|
| `{colors.accent}` | `#5E6AD2` | Primary CTA, focus, tree selection wash, hover-inspect stroke |
| `{colors.accent-hover}` | `#828FFF` | Pressed/hovered primary |
| `{colors.accent-active}` | `#4F5BC0` | Active primary |
| `{colors.on-accent}` | `#FFFFFF` | Label on primary fill |
| `{colors.inspect}` | `#F5C542` | Selected-node overlay on the device only |
| `{colors.success}` | `#4ADE80` | live / PASS / permission |
| `{colors.danger}` | `#F07178` | stopped / CHECK / Deny / log errors |
| `{colors.info}` | `#57C1FF` | Open URL / Settings dialogs |

Do not introduce orange, pink, emerald, or sky-blue as chrome fills.
Do not paint lavender onto panel backgrounds.

## 3. Typography Rules

macOS already ships the correct face. Load SF Pro (`SFNS.ttf`) with CJK
fallback. Do not bundle Inter, Geist, or JetBrains Mono for host chrome.

| Token | Size | Weight | Use |
|---|---|---|---|
| `{typography.ui}` | 14px | 400 | Default ImGui text |
| `{typography.ui-strong}` | 14px | 500 | Buttons, selected rows |
| `{typography.panel-title}` | 14px | 600 | Device / Pages / ShadowTree headers |
| `{typography.caption}` | 12px | 400 | Viewport subtitle, running app key, hints |
| `{typography.keycap}` | 11px | 500 | Shortcut glyphs |
| `{typography.mono}` | 12px | 400 | Logs and JSON if a mono face is present |

No display sizes. This is not a marketing surface.

## 4. Component Stylings

### Buttons

- **Primary** — `{colors.accent}` fill, `{colors.on-accent}` text,
  `{rounded.sm}` 6px, ~32–36px tall. One visible primary per region
  (`Run` / `Re-run`, dialog confirm). Not a pill.
- **Neutral** — `{colors.frame}` fill, ink text. Secondary dialog actions.
- **Ghost** — transparent, muted text. Dismiss / Copy / Clear.
- **Danger** — danger-tinted fill, `{colors.danger}` text. Deny / negative.

Never use the accent fill for Interact/Select. That control is a segmented
track, not a CTA.

### Segmented control (Interact / Select)

Raycast surface-lift:

- Track: `{colors.frame}` + hairline, `{rounded.sm}`.
- Selected: `{colors.elevated}` + `{colors.ink}`.
- Unselected: transparent + `{colors.muted}`.
- Shortcut keycaps `1` / `2` sit inside each segment.

### Keycap

Raycast glyph: `{colors.elevated}` fill, hairline, `{rounded.xs}` 4px,
muted 11px label, ~18px tall.

### Panels

Side columns use `{component.panel}`: `{colors.panel}`, 1px hairline,
`{rounded.md}` 8px, 12px padding. Device column uses `{colors.panel-alt}`
and no competing inner chrome besides the bezel.

Panel header: title in ink, subtitle in muted, then a hairline. No amber
title accent.

### Device bezel (Expo protagonist)

The Skia texture is the product screenshot.

- Bezel fill `{colors.elevated}`, stroke `{colors.hairline-strong}`,
  radius `{rounded.device}` 16px, 12px inset.
- At most one soft product shadow under the bezel
  (`rgba(0,0,0,0.28)`). No stacked glow rings.
- Empty well: `{colors.canvas}` + hairline + centered muted
  “Select a page, then Run”.
- Hover overlay: lavender wash + `{colors.accent-hover}` stroke.
- Selected overlay: amber wash + `{colors.inspect}` stroke and label.

Do not round, tint, or re-composite RN pixels in ImGui.

### Status chips

Pill, 3×8px padding, 28-alpha wash of the semantic color, matching
foreground, optional 3.4px live dot. `live` / `stopped` / `PASS` /
`CHECK` / `replay`.

### Inputs

`{colors.frame}` fill, hairline border, `{rounded.sm}`, 8×6px padding.
Focus brightens the border to `{colors.hairline-strong}` — no colored
ring except when a dialog field is the actual focus target, then 50%
`{colors.accent}`.

### Host dialogs

Elevated surface, `{rounded.lg}` 12px, 20px padding. A 3px top rule in
the dialog’s semantic color (alert inspect, share accent, permission
success, URL info). Primary action last and right-aligned.

### Inspector

Same palette. Menu bar on canvas. PASS/CHECK chips as above. Tree
selection uses accent wash, not amber. Amber is device-overlay only.

## 5. Layout Principles

```text
[ traffic lights | live | Interact 1 | Select 2 | appKey ]
----------------------------------------------------------
[ ShadowTree 220–268 ] [ Device (flex) ] [ Pages 244–292 ]
```

- Base unit 4px. Panel padding 12px. Workspace gutter 8px.
- Toolbar shares the Dark Aqua titlebar row. No second window title.
- Device column is optically centered; side panels are tools, not peers
  of the raster.
- Minimum window 960×640. Comfortable default 1280×860.

## 6. Depth & Elevation

| Level | Treatment | Use |
|---|---|---|
| 0 | `{colors.canvas}`, no border | Toolbar, window floor |
| 1 | `{colors.panel}` + 1px hairline | Side panels |
| 2 | `{colors.elevated}` + hairline | Dialogs, bezel, selected segment |
| 3 | Product shadow under bezel only | Device mockup |

No drop shadows on buttons, panels, chips, or text.
No gradients, no glass, no neon.

## 7. Do's and Don'ts

### Do

- Keep the shell darker than the device raster.
- Spend `{colors.accent}` on Run, focus, and hover-inspect only.
- Use surface lift for selection in lists and segmented controls.
- Load SF Pro from the OS. Keep CJK fallback.
- Put shortcuts in keycaps (`1` / `2`).
- Fail closed: missing Skia/fonts still must not invent a fake renderer.

### Don't

- Don’t ship light mode.
- Don’t fill Interact with accent — Run is the only filled CTA in the
  toolbar/pages column.
- Don’t restyle RN components with ImGui. Dear ImGui is host chrome.
- Don’t copy Expo’s white canvas or Apple’s pill CTAs into this tool.
- Don’t add a second brand hue, decorative gradient, or Inter/Geist.
- Don’t use `{colors.inspect}` amber on panel titles or tree headers.

## 8. Responsive Behavior

ImGui desktop window, not a website.

| Width | Layout |
|---|---|
| ≥ 1280 | Three columns at the clamps above |
| 960–1279 | Same three columns, device flex shrinks first |
| < 960 | Window minimum; do not stack into a mobile marketing layout |

Hit targets: buttons ≥ 32px tall, segmented ≥ frame height, keycaps are
visual only (the whole segment is the hit target).

## 9. Agent Prompt Guide

Quick colors: canvas `#07080A` · panel `#101114` · elevated `#16171C` ·
hairline `#23252A` · ink `#F7F8F8` · muted `#8A8F98` · accent `#5E6AD2` ·
inspect `#F5C542` · success `#4ADE80` · danger `#F07178`.

When changing host UI:

1. Read this file and `frontend/SimulatorImGuiTheme.h`.
2. Put new colors in `imgui_theme::Palette`. Do not scatter hex in
   widgets.
3. Primary actions use `ButtonKind::Primary`. Mode switches use
   `segmented()`.
4. Device overlay hover = accent, selected = inspect.
5. Do not touch Skia text/layout or Fabric semantics to “make it look
   nicer.”

Ready-to-use prompt:

> Restyle the rnsim interactive shell to `docs/design/CHROME_STYLE.md`: Linear dark surface
> ladder, scarce lavender `#5E6AD2` CTA, Raycast segmented + keycaps,
> Expo 16px device bezel as the protagonist, SF Pro, no light mode, no
> second accent. ImGui chrome only; Skia raster unchanged.
