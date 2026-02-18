const fileInput = document.getElementById("json-file");
const fitBtn = document.getElementById("fit-btn");
const svg = document.getElementById("timeline-svg");
const statusText = document.getElementById("status-text");
const metaPanel = document.getElementById("meta-panel");

const state = {
    points: [],
    range: { start: 0, end: 1 },
    dragging: false,
    dragStartX: 0,
    baseRange: null
};

const layout = {
    width: 1200,
    height: 620,
    left: 88,
    right: 20,
    top: 34,
    bottom: 36,
    laneGap: 16,
    laneCount: 3
};

const tracks = [
    { key: "voltage", label: "Voltage (V)", color: "var(--voltage)" },
    { key: "current", label: "Current (A)", color: "var(--current)" },
    { key: "power", label: "Power (W)", color: "var(--power)" }
];

function laneHeight() {
    return (layout.height - layout.top - layout.bottom - layout.laneGap * (layout.laneCount - 1)) / layout.laneCount;
}

function xScale(tUs) {
    const plotWidth = layout.width - layout.left - layout.right;
    const span = Math.max(1, state.range.end - state.range.start);
    return layout.left + ((tUs - state.range.start) / span) * plotWidth;
}

function yScale(value, min, max, laneTop) {
    const h = laneHeight();
    const span = Math.max(1e-9, max - min);
    return laneTop + h - ((value - min) / span) * h;
}

function clearSvg() {
    while (svg.firstChild) {
        svg.removeChild(svg.firstChild);
    }
}

function addSvg(tag, attrs, parent = svg) {
    const el = document.createElementNS("http://www.w3.org/2000/svg", tag);
    Object.entries(attrs).forEach(([k, v]) => el.setAttribute(k, String(v)));
    parent.appendChild(el);
    return el;
}

function formatValue(v) {
    if (!Number.isFinite(v)) {
        return "n/a";
    }
    return Math.abs(v) >= 100 ? v.toFixed(1) : v.toFixed(3);
}

function parsePayload(text) {
    const root = JSON.parse(text);
    const samples = Array.isArray(root.samples) ? root.samples : [];
    return {
        meta: root.meta || {},
        points: samples
            .map((sample) => {
                const e = sample.engineering || {};
                const v = Number(e.vbus_v);
                const c = Number(e.current_a);
                if (!Number.isFinite(v) || !Number.isFinite(c)) {
                    return null;
                }
                const t = Number(sample.device_timestamp_us ?? sample.host_timestamp_us ?? 0);
                return {
                    seq: Number(sample.seq ?? 0),
                    timeUs: Number.isFinite(t) ? t : 0,
                    voltage: v,
                    current: c,
                    power: v * c
                };
            })
            .filter(Boolean)
            .sort((a, b) => a.timeUs - b.timeUs)
    };
}

function computeBounds(values) {
    let min = Number.POSITIVE_INFINITY;
    let max = Number.NEGATIVE_INFINITY;
    values.forEach((v) => {
        if (v < min) min = v;
        if (v > max) max = v;
    });
    if (!Number.isFinite(min) || !Number.isFinite(max)) {
        return { min: 0, max: 1 };
    }
    if (min === max) {
        const pad = Math.max(0.1, Math.abs(min) * 0.05);
        return { min: min - pad, max: max + pad };
    }
    const pad = (max - min) * 0.1;
    return { min: min - pad, max: max + pad };
}

function visiblePoints() {
    return state.points.filter((p) => p.timeUs >= state.range.start && p.timeUs <= state.range.end);
}

