import React, { useState, useMemo } from "react";
import type { TimeSpanSource, TimeSpan, Range, Layout } from "../types";

interface GanttPanelProps {
    panelId: string;
    source: TimeSpanSource;
    range: Range;
    layout: Layout;
    isCollapsed?: boolean;
    isDragging?: boolean;
    onToggleCollapsed?: (panelId: string) => void;
    onPanelDragStart?: (panelId: string) => void;
    onPanelDragOver?: (panelId: string) => void;
    onPanelDrop?: () => void;
    onPanelDragEnd?: () => void;
}

const LANE_HEIGHT = 28;
const LANE_LABEL_WIDTH = 80;
const PANEL_PADDING = 10;
const BAR_HEIGHT = 16;
const BAR_GAP = 1;
const OVERLAP_OFFSET = 4; // Vertical offset for overlapping spans

function formatTimeUs(us: number): string {
    return `${(us / 1_000_000).toFixed(3)} s`;
}

// Compute vertical offsets for overlapping spans within each lane
function computeOverlapOffsets(spans: TimeSpan[]): Map<string, number> {
    const offsets = new Map<string, number>();

    // Group spans by lane
    const laneSpans = new Map<number, TimeSpan[]>();
    for (const span of spans) {
        const lane = span.lane;
        if (!laneSpans.has(lane)) laneSpans.set(lane, []);
        laneSpans.get(lane)!.push(span);
    }

    // For each lane, compute offsets using interval scheduling
    for (const [lane, laneSpanList] of laneSpans) {
        // Sort by start time
        const sorted = [...laneSpanList].sort((a, b) => a.startUs - b.startUs);

        // Track end times at each offset level
        const levels: number[] = [];

        for (const span of sorted) {
            // Find first level where this span doesn't overlap
            let level = 0;
            while (level < levels.length && levels[level] > span.startUs) {
                level++;
            }

            // Set offset for this span
            offsets.set(span.id, level * OVERLAP_OFFSET);

            // Update end time at this level
            if (level >= levels.length) {
                levels.push(span.endUs);
            } else {
                levels[level] = span.endUs;
            }
        }
    }

    return offsets;
}

