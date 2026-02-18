import { useMemo } from "react";
import type { MouseEvent as ReactMouseEvent, RefObject, WheelEvent } from "react";
import { buildTimeTicks } from "../chart-core/ticks";
import { laneHeight } from "../chart-core/bounds";
import { createXScale } from "../chart-core/scales";
import TrackLane from "./chart/TrackLane";
import TimeAxis from "./chart/TimeAxis";
import Legend from "./chart/Legend";
import HoverLayer from "./chart/HoverLayer";
import type { LayoutConfig, TimeRange, TimelinePoint, Track } from "../types";

interface TimelineChartProps {
    svgRef: RefObject<SVGSVGElement | null>;
    layout: LayoutConfig;
    tracks: Track[];
    points: TimelinePoint[];
    range: TimeRange;
    hoverX: number | null;
    onWheel: (event: WheelEvent<SVGSVGElement>) => void;
    onMouseDown: (event: ReactMouseEvent<SVGSVGElement>) => void;
    onHoverMove: (localX: number | null, nearestPoint: TimelinePoint | null) => void;
    onHoverLeave: () => void;
}

export default function TimelineChart({
    svgRef,
    layout,
    tracks,
    points,
    range,
    hoverX,
    onWheel,
    onMouseDown,
    onHoverMove,
    onHoverLeave
}: TimelineChartProps) {
    const chartRight = layout.width - layout.right;
    const plotWidth = chartRight - layout.left;
    const visiblePoints = useMemo(
        () => points.filter((point) => point.timeUs >= range.start && point.timeUs <= range.end),
        [points, range]
    );
    const ticks = useMemo(() => buildTimeTicks(range.start, range.end), [range.start, range.end]);
    const laneCount = Math.max(1, tracks.length);
    const currentLaneHeight = laneHeight(laneCount, layout);
    const xScale = useMemo(() => createXScale(range, layout.left, plotWidth), [range, layout.left, plotWidth]);

    function handleHoverMove(localX: number | null): void {
        if (localX === null) {
            onHoverMove(null, null);
            return;
        }
        const span = Math.max(1, range.end - range.start);
        const timeUs = range.start + ((localX - layout.left) / plotWidth) * span;
        const nearestPoint = visiblePoints.reduce<TimelinePoint | null>((best, point) => {
            if (!best) return point;
            return Math.abs(point.timeUs - timeUs) < Math.abs(best.timeUs - timeUs) ? point : best;
        }, null);
        onHoverMove(localX, nearestPoint);
    }

    return (
        <section className="chart-area">
            <svg
                ref={svgRef}
                id="timeline-svg"
                viewBox={`0 0 ${layout.width} ${layout.height}`}
                aria-label="Power timeline chart"
                onWheel={onWheel}
                onMouseDown={onMouseDown}
            >
                {!points.length && (
                    <text x={layout.width / 2} y={layout.height / 2} textAnchor="middle" fill="#94a0b8" fontSize="16">
                        Load a JSON file to view timeline
                    </text>
                )}

                {points.length > 0 &&
                    tracks.map((track, index) => {
                        const laneTop = layout.top + index * (currentLaneHeight + layout.laneGap);
                        return (
                            <TrackLane
                                key={track.id}
                                track={track}
                                laneTop={laneTop}
                                laneHeight={currentLaneHeight}
                                layout={layout}
                                chartRight={chartRight}
                                visiblePoints={visiblePoints}
                                xScale={xScale}
                            />
                        );
                    })}

                {points.length > 0 && <TimeAxis layout={layout} timeTicks={ticks} xScale={xScale} />}

                {points.length > 0 && <Legend tracks={tracks} layout={layout} />}

                {points.length > 0 && (
                    <HoverLayer
                        svgRef={svgRef}
                        layout={layout}
                        chartRight={chartRight}
                        hoverX={hoverX}
                        onHoverMove={handleHoverMove}
                        onHoverLeave={onHoverLeave}
                    />
                )}
            </svg>
        </section>
    );
}