function draw() {
    clearSvg();

    if (!state.points.length) {
        addSvg("text", {
            x: layout.width / 2,
            y: layout.height / 2,
            "text-anchor": "middle",
            fill: "#94a0b8",
            "font-size": 16
        }).textContent = "Load a JSON file to view timeline";
        return;
    }

    const points = visiblePoints();
    const h = laneHeight();
    const chartRight = layout.width - layout.right;

    tracks.forEach((track, index) => {
        const laneTop = layout.top + index * (h + layout.laneGap);
        addSvg("rect", {
            x: layout.left,
            y: laneTop,
            width: chartRight - layout.left,
            height: h,
            fill: "none",
            stroke: "#2f3643"
        });

        addSvg("text", { x: 10, y: laneTop + h / 2, class: "track-label" }).textContent = track.label;

        const trackValues = points.map((p) => p[track.key]);
        const bounds = computeBounds(trackValues);

        for (let i = 0; i <= 4; i += 1) {
            const y = laneTop + (h / 4) * i;
            addSvg("line", { x1: layout.left, y1: y, x2: chartRight, y2: y, class: "grid-line" });
            const value = bounds.max - ((bounds.max - bounds.min) * i) / 4;
            addSvg("text", {
                x: layout.left - 8,
                y: y + 4,
                "text-anchor": "end",
                class: "axis-label"
            }).textContent = formatValue(value);
        }

        if (points.length > 1) {
            const d = points
                .map((p, i) => `${i === 0 ? "M" : "L"}${xScale(p.timeUs)},${yScale(p[track.key], bounds.min, bounds.max, laneTop)}`)
                .join(" ");
            addSvg("path", {
                d,
                fill: "none",
                stroke: track.color,
                "stroke-width": 1.6,
                "vector-effect": "non-scaling-stroke"
            });
        }
    });

    const ticks = 8;
    const span = Math.max(1, state.range.end - state.range.start);
    for (let i = 0; i <= ticks; i += 1) {
        const t = state.range.start + (span * i) / ticks;
        const x = xScale(t);
        addSvg("line", { x1: x, y1: layout.top, x2: x, y2: layout.height - layout.bottom, class: "axis-line" });
        addSvg("text", {
            x,
            y: layout.height - 12,
            "text-anchor": "middle",
            class: "axis-label"
        }).textContent = `${(t / 1e6).toFixed(3)} s`;
    }

    const legendY = 18;
    tracks.forEach((track, idx) => {
        const x = layout.left + idx * 210;
        addSvg("line", { x1: x, y1: legendY, x2: x + 24, y2: legendY, stroke: track.color, "stroke-width": 2 });
        addSvg("text", { x: x + 30, y: legendY + 4, class: "legend" }).textContent = track.label;
    });

    const hover = addSvg("line", {
        x1: layout.left,
        y1: layout.top,
        x2: layout.left,
        y2: layout.height - layout.bottom,
        class: "hover-line",
        visibility: "hidden"
    });

    const overlay = addSvg("rect", {
        x: layout.left,
        y: layout.top,
        width: chartRight - layout.left,
        height: layout.height - layout.top - layout.bottom,
        fill: "transparent",
        cursor: "crosshair"
    });

    overlay.addEventListener("mousemove", (event) => {
        const pt = svg.createSVGPoint();
        pt.x = event.clientX;
        pt.y = event.clientY;
        const local = pt.matrixTransform(svg.getScreenCTM().inverse());
        if (local.x < layout.left || local.x > chartRight) {
            hover.setAttribute("visibility", "hidden");
            return;
        }
        hover.setAttribute("visibility", "visible");
        hover.setAttribute("x1", local.x);
        hover.setAttribute("x2", local.x);

        const t = state.range.start + ((local.x - layout.left) / (chartRight - layout.left)) * span;
        const nearest = points.reduce((best, p) => {
            if (!best) return p;
            return Math.abs(p.timeUs - t) < Math.abs(best.timeUs - t) ? p : best;
        }, null);
        if (nearest) {
            statusText.textContent = `t=${(nearest.timeUs / 1e6).toFixed(6)}s seq=${nearest.seq} V=${nearest.voltage.toFixed(4)}V I=${nearest.current.toFixed(4)}A P=${nearest.power.toFixed(4)}W`;
        }
    });

    overlay.addEventListener("mouseleave", () => {
        hover.setAttribute("visibility", "hidden");
    });
}

