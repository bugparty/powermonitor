// TimelinePanel — a single source's stacked chart card
// Uses inline SVG rendering (no Chart.js dep) to match visual language 1:1
function TimelinePanel({ source, tracks, range, onHover }) {
  const width = 640;   // viewBox width — actual render is via preserveAspectRatio
  const height = 220;
  const padL = 44, padR = 10, padT = 10, padB = 22;
  const innerW = width - padL - padR;
  const innerH = height - padT - padB;

  const visible = source.points.filter((p) => p.timeUs >= range.start && p.timeUs <= range.end);
  const visTracks = tracks.filter((t) => t.visible);

  const xOf = (t) => {
    if (range.end === range.start) return padL;
    return padL + ((t - range.start) / (range.end - range.start)) * innerW;
  };

  // Per-track y scale (auto-bounds from visible data)
  const bounds = {};
  visTracks.forEach((tr) => {
    const vals = visible.map((p) => p[tr.key]).filter((v) => Number.isFinite(v));
    if (vals.length === 0) { bounds[tr.key] = { min: 0, max: 1 }; return; }
    let min = Math.min(...vals), max = Math.max(...vals);
    if (min === max) { min -= 1; max += 1; }
    const pad = (max - min) * 0.1;
    bounds[tr.key] = { min: min - pad, max: max + pad };
  });

  const yOf = (tr, v) => {
    const b = bounds[tr.key];
    if (!b) return padT + innerH;
    return padT + innerH - ((v - b.min) / (b.max - b.min)) * innerH;
  };

  const buildPath = (tr) => {
    const pts = visible.filter((p) => Number.isFinite(p[tr.key]));
    if (pts.length < 2) return "";
    return pts.map((p, i) => `${i === 0 ? "M" : "L"}${xOf(p.timeUs).toFixed(2)},${yOf(tr, p[tr.key]).toFixed(2)}`).join(" ");
  };

  // Grid: 4 horizontal lines
  const gridYs = [0, 0.25, 0.5, 0.75, 1].map((f) => padT + f * innerH);

  // Time ticks
  const nTicks = 5;
  const tickTimes = Array.from({ length: nTicks }, (_, i) => range.start + ((range.end - range.start) / (nTicks - 1)) * i);

  const [hoverX, setHoverX] = React.useState(null);
  const svgRef = React.useRef(null);

  const onMove = (e) => {
    const rect = svgRef.current.getBoundingClientRect();
    const xFrac = (e.clientX - rect.left) / rect.width;
    const localX = xFrac * width;
    if (localX < padL || localX > padL + innerW) { setHoverX(null); onHover(null); return; }
    setHoverX(localX);
    const t = range.start + ((localX - padL) / innerW) * (range.end - range.start);
    // find nearest point
    let best = null, bd = Infinity;
    for (const p of visible) {
      const d = Math.abs(p.timeUs - t);
      if (d < bd) { bd = d; best = p; }
    }
    onHover(best);
  };
  const onLeave = () => { setHoverX(null); onHover(null); };

  return (
    <section className="pm-panel">
      <header className="pm-panel-head">
        <span className="pm-panel-title">{source.label}</span>
        <span className="pm-panel-meta">{visible.length.toLocaleString()} visible samples</span>
      </header>
      <div className="pm-panel-body">
        <svg ref={svgRef} viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none"
             onMouseMove={onMove} onMouseLeave={onLeave} style={{ display: "block", width: "100%", height: "100%" }}>
          {/* grid */}
          {gridYs.map((y, i) => (
            <line key={i} x1={padL} y1={y} x2={padL + innerW} y2={y} stroke="rgba(47,54,67,0.55)" strokeWidth="1" />
          ))}
          {/* y-axis labels — show primary track's scale on left */}
          {visTracks[0] && [0, 0.5, 1].map((f, i) => {
            const b = bounds[visTracks[0].key];
            const v = b.max - f * (b.max - b.min);
            return <text key={i} x={padL - 6} y={padT + f * innerH + 3} textAnchor="end" fontSize="10" fill="#94a0b8" fontFamily="JetBrains Mono, monospace">{Number.isFinite(v) ? (Math.abs(v) >= 100 ? v.toFixed(0) : v.toFixed(2)) : "—"}</text>;
          })}
          {/* x ticks */}
          {tickTimes.map((t, i) => (
            <g key={i}>
              <line x1={xOf(t)} y1={padT + innerH} x2={xOf(t)} y2={padT + innerH + 3} stroke="#2f3643" />
              <text x={xOf(t)} y={height - 6} textAnchor="middle" fontSize="10" fill="#94a0b8" fontFamily="JetBrains Mono, monospace">
                {(t / 1e6).toFixed(3)} s
              </text>
            </g>
          ))}
          {/* series paths */}
          {visTracks.map((tr) => (
            <path key={tr.key} d={buildPath(tr)} fill="none" stroke={tr.color} strokeWidth="1.6" vectorEffect="non-scaling-stroke" />
          ))}
          {/* hover crosshair */}
          {hoverX !== null && (
            <line x1={hoverX} y1={padT} x2={hoverX} y2={padT + innerH} stroke="#6b7280" strokeWidth="1" strokeDasharray="4 3" />
          )}
        </svg>
      </div>
    </section>
  );
}

Object.assign(window, { PMTimelinePanel: TimelinePanel });
