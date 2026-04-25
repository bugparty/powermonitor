// GanttPanel — swimlane / Gantt track that plugs into the Power Monitor chart stack.
// One row per lane (core / thread / process). Each bar = a task time-span.
// Visual language matches TimelinePanel: accent-tinted border, panel gradient,
// hairline grid, mono axis labels, hover crosshair.
function GanttPanel({ title, lanes, tasks, range, groupColors, onHover }) {
  const width = 640;
  const rowH = 26;
  const padL = 78, padR = 12, padT = 8, padB = 22;
  const innerW = width - padL - padR;
  const innerH = lanes.length * rowH;
  const height = padT + innerH + padB;

  const xOf = (t) => padL + ((t - range.start) / Math.max(1, range.end - range.start)) * innerW;

  const nTicks = 6;
  const tickTimes = Array.from({ length: nTicks }, (_, i) =>
    range.start + ((range.end - range.start) / (nTicks - 1)) * i
  );

  const [hoverX, setHoverX] = React.useState(null);
  const [hoverTask, setHoverTask] = React.useState(null);
  const svgRef = React.useRef(null);

  const onMove = (e) => {
    const rect = svgRef.current.getBoundingClientRect();
    const xFrac = (e.clientX - rect.left) / rect.width;
    const yFrac = (e.clientY - rect.top) / rect.height;
    const localX = xFrac * width;
    const localY = yFrac * height;
    if (localX < padL || localX > padL + innerW) {
      setHoverX(null); setHoverTask(null); onHover?.(null); return;
    }
    setHoverX(localX);
    const t = range.start + ((localX - padL) / innerW) * (range.end - range.start);
    const laneIdx = Math.floor((localY - padT) / rowH);
    const lane = lanes[laneIdx];
    if (!lane) { setHoverTask(null); onHover?.(null); return; }
    const found = tasks.find((k) => k.lane === lane.id && t >= k.start && t <= k.end);
    setHoverTask(found || null);
    onHover?.(found || null);
  };
  const onLeave = () => { setHoverX(null); setHoverTask(null); onHover?.(null); };

  return (
    <section className="pm-panel">
      <header className="pm-panel-head">
        <span className="pm-panel-title">{title}</span>
        <span className="pm-panel-meta">
          {tasks.filter((k) => k.end >= range.start && k.start <= range.end).length} visible spans ·
          {" "}{lanes.length} lanes
        </span>
      </header>
      <div className="pm-panel-body" style={{ height: height + 4 }}>
        <svg ref={svgRef} viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none"
             onMouseMove={onMove} onMouseLeave={onLeave}
             style={{ display: "block", width: "100%", height: "100%" }}>
          {/* lane separators + labels */}
          {lanes.map((lane, i) => {
            const y = padT + i * rowH;
            return (
              <g key={lane.id}>
                <line x1={padL} y1={y} x2={padL + innerW} y2={y} stroke="rgba(47,54,67,0.55)" strokeWidth="1" />
                <text x={padL - 8} y={y + rowH / 2 + 3} textAnchor="end"
                      fontSize="10.5" fill="#94a0b8" fontFamily="JetBrains Mono, monospace">
                  {lane.label}
                </text>
              </g>
            );
          })}
          <line x1={padL} y1={padT + innerH} x2={padL + innerW} y2={padT + innerH}
                stroke="rgba(47,54,67,0.55)" strokeWidth="1" />
          {/* vertical gridlines on major ticks */}
          {tickTimes.map((t, i) => (
            <line key={`g-${i}`} x1={xOf(t)} y1={padT} x2={xOf(t)} y2={padT + innerH}
                  stroke="rgba(47,54,67,0.35)" strokeWidth="1" />
          ))}
          {/* time ticks */}
          {tickTimes.map((t, i) => (
            <g key={`t-${i}`}>
              <line x1={xOf(t)} y1={padT + innerH} x2={xOf(t)} y2={padT + innerH + 3} stroke="#2f3643" />
              <text x={xOf(t)} y={height - 6} textAnchor="middle"
                    fontSize="10" fill="#94a0b8" fontFamily="JetBrains Mono, monospace">
                {(t / 1e6).toFixed(3)} s
              </text>
            </g>
          ))}
          {/* task bars */}
          {tasks.map((k, i) => {
            const laneIdx = lanes.findIndex((l) => l.id === k.lane);
            if (laneIdx < 0) return null;
            if (k.end < range.start || k.start > range.end) return null;
            const x1 = Math.max(padL, xOf(k.start));
            const x2 = Math.min(padL + innerW, xOf(k.end));
            const w = Math.max(1, x2 - x1);
            const y = padT + laneIdx * rowH + 3;
            const h = rowH - 6;
            const color = groupColors[k.group] || "#3f8cff";
            const isHover = hoverTask === k;
            return (
              <g key={i}>
                <rect x={x1} y={y} width={w} height={h} rx="3"
                      fill={color} fillOpacity={isHover ? 0.95 : 0.72}
                      stroke={color} strokeOpacity={isHover ? 1 : 0.9} strokeWidth="1" />
                {w > 28 && (
                  <text x={x1 + 6} y={y + h / 2 + 3.5} fontSize="10.5"
                        fill="#0b0d10" fontFamily="JetBrains Mono, monospace" fontWeight="600"
                        style={{ pointerEvents: "none" }}>
                    {k.label}
                  </text>
                )}
              </g>
            );
          })}
          {/* hover crosshair */}
          {hoverX !== null && (
            <line x1={hoverX} y1={padT} x2={hoverX} y2={padT + innerH}
                  stroke="#6b7280" strokeWidth="1" strokeDasharray="4 3" />
          )}
        </svg>
      </div>
    </section>
  );
}