function fitAll() {
    if (!state.points.length) return;
    state.range.start = state.points[0].timeUs;
    state.range.end = state.points[state.points.length - 1].timeUs;
    if (state.range.end <= state.range.start) {
        state.range.end = state.range.start + 1;
    }
    draw();
}

function zoomAt(factor, localX) {
    if (!state.points.length) return;
    const minTime = state.points[0].timeUs;
    const maxTime = state.points[state.points.length - 1].timeUs;
    const plotWidth = layout.width - layout.left - layout.right;
    const cursorRatio = (localX - layout.left) / plotWidth;
    const span = Math.max(1, state.range.end - state.range.start);
    const anchor = state.range.start + span * cursorRatio;

    const newSpan = Math.max(200, span * factor);
    let start = anchor - newSpan * cursorRatio;
    let end = start + newSpan;

    if (start < minTime) {
        start = minTime;
        end = start + newSpan;
    }
    if (end > maxTime) {
        end = maxTime;
        start = end - newSpan;
    }
    state.range.start = Math.max(minTime, start);
    state.range.end = Math.min(maxTime, end);
    if (state.range.end <= state.range.start) {
        state.range.end = state.range.start + 1;
    }
    draw();
}

function bindInteractions() {
    svg.addEventListener("wheel", (event) => {
        if (!state.points.length) return;
        event.preventDefault();
        const pt = svg.createSVGPoint();
        pt.x = event.clientX;
        pt.y = event.clientY;
        const local = pt.matrixTransform(svg.getScreenCTM().inverse());
        if (local.x < layout.left || local.x > layout.width - layout.right) return;
        const factor = event.deltaY < 0 ? 0.8 : 1.25;
        zoomAt(factor, local.x);
    });

    svg.addEventListener("mousedown", (event) => {
        if (!state.points.length) return;
        state.dragging = true;
        state.dragStartX = event.clientX;
        state.baseRange = { ...state.range };
    });

    window.addEventListener("mousemove", (event) => {
        if (!state.dragging || !state.baseRange || !state.points.length) return;
        const plotWidth = layout.width - layout.left - layout.right;
        const dx = event.clientX - state.dragStartX;
        const span = state.baseRange.end - state.baseRange.start;
        const dt = (dx / plotWidth) * span;

        const minTime = state.points[0].timeUs;
        const maxTime = state.points[state.points.length - 1].timeUs;

        let start = state.baseRange.start - dt;
        let end = state.baseRange.end - dt;

        if (start < minTime) {
            end += minTime - start;
            start = minTime;
        }
        if (end > maxTime) {
            start -= end - maxTime;
            end = maxTime;
        }

        state.range.start = start;
        state.range.end = end;
        draw();
    });

    window.addEventListener("mouseup", () => {
        state.dragging = false;
        state.baseRange = null;
    });
}

fileInput.addEventListener("change", async (event) => {
    const file = event.target.files?.[0];
    if (!file) return;

    try {
        const text = await file.text();
        const parsed = parsePayload(text);
        state.points = parsed.points;
        if (!state.points.length) {
            throw new Error("No valid engineering samples found");
        }

        metaPanel.textContent = `schema=${parsed.meta.schema_version || "n/a"} protocol=${parsed.meta.protocol_version || "n/a"} samples=${state.points.length} stream_period_us=${parsed.meta.config?.stream_period_us ?? "n/a"}`;

        fitAll();
        statusText.textContent = `Loaded ${file.name}. Mouse wheel to zoom, drag to pan, hover to inspect.`;
    } catch (error) {
        statusText.textContent = `Failed to parse JSON: ${error.message}`;
        state.points = [];
        draw();
    }
});

fitBtn.addEventListener("click", fitAll);

bindInteractions();
draw();
