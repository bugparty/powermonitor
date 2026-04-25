# Handoff: Power Monitor Viewer Design System

## Overview

Design system + UI kit for the **Power Monitor** project (openpowermonitor) — an INA228 / Raspberry Pi Pico power-monitoring tool with a browser-based timeline viewer. This handoff captures the visual + interaction language derived from `web_viewer/` (the repo's only GUI) and packages it for a developer to implement in the existing `web_viewer/` React+TypeScript+Vite+Chart.js codebase (or to clone as reference for a new surface).

## About the Design Files

The files in this bundle are **design references created in HTML/JSX** — prototypes showing intended look and behavior, not production code to copy directly.

The target codebase (`web_viewer/`) is already React + TypeScript + Chart.js. Your task is to **adopt the tokens and patterns** in the existing app — not replace it with the JSX here. The JSX samples in `ui_kits/viewer/` are vanilla React + Babel (no TS, no build) so they run standalone; port them to `.tsx` matching the existing component style in `web_viewer/src/components/`.

## Fidelity

**High-fidelity.** Exact hex values, pixel sizes, Inter + JetBrains Mono typography, and interaction states are specified. Recreate pixel-perfectly.

---

## Design Tokens

Source of truth: **`colors_and_type.css`** — drop this file in (or merge its `:root {}` into `web_viewer/styles.css`). All tokens are CSS variables.

### Colors — Surfaces (dark stack)
| Token | Hex | Role |
|---|---|---|
| `--bg-deep` | `#0B0D10` | Top bar, deepest shell |
| `--bg` | `#111317` | App background |
| `--panel-raised` | `#14181F` | Inside timeline panels (gradient top) |
| `--panel` | `#181B21` | Cards, selects, chart stack, chips |
| `--line` | `#2F3643` | 1 px borders, grid lines, dividers |
| `--subtle` | `#6B7280` | Hover crosshair |
| `--muted` | `#94A0B8` | Axis labels, meta text |
| `--text` | `#DBE2EE` | Primary text |

### Colors — Signals (one hue per metric)
| Token | Hex | Metric |
|---|---|---|
| `--sig-voltage` | `#22C55E` | Voltage (V) |
| `--sig-current` | `#F59E0B` | Current (A) |
| `--sig-power` | `#A855F7` | Power (W) |
| `--sig-temp` | `#EF4444` | Temperature (°C) |
| `--sig-vshunt` | `#06B6D4` | Shunt voltage |
| `--sig-charge` | `#F97316` | Charge (C) |
| `--sig-energy` | `#3B82F6` | Energy (J) |

Color stays consistent legend dot → track chip → chart stroke.

### Colors — Semantic & Accent
| Token | Hex | Use |
|---|---|---|
| `--accent` | `#3F8CFF` | Hover/focus border only — never fills |
| `--accent-soft` | `rgba(63,140,255,0.16)` | Timeline panel tinted border, focus halo |
| `--ok` | `#22C55E` | Success |
| `--warn` | `#F59E0B` | Warning |
| `--err` | `#EF4444` | Error |
| `--info` | `#3F8CFF` | Info |

### Typography
- `--font-sans`: `Inter, "Segoe UI", Roboto, Helvetica, Arial, sans-serif`
- `--font-mono`: `"JetBrains Mono", "SFMono-Regular", ui-monospace, Menlo, Consolas, monospace`

**Scale** (dense, instrumentation-grade):
| Size | Token | Use |
|---|---|---|
| 18 px / 600 | `--t-h1` | Product name (only) |
| 15 px / 600 | — | Section headings |
| 13 px / 600 | `--t-title` | Panel titles, control labels |
| 13 px / 400 | `--t-body` | Body |
| 12 px / 400 | `--t-meta` | Axis / legend / meta, muted |
| 11 px / 400 | `--t-axis` | SVG axis ticks, muted |
| 12 px / 400 | `--t-status` | Status row, mono |

Use `var(--font-mono)` for all numerics, timestamps, hex codes, message IDs.

### Spacing (2 px half-step grid)
`--space-1..7`: **4 / 6 / 8 / 10 / 12 / 14 / 16**. No 24, no 32 — layouts stay tight.

### Radii
`--radius-sm` 6 px (inputs), `--radius-md` 8 px (buttons, chips), `--radius-lg` 10 px (panels, cards). No pills.

### Shadows
**Flat** — no drop shadows. Depth = hairlines + one subtle gradient inside timeline panels:
```css
background: linear-gradient(180deg, rgba(20,24,31,0.96), rgba(17,19,23,0.96));
```

---

## Content / Voice Rules

- **Sentence case** for all labels: "Load JSON", "Fit All", "Align starts".
- **Title Case** only for the product name.
- **Always show units** in labels: `Voltage (V)`, `Current (A)`, `0.250 s`.
- **No emoji.** No exclamation marks. No marketing adjectives ("powerful", "seamless").
- **Number formatting**: `toFixed(4)` tooltips, `toFixed(3)` small values, `toFixed(1)` ≥ 100, µs → s with 3 decimals.
- **Status messages state facts**: "Failed to load JSON: HTTP 404", "1,274 visible samples".

---

## Screens / Views

The viewer is a **single-page vertical stack** in this order:

### 1. Top bar (`web_viewer/src/components/TopBar.tsx`)
- Height auto · padding `12px 16px` · `background: var(--bg-deep)` · `border-bottom: 1px solid var(--line)`
- Left: 22 px square brand mark + H1 "Power Monitor Timeline Viewer" (18/600)
- Right controls (gap `8px`): **Load JSON** button, **Fit All** button, **Downsample** select (None / Min-Max / LTTB), **Align starts** checkbox, optional filename (`font-mono`, 11 px, muted, ellipsis ≤ 260 px)

### 2. Meta bar (`MetaBar.tsx`)
- `padding: 10px 16px` · `border-bottom: 1px solid var(--line)` · muted 13 px · **mono**
- Format: `schema 1.0 · protocol 1 · stream_period 1000 µs · 2 sources · 24,810 samples`

### 3. Track chips row (`TrackControl.tsx`)
- `padding: 10px 16px` · `border-bottom: 1px solid var(--line)` · flex gap `14px`, `flex-wrap: wrap`
- Row label "Tracks (drag to reorder)" in muted 13 px
- Each chip: `.pm-chip` — 1 px `--line` border, radius `8px`, `--panel` bg, `padding: 6px 10px`, `cursor: grab` → `grabbing` on drag. Hover swaps border to `--accent`. Contains: checkbox + 10 px colored `.pm-dot` + label (12 px).

### 4. Chart area (`TimelineChart.tsx`)
- `padding: 12px` · flex:1
- `.pm-stack`: 1 px `--line` border, radius `10px`, `--panel` bg, `padding: 10px`, grid `gap: 10px`
- Scroll wheel zooms, mouse drag pans (see `usePanZoom.ts`)

### 5. Timeline panel — one per source
- Accent-tinted border `1px solid rgba(63,140,255,0.16)`, radius `10px`, dual-stop gradient bg (see Shadows)
- **Header** (`padding: 10px 12px`, `border-bottom: 1px solid rgba(47,54,67,0.8)`): left = source label 13/600, right = `{n} visible samples` 12 px muted **mono**
- **Body**: fixed `240px` height, `padding: 6px 10px 10px`, Chart.js line chart with: `tension: 0`, `pointRadius: 0`, `borderWidth: 1.6`, `animation: { duration: 0 }`, y tick mono/muted, x tick formatted `(v/1e6).toFixed(3) + ' s'`, grid `rgba(47,54,67,0.55)`
- Hover crosshair: `stroke: #6b7280`, `stroke-width: 1`, `stroke-dasharray: 4 3`

### 6. Status row (bottom)
- `padding: 10px 16px` · `border-top: 1px solid var(--line)` · muted 12 px · **mono**
- Hover: `t=0.142 s · V=12.0430 · A=0.5120 · W=6.1661 · source=pico`
- Idle: `range 0.000 s → 2.400 s · scroll to zoom · drag to pan`

### 7. Empty state (when no data)
- Centered, `padding: 40px`
- `1px dashed var(--line)` rounded 10 px bordered card, `--panel` bg, padding `36px 48px`, flex column gap `10px`
- Title "No data loaded" 15/600, sub "Load a JSON file to view timeline" 13 muted, primary **Load JSON** button

---

## Interactions & Behavior

- **Hover** on any bordered control: `border-color: var(--accent)`, no background change, no cursor change. Transition: `border-color 80ms ease-out`.
- **Focus** on buttons/selects: border → accent + `box-shadow: 0 0 0 2px rgba(63,140,255,0.18)`.
- **Disabled**: `opacity: 0.5`, no hover border swap.
- **Drag chip**: `cursor: grab` → `grabbing`. Drop reorders the chip list (see `TrackControl.tsx`).
- **Wheel on chart stack**: zoom about pointer. `scale = deltaY < 0 ? 0.8 : 1.25`, clamp span ≥ 1 µs.
- **Mouse-down drag on chart stack**: pan range by `-(dx / width) * span`.
- **Hover on panel**: crosshair follows pointer; status row updates with nearest-sample values.
- **Animations**: Chart.js `duration: 0`. No animations anywhere else. If any new animation is added, ≤ 120 ms, `ease-out`, opacity/transform only.

---

## State Management

Match the existing hooks (`useTimelineState`, `usePanZoom`):
- `tracks: Track[]` — id, key, label, color, visible; order = render order
- `series: Series[]` — parsed sources, each with `points: Point[]`
- `range: { start, end }` (µs) — visible time window
- `downsampleMode: 'none' | 'min-max' | 'lttb'`
- `alignStarts: boolean`
- `hoverPoint: Point | null`

---

## Assets

- `assets/mark.svg`, `assets/wordmark.svg` — **placeholder originals** (repo ships no logo). Replace when real mark exists.
- Icons: **Lucide** (https://lucide.dev) via CDN, 1.5 px stroke, 24 px, sparingly. Text labels preferred.

---

## Files in this bundle

```
design_handoff_power_monitor/
├── README.md                   ← you are here
├── colors_and_type.css         ← drop-in tokens (primary deliverable)
├── SKILL.md                    ← agent-readable summary
├── assets/                     ← logo/mark SVGs + asset README
├── preview/                    ← one HTML card per token group (reference only)
└── ui_kits/viewer/             ← clickable React recreation
    ├── index.html              ← standalone demo
    ├── styles.css              ← component CSS using the tokens
    ├── TopBar.jsx
    ├── TrackChips.jsx
    ├── TimelinePanel.jsx
    ├── Trim.jsx                ← MetaBar + StatusRow + EmptyState
    ├── data.jsx                ← synthetic dataset for demo
    └── README.md
```

## Implementation steps for `web_viewer/`

1. **Replace** `web_viewer/styles.css` `:root { ... }` with the contents of `colors_and_type.css`. All existing class names (`.topbar`, `.timeline-panel`, `.track-item`, …) already reference the old tokens — they'll continue to work, just with the improved palette.
2. **Add** JetBrains Mono + Inter via Google Fonts in `web_viewer/index.html`:
   ```html
   <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
   ```
3. **Apply `var(--font-mono)`** to: `.meta`, `.status-row`, `.timeline-panel-meta`, all axis ticks (pass `font-family` in Chart.js `ticks.font`).
4. **Nudge `bg-deep`** — any hardcoded `#0f1115` in the repo should become `var(--bg-deep)` (now `#0B0D10`).
5. **Add the empty state** — currently `TimelineChart.tsx` just shows a plain "Load a JSON file" div. Replace with the styled empty card pattern (see `ui_kits/viewer/Trim.jsx` → `PMEmptyState`).
6. **Add filename pill** to `TopBar` (mono, muted, truncate) — nice-to-have.
7. **Verify visuals** against `preview/*.html` cards.
