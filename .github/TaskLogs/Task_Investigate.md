# PROBLEM
investigate web_viewer's UI elements (powermonitor, latency ), these two elements 's height is too tall with too many paddings , ajust them like scheduling element which has proper padding

# UPDATES
- Continue: Investigating web_viewer panel spacing mismatch. Powermonitor and latency panels look taller because they are wrapped by an extra nested container with additional min-height and padding.

# TEST [CONFIRMED]
- Repro steps:
  1. Run the web viewer and load data containing power/latency series and scheduling timespans.
  2. Compare panel spacing in the same stack.
  3. Observe that powermonitor/latency panels appear inside an extra inner container with extra border/padding and larger vertical footprint, while scheduling panels are direct timeline panels.
- Success criteria:
  1. Powermonitor and latency panels are rendered as direct timeline panels in the same stack level as scheduling panels.
  2. No extra nested stack/container is present around power/latency panels.
  3. `npm run build:web` succeeds.

# PROPOSALS
- No.1 Remove nested chart wrappers [CONFIRMED]

## No.1 Remove nested chart wrappers
### CODE CHANGE
- Update `web_viewer/src/components/TimelineChart.tsx` to remove the nested `.chart-area` and `.timeline-stack` wrappers.
- Render timeline chart panels directly so they match scheduling panel spacing and padding.
- Keep the timeline panel header/body structure unchanged so visual style stays consistent.

### CONFIRMED
- Confirmed by code inspection and `npm run build:web` success.
- Powermonitor and latency panels now render at the same stack level as scheduling panels, eliminating the extra min-height and padding layer.
