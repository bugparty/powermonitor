# Viewer — Power Monitor UI Kit

High-fidelity React recreation of the **Power Monitor Timeline Viewer**, the only GUI surface in the `openpowermonitor` repo.

## Mapped from source

| File here                | Corresponds to (original)                              |
|--------------------------|--------------------------------------------------------|
| `TopBar.jsx`             | `web_viewer/src/components/TopBar.tsx`                 |
| `TrackChips.jsx`         | `web_viewer/src/components/TrackControl.tsx`           |
| `TimelinePanel.jsx`      | `web_viewer/src/components/TimelineChart.tsx` (per-source panel, SVG renderer) |
| `Trim.jsx`               | `MetaBar.tsx` + status row + empty state               |
| `data.jsx`               | Synthetic stand-in for `parsePayload.ts` output        |
| `styles.css`             | `web_viewer/styles.css`                                |
| `index.html`             | `App.tsx` + `index.html` wired up as a clickable demo  |

## Run

Open `index.html` (no build step). Click **Load JSON** → if an unrecognised file is picked, a synthetic two-source 1 kHz dataset loads instead; the empty state has a direct **Load JSON** that calls the demo loader.

## Interactions
- Load JSON · Fit All
- Toggle each track on/off, drag-reorder chips
- Scroll-wheel zoom + drag-pan on the chart stack
- Hover a panel for crosshair + status-row readout
- Align starts · Downsample (selector wired; panel uses raw SVG render)

## Differences vs real app
- Renders series via inline SVG, not Chart.js — matches visual language but omits canvas tooltip.
- `Downsample` selector is a no-op on the SVG renderer (acknowledged; downsample math already exists in `TimelineChart.tsx` if porting).
- Pan/zoom uses a simpler algorithm than `usePanZoom.ts`.