// Sample scheduling data — 8 cores, 2 groups
function makeSchedulingData() {
  const lanes = Array.from({ length: 8 }, (_, i) => ({ id: `c${i}`, label: `core ${i}` }));
  // Task list: { lane, group, start (us), end (us), label }
  // Times in microseconds to match the rest of the viewer (0 → 7_000_000 = 7 s window)
  const tasks = [
    // core 0
    { lane: "c0", group: "io",    start: 0,        end: 300_000,   label: "T24" },
    { lane: "c0", group: "io",    start: 300_000,  end: 500_000,   label: "T18" },
    { lane: "c0", group: "io",    start: 500_000,  end: 700_000,   label: "T12" },
    { lane: "c0", group: "io",    start: 700_000,  end: 3_300_000, label: "T29" },
    // core 1
    { lane: "c1", group: "compute", start: 4_000_000, end: 6_500_000, label: "T39" },
    { lane: "c1", group: "compute", start: 6_500_000, end: 7_000_000, label: "T7" },
    // core 2
    { lane: "c2", group: "io",    start: 100_000,   end: 800_000,  label: "T27" },
    { lane: "c2", group: "io",    start: 800_000,   end: 1_300_000,label: "T11" },
    { lane: "c2", group: "compute", start: 3_000_000, end: 4_300_000, label: "T31" },
    { lane: "c2", group: "compute", start: 6_000_000, end: 7_000_000, label: "T5"  },
    // core 3
    { lane: "c3", group: "io",    start: 0,         end: 3_000_000, label: "T0"  },
    { lane: "c3", group: "io",    start: 3_000_000, end: 3_500_000, label: "T17" },
    { lane: "c3", group: "io",    start: 3_500_000, end: 4_600_000, label: "T30" },
    // core 4
    { lane: "c4", group: "compute", start: 3_000_000, end: 4_300_000, label: "T38" },
    // core 5
    { lane: "c5", group: "io",    start: 0,         end: 800_000,  label: "T7" },
    { lane: "c5", group: "compute", start: 2_000_000, end: 7_000_000, label: "T34" },
    // core 6
    { lane: "c6", group: "io",    start: 0,         end: 1_800_000, label: "T25" },
    { lane: "c6", group: "compute", start: 5_000_000, end: 7_000_000, label: "T35" },
    // core 7 — idle mostly
    { lane: "c7", group: "io",    start: 0,         end: 100_000,  label: "T1" },
  ];
  return { lanes, tasks };
}

const GANTT_GROUP_COLORS = {
  io:      "#22c55e",  // voltage-green
  compute: "#f59e0b",  // current-amber
  wait:    "#6b7280",
  irq:     "#a855f7",
  idle:    "#2f3643",
};

Object.assign(window, { PMGanttPanel: GanttPanel, makeSchedulingData, GANTT_GROUP_COLORS });