export default function GanttPanel({
    panelId,
    source,
    range,
    layout,
    isCollapsed = false,
    isDragging = false,
    onToggleCollapsed,
    onPanelDragStart,
    onPanelDragOver,
    onPanelDrop,
    onPanelDragEnd
}: GanttPanelProps) {
    const { spans, laneCount, laneLabels, label: panelLabel } = source;
    const [hoveredSpan, setHoveredSpan] = useState<string | null>(null);

    // Calculate visible spans within range
    const visibleSpans = useMemo(() => {
        if (isCollapsed) {
            return [];
        }
        return spans.filter((span) => span.endUs >= range.start && span.startUs <= range.end);
    }, [isCollapsed, spans, range.start, range.end]);

    // Compute overlap offsets for visible spans
    const overlapOffsets = useMemo(() => computeOverlapOffsets(visibleSpans), [visibleSpans]);

    // Calculate SVG dimensions (need to account for potential overlap stacking)
    const chartWidth = layout.width - LANE_LABEL_WIDTH - PANEL_PADDING * 2;
    const chartHeight = laneCount * LANE_HEIGHT;
    const totalHeight = chartHeight + 24; // +24 for axis labels

    // Time scale function
    const timeToX = (timeUs: number): number => {
        const span = range.end - range.start;
        if (span <= 0) return LANE_LABEL_WIDTH;
        return LANE_LABEL_WIDTH + ((timeUs - range.start) / span) * chartWidth;
    };

    // Extract numeric ID from span id (e.g., "vio_3" -> "3")
    const getSpanNumber = (id: string): string => {
        const parts = id.split('_');
        return parts[parts.length - 1] || id;
    };

    // Render a single span bar
    const renderSpan = (span: TimeSpan) => {
        const x = timeToX(span.startUs);
        const endX = timeToX(span.endUs);
        const width = Math.max(2, endX - x);
        const overlapOffset = overlapOffsets.get(span.id) || 0;
        const y = 6 + span.lane * LANE_HEIGHT + BAR_GAP + overlapOffset;
        const isHovered = hoveredSpan === span.id;

        // Show span number when bar is wide enough
        const showNumber = width > 25;

        return (
            <g key={span.id}>
                <rect
                    x={x}
                    y={y}
                    width={width}
                    height={BAR_HEIGHT}
                    rx={3}
                    fill={span.color}
                    fillOpacity={isHovered ? 0.9 : 0.5}
                    stroke={span.color}
                    strokeWidth={isHovered ? 2 : 1}
                    strokeOpacity={isHovered ? 1 : 0.7}
                    style={{ cursor: "pointer" }}
                    onMouseEnter={() => setHoveredSpan(span.id)}
                    onMouseLeave={() => setHoveredSpan(null)}
                />
                {showNumber && (
                    <text
                        x={x + width / 2}
                        y={y + 11}
                        textAnchor="middle"
                        fontSize={9}
                        fontFamily="var(--font-mono)"
                        fontWeight={600}
                        fill="#fff"
                        fillOpacity={0.95}
                        style={{ pointerEvents: "none" }}
                    >
                        {getSpanNumber(span.id)}
                    </text>
                )}
            </g>
        );
    };

    // Render horizontal grid lines
    const renderGridLines = () => {
        const lines = [];
        for (let i = 0; i <= laneCount; i++) {
            const y = 6 + i * LANE_HEIGHT;
            lines.push(
                <line
                    key={`grid-${i}`}
                    x1={LANE_LABEL_WIDTH}
                    y1={y}
                    x2={LANE_LABEL_WIDTH + chartWidth}
                    y2={y}
                    stroke="rgba(47,54,67,0.35)"
                    strokeWidth={1}
                />
            );
        }
        return lines;
    };

    // Render lane labels
    const renderLaneLabels = () => {
        const labels = [];
        for (let i = 0; i < laneCount; i++) {
            const y = 6 + i * LANE_HEIGHT + BAR_GAP + BAR_HEIGHT / 2 + 4;
            labels.push(
                <text
                    key={`lane-${i}`}
                    x={LANE_LABEL_WIDTH - 8}
                    y={y}
                    textAnchor="end"
                    fontSize={10}
                    fontFamily="var(--font-sans)"
                    fill="var(--muted)"
                >
                    {laneLabels[i] || `lane ${i}`}
                </text>
            );
        }
        return labels;
    };

    // Render time axis ticks
    const renderTimeAxis = () => {
        const ticks: React.ReactNode[] = [];
        const span = range.end - range.start;
        const tickCount = 6;
        const step = span / (tickCount - 1);

        for (let i = 0; i < tickCount; i++) {
            const timeUs = range.start + step * i;
            const x = timeToX(timeUs);
            ticks.push(
                <text
                    key={`tick-${i}`}
                    x={x}
                    y={totalHeight - 4}
                    textAnchor="middle"
                    fontSize={10}
                    fontFamily="var(--font-mono)"
                    fill="var(--muted)"
                >
                    {formatTimeUs(timeUs)}
                </text>
            );
        }
        return ticks;
    };

    // Find hovered span details
    const hoveredSpanData = hoveredSpan ? visibleSpans.find(s => s.id === hoveredSpan) : null;
    const formatDuration = (us: number) => {
        if (us >= 1_000_000) return `${(us / 1_000_000).toFixed(3)} s`;
        if (us >= 1_000) return `${(us / 1_000).toFixed(1)} ms`;
        return `${us.toFixed(0)} µs`;
    };
    const formatStartTime = (us: number) => {
        return `${(us / 1_000_000).toFixed(3)} s`;
    };

    return (
        <section
            className={`timeline-panel${isCollapsed ? " timeline-panel-collapsed" : ""}${isDragging ? " timeline-panel-dragging" : ""}`}
            onDragOver={(event) => {
                event.preventDefault();
                onPanelDragOver?.(panelId);
            }}
            onDrop={(event) => {
                event.preventDefault();
                onPanelDrop?.();
            }}
        >
            <header className="timeline-panel-header">
                <div className="timeline-panel-heading">
                    <button
                        type="button"
                        className="timeline-panel-drag-handle"
                        draggable
                        aria-label={`Drag ${panelLabel}`}
                        title="Drag to reorder"
                        onMouseDown={(event) => event.stopPropagation()}
                        onDragStart={(event) => {
                            event.dataTransfer.effectAllowed = "move";
                            event.dataTransfer.setData("text/plain", panelId);
                            onPanelDragStart?.(panelId);
                        }}
                        onDragEnd={onPanelDragEnd}
                    >
                        ::
                    </button>
                    <span className="timeline-panel-title">{panelLabel}</span>
                </div>
                <div className="timeline-panel-actions">
                    {!isCollapsed && (
                        <span className="timeline-panel-meta">
                            {hoveredSpanData ? (
                                <>
                                    {hoveredSpanData.id} · {formatDuration(hoveredSpanData.endUs - hoveredSpanData.startUs)} ·
                                    start {formatStartTime(hoveredSpanData.startUs)}
                                </>
                            ) : (
                                `${visibleSpans.length} spans · ${laneCount} lanes`
                            )}
                        </span>
                    )}
                    <button
                        type="button"
                        className="timeline-panel-toggle"
                        aria-label={`${isCollapsed ? "Expand" : "Collapse"} ${panelLabel}`}
                        onMouseDown={(event) => event.stopPropagation()}
                        onClick={() => onToggleCollapsed?.(panelId)}
                    >
                        {isCollapsed ? "+" : "-"}
                    </button>
                </div>
            </header>
            {!isCollapsed && (
                <div className="timeline-panel-chart" style={{ height: totalHeight }}>
                    <svg
                        viewBox={`0 0 ${layout.width} ${totalHeight}`}
                        preserveAspectRatio="none"
                        style={{ width: "100%", height: "100%", display: "block" }}
                    >
                        {/* Lane labels */}
                        <g>{renderLaneLabels()}</g>

                        {/* Grid lines */}
                        <g>{renderGridLines()}</g>

                        {/* Span bars */}
                        <g>{visibleSpans.map(renderSpan)}</g>

                        {/* Time axis */}
                        <g>{renderTimeAxis()}</g>
                    </svg>
                </div>
            )}
        </section>
    );
}
