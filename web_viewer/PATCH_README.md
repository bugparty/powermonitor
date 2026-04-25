# Applying the design system patch

The patch file `apply_design_system.patch` upgrades `web_viewer/styles.css` and `web_viewer/index.html` in place to use the new token system.

## 1. Apply

From the repo root:

```bash
git apply --3way design_handoff_power_monitor/apply_design_system.patch
```

If `--3way` is not supported on your git version, try:

```bash
git apply --reject design_handoff_power_monitor/apply_design_system.patch
```

Any `.rej` files show hunks that didn't apply cleanly — usually because the file drifted since commit `9e4f84a`. Merge them manually.

## 2. Verify

```bash
npm run dev
# open http://localhost:5173/web_viewer/index.html?data=/build/pc_client/output/<your-file>.json
```

Check:
- Top bar is visibly darker than the app body
- Meta row, status row, and panel sample counts render in JetBrains Mono
- Hovering any button / select / track chip swaps its border to blue (#3F8CFF)
- Focus ring (Tab key) shows the soft blue halo
- Empty-state "Load a JSON file to view timeline" is now a dashed bordered card, not bare text

## 3. What the patch does NOT change

- Chart.js configuration — if you want mono axis ticks, edit `TimelineChart.tsx` and add `ticks.font: { family: "'JetBrains Mono', monospace" }` to both x and y scales.
- Component JSX — no behavior changes.
- Signal colors — they gained `--sig-*` aliases but the old `--voltage`, `--current`, `--power` still work.

## 4. Rollback

```bash
git apply -R design_handoff_power_monitor/apply_design_system.patch
```
