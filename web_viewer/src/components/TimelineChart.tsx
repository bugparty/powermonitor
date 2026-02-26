import React, { useMemo } from "react";
import type { MouseEvent, WheelEvent } from "react";
import { Line } from "react-chartjs-2";
import {
    Chart as ChartJS,
    LineElement,
    PointElement,
    LinearScale,
    Tooltip,
    Legend as ChartLegend,
    Filler
} from "chart.js";
import type { Layout, Point as DataPoint, Range, Track, DownsampleMode } from "../types";
import { formatTimeUs } from "../domain/formatters";
import { findStartIndex, findEndIndex } from "../domain/search";

ChartJS.register(LineElement, PointElement, LinearScale, Tooltip, ChartLegend, Filler);

interface TimelineChartProps {
    layout: Layout;
    tracks: Track[];
    points: DataPoint[];
    range: Range;
    downsampleMode: DownsampleMode;
    onWheel: (event: WheelEvent<HTMLElement>) => void;
    onMouseDown: (event: MouseEvent<HTMLElement>) => void;
    onPointHover: (point: DataPoint | null) => void;
}

export default function TimelineChart({
    layout,
    tracks,
    points,
    range,
    downsampleMode,
    onWheel,
    onMouseDown,
    onPointHover
}: TimelineChartProps) {
    const visiblePoints = useMemo(() => {
        if (points.length === 0) return [];
        const startIdx = findStartIndex(points, range.start);
        const endIdx = findEndIndex(points, range.end);
        return startIdx <= endIdx ? points.slice(startIdx, endIdx + 1) : [];
    }, [points, range]);

    type RawPoint = { x: number; y: number; _point: DataPoint };

    function minMaxDownsample(data: RawPoint[], targetCount: number): RawPoint[] {
        if (data.length <= targetCount || targetCount <= 0) return data;
        const bucketSize = Math.ceil(data.length / targetCount);
        const result: RawPoint[] = [];
        for (let i = 0; i < data.length; i += bucketSize) {
            const bucket = data.slice(i, i + bucketSize);
            if (!bucket.length) continue;
            let min = bucket[0];
            let max = bucket[0];
            for (const p of bucket) {
                if (p.y < min.y) min = p;
                if (p.y > max.y) max = p;
            }
            if (min.x <= max.x) {
                result.push(min, max);
            } else {
                result.push(max, min);
            }
        }
        return result;
    }

    function lttbDownsample(data: RawPoint[], threshold: number): RawPoint[] {
        if (threshold >= data.length || threshold <= 0) return data;
        const sampled: RawPoint[] = [];
        const bucketSize = (data.length - 2) / (threshold - 2);
        let a = 0;
        sampled.push(data[a]);

        for (let i = 0; i < threshold - 2; i++) {
            const rangeStart = Math.floor((i + 1) * bucketSize) + 1;
            const rangeEnd = Math.floor((i + 2) * bucketSize) + 1;
            const rangeEndClamped = Math.min(rangeEnd, data.length);

            let avgX = 0;
            let avgY = 0;
            const avgRangeStart = rangeStart;
            const avgRangeEnd = rangeEndClamped;
            const avgRangeLength = avgRangeEnd - avgRangeStart;
            for (let j = avgRangeStart; j < avgRangeEnd; j++) {
                avgX += data[j].x;
                avgY += data[j].y;
            }
            avgX /= Math.max(1, avgRangeLength);
            avgY /= Math.max(1, avgRangeLength);

            const rangeOffs = Math.floor(i * bucketSize) + 1;
            const rangeTo = Math.min(Math.floor((i + 1) * bucketSize) + 1, data.length - 1);

            let maxArea = -1;
            let nextA = rangeOffs;

            for (let j = rangeOffs; j < rangeTo; j++) {
                const pointA = data[a];
                const point = data[j];
                const area =
                    Math.abs(
                        (pointA.x - avgX) * (point.y - pointA.y) - (pointA.x - point.x) * (avgY - pointA.y)
                    ) * 0.5;
                if (area > maxArea) {
                    maxArea = area;
                    nextA = j;
                }
            }

            sampled.push(data[nextA]);
            a = nextA;
        }

        sampled.push(data[data.length - 1]);
        return sampled;
    }

    const chartData = useMemo(() => {
        const targetPoints = Math.max(200, Math.floor(layout.width / 2));

        return {
            datasets: tracks.map((track) => {
                const base: RawPoint[] = visiblePoints.map((point) => ({
                    x: point.timeUs,
                    y: point[track.key],
                    _point: point
                }));

                let data: RawPoint[];
                if (downsampleMode === "min-max") {
                    data = minMaxDownsample(base, targetPoints);
                } else if (downsampleMode === "lttb") {
                    data = lttbDownsample(base, targetPoints);
                } else {
                    data = base;
                }

                return {
                    label: track.label,
                    borderColor: track.color,
                    backgroundColor: track.color,
                    data,
                    tension: 0,
                    pointRadius: 0,
                    borderWidth: 1.6
                };
            })
        };
    }, [tracks, visiblePoints, downsampleMode, layout.width]);

    const options = useMemo(
        () => ({
            responsive: true,
            maintainAspectRatio: false,
            interaction: {
                mode: "nearest" as const,
                intersect: false
            },
            plugins: {
                legend: {
                    display: true,
                    labels: {
                        color: "var(--muted)",
                        boxWidth: 12,
                        usePointStyle: true
                    }
                },
                tooltip: {
                    enabled: true,
                    callbacks: {
                        label(ctx: any) {
                            const label = ctx.dataset.label || "";
                            const value = ctx.parsed.y;
                            return `${label}: ${Number.isFinite(value) ? value.toFixed(4) : "n/a"}`;
                        }
                    }
                }
            },
            scales: {
                x: {
                    type: "linear" as const,
                    min: range.start,
                    max: range.end,
                    ticks: {
                        color: "var(--muted)",
                        callback: (value: string | number) => formatTimeUs(Number(value))
                    },
                    grid: {
                        color: "rgba(47,54,67,0.5)"
                    }
                },
                y: {
                    type: "linear" as const,
                    ticks: {
                        color: "var(--muted)"
                    },
                    grid: {
                        color: "rgba(47,54,67,0.55)"
                    }
                }
            },
            animation: {
                duration: 0
            } as const,
            onHover: (_event: unknown, elements: any[]) => {
                if (!elements?.length) {
                    onPointHover(null);
                    return;
                }
                const el = elements[0];
                const raw: any = (el as any).element?.$context?.raw ?? (el as any).raw;
                const point: DataPoint | null = raw?._point ?? null;
                onPointHover(point);
            }
        }),
        [onPointHover, range.end, range.start]
    );

    return (
        <section className="chart-area">
            <div
                id="timeline-svg"
                aria-label="Power timeline chart"
                onWheel={onWheel}
                onMouseDown={onMouseDown}
            >
                {!points.length && (
                    <div
                        style={{
                            width: "100%",
                            height: "100%",
                            display: "flex",
                            alignItems: "center",
                            justifyContent: "center",
                            color: "#94a0b8",
                            fontSize: 16
                        }}
                    >
                        Load a JSON file to view timeline
                    </div>
                )}
                {points.length > 0 && <Line data={chartData} options={options} />}
            </div>
        </section>
    );
}
