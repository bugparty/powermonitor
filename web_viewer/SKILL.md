---
name: power-monitor-design
description: Use this skill to generate well-branded interfaces and assets for Power Monitor (openpowermonitor) — an INA228 / Raspberry Pi Pico power-monitoring tool with a browser-based timeline viewer. Contains essential design guidelines, colors, type, fonts, assets, and UI kit components for prototyping.
user-invocable: true
---

Read the `README.md` file within this skill, and explore the other available files.

If creating visual artifacts (slides, mocks, throwaway prototypes, etc), copy assets out and create static HTML files for the user to view. If working on production code, you can copy assets and read the rules here to become an expert in designing with this brand.

If the user invokes this skill without any other guidance, ask them what they want to build or design, ask some questions, and act as an expert designer who outputs HTML artifacts _or_ production code, depending on the need.

## Quick orientation

- **Aesthetic:** dark instrumentation panel / logic analyzer. Flat, hairline borders, no shadows, no animations.
- **Tokens:** `colors_and_type.css` — link this first in any new HTML.
- **Type:** Inter (UI) + JetBrains Mono (numerics, timestamps, codes).
- **Icons:** Lucide via CDN, used sparingly — prefer text labels (flagged substitution).
- **Logo:** `assets/wordmark.svg` / `assets/mark.svg` — originals, placeholder until a real mark exists.
- **Components:** see `ui_kits/viewer/` for the recreated Timeline Viewer (TopBar, TrackChips, TimelinePanel, MetaBar, StatusRow, EmptyState).
- **Signal colours:** V green `#22c55e`, A amber `#f59e0b`, W violet `#a855f7`, °C red `#ef4444`, shunt V cyan `#06b6d4`, C orange `#f97316`, J blue `#3b82f6`.
- **Copy:** terse, imperative, sentence case, explicit units, no emoji, no exclamation marks.
