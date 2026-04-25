import { useEffect, useMemo, useState } from "react";
import type { ChangeEvent } from "react";
import { layout } from "./constants/layout";
import { parsePayload } from "./domain/parsePayload";
import { useTimelineState } from "./state/useTimelineState";
import { usePanZoom } from "./state/usePanZoom";
import type { DownsampleMode, MetricKey, Point, Series, TimeSpanSource, Track } from "./types";
import TopBar, { type MovingAvgWindow } from "./components/TopBar";
import MetaBar from "./components/MetaBar";
import TrackControl from "./components/TrackControl";
import TimelineChart from "./components/TimelineChart";
import GanttPanel from "./components/GanttPanel";

type ChartTrackGroup = {
    id: string;
    title: string;
    tracks: Track[];
};

type TimelinePanelSpec =
    | {
        id: string;
        kind: "gantt";
        source: TimeSpanSource;
    }
    | {
        id: string;
        kind: "chart";
        sourceSeries: Series;
        group: ChartTrackGroup;
    };

const metricGroupLabels: Record<string, string> = {
    power: "power",
    latency: "latency",
    frequency: "frequency",
};

function getMetricGroup(key: MetricKey): keyof typeof metricGroupLabels {
    switch (key) {
        case "m2d":
        case "c2d":
        case "p2d":
        case "r2d":
            return "latency";
        case "cpu0_freq":
        case "cpu1_freq":
        case "gpu_freq":
        case "emc_freq":
            return "frequency";
        default:
            return "power";
    }
}

function buildChartTrackGroups(sourceSeries: Series, visibleTracks: Track[]): ChartTrackGroup[] {
    const grouped = new Map<keyof typeof metricGroupLabels, Track[]>();

    for (const track of visibleTracks) {
        const hasData = sourceSeries.points.some((point) => Number.isFinite(point[track.key]));
        if (!hasData) {
            continue;
        }

        const group = getMetricGroup(track.key);
        grouped.set(group, [...(grouped.get(group) || []), track]);
    }

    return (["power", "latency", "frequency"] as const)
        .map((group) => ({
            id: group,
            title: `${sourceSeries.label} · ${metricGroupLabels[group]}`,
            tracks: grouped.get(group) || [],
        }))
        .filter((group) => group.tracks.length > 0);
}

function normalizePanelOrder(panelIds: string[], order: string[]): string[] {
    const knownIds = new Set(panelIds);
    return [
        ...order.filter((id) => knownIds.has(id)),
        ...panelIds.filter((id) => !order.includes(id)),
    ];
}

// Apply moving average to power values
function applyMovingAverage(series: Series[], windowMs: number): Series[] {
    if (windowMs === 0) {
        return series;
    }

    const windowUs = windowMs * 1000; // Convert ms to microseconds

    return series.map((item) => {
        const points = item.points;
        if (points.length === 0) {
            return item;
        }

        const averagedPoints: Point[] = [];

        for (let i = 0; i < points.length; i++) {
            const currentPoint = points[i];
            const windowStart = currentPoint.timeUs - windowUs / 2;
            const windowEnd = currentPoint.timeUs + windowUs / 2;

            // Find points within the window
            let sumPower = 0;
            let sumVoltage = 0;
            let sumCurrent = 0;
            let countPower = 0;
            let countVoltage = 0;
            let countCurrent = 0;

            for (let j = 0; j < points.length; j++) {
                const p = points[j];
                if (p.timeUs >= windowStart && p.timeUs <= windowEnd) {
                    if (p.power != null) {
                        sumPower += p.power;
                        countPower++;
                    }
                    if (p.voltage != null) {
                        sumVoltage += p.voltage;
                        countVoltage++;
                    }
                    if (p.current != null) {
                        sumCurrent += p.current;
                        countCurrent++;
                    }
                }
            }

            averagedPoints.push({
                ...currentPoint,
                power: countPower > 0 ? sumPower / countPower : currentPoint.power,
                voltage: countVoltage > 0 ? sumVoltage / countVoltage : currentPoint.voltage,
                current: countCurrent > 0 ? sumCurrent / countCurrent : currentPoint.current,
            });
        }

        return { ...item, points: averagedPoints };
    });
}

export default function App() {
    const [downsampleMode, setDownsampleMode] = useState<DownsampleMode>("none");
    const [alignSources, setAlignSources] = useState(false);
    const [movingAvgWindow, setMovingAvgWindow] = useState<MovingAvgWindow>(0);
    const [panelOrder, setPanelOrder] = useState<string[]>([]);
    const [collapsedPanelIds, setCollapsedPanelIds] = useState<Set<string>>(() => new Set());
    const [draggingPanelId, setDraggingPanelId] = useState<string | null>(null);
    const {
        tracks,
        visibleTracks,
        series,
        points,
        timespans,
        range,
        metaText,
        statusText,
        setRange,
        fitAll,
        applyParsedData,
        clearDataWithStatus,
        toggleTrack,
        reorderTracks,
        setHoverStatus
    } = useTimelineState();

    const displaySeries = useMemo<Series[]>(() => {
        let result = series;

        // Apply source alignment
        if (alignSources) {
            const starts = result.map((item) => item.points[0]?.timeUs).filter((value): value is number => Number.isFinite(value));
            if (starts.length) {
                const anchor = Math.min(...starts);
                result = result.map((item) => {
                    const start = item.points[0]?.timeUs;
                    if (!Number.isFinite(start)) {
                        return item;
                    }
                    const delta = start - anchor;
                    return {
                        ...item,
                        points: item.points.map((point): Point => ({
                            ...point,
                            timeUs: point.timeUs - delta
                        }))
                    };
                });
            }
        }

        // Apply moving average
        if (movingAvgWindow > 0) {
            result = applyMovingAverage(result, movingAvgWindow);
        }

        return result;
    }, [alignSources, series, movingAvgWindow]);

    const displayPoints = useMemo(
        () => displaySeries.flatMap((item) => item.points).sort((a, b) => a.timeUs - b.timeUs),
        [displaySeries]
    );

    const { onWheel, onMouseDown } = usePanZoom({ points: displayPoints, range, setRange, layout });

    const panelSpecs = useMemo<TimelinePanelSpec[]>(() => {
        const ganttPanels: TimelinePanelSpec[] = timespans.map((source) => ({
            id: `gantt:${source.id}`,
            kind: "gantt",
            source,
        }));

        const chartPanels: TimelinePanelSpec[] = displaySeries.flatMap((sourceSeries) =>
            buildChartTrackGroups(sourceSeries, visibleTracks).map((group) => ({
                id: `chart:${sourceSeries.id}:${group.id}`,
                kind: "chart",
                sourceSeries,
                group,
            }))
        );

        return [...ganttPanels, ...chartPanels];
    }, [displaySeries, timespans, visibleTracks]);

    const panelIds = useMemo(() => panelSpecs.map((panel) => panel.id), [panelSpecs]);

    const orderedPanelSpecs = useMemo(() => {
        const panelById = new Map(panelSpecs.map((panel) => [panel.id, panel]));
        return normalizePanelOrder(panelIds, panelOrder)
            .map((id) => panelById.get(id))
            .filter((panel): panel is TimelinePanelSpec => Boolean(panel));
    }, [panelIds, panelOrder, panelSpecs]);

    useEffect(() => {
        setCollapsedPanelIds((prev) => {
            const knownIds = new Set(panelIds);
            const next = new Set([...prev].filter((id) => knownIds.has(id)));
            return next.size === prev.size ? prev : next;
        });
    }, [panelIds]);

    function togglePanelCollapsed(panelId: string): void {
        setCollapsedPanelIds((prev) => {
            const next = new Set(prev);
            if (next.has(panelId)) {
                next.delete(panelId);
            } else {
                next.add(panelId);
            }
            return next;
        });
    }

    function handlePanelDragStart(panelId: string): void {
        setDraggingPanelId(panelId);
        setPanelOrder((prev) => normalizePanelOrder(panelIds, prev));
    }

    function handlePanelDragOver(targetPanelId: string): void {
        if (!draggingPanelId || draggingPanelId === targetPanelId) {
            return;
        }

        setPanelOrder((prev) => {
            const next = normalizePanelOrder(panelIds, prev);
            const sourceIndex = next.indexOf(draggingPanelId);
            const targetIndex = next.indexOf(targetPanelId);
            if (sourceIndex < 0 || targetIndex < 0) {
                return next;
            }

            const [moved] = next.splice(sourceIndex, 1);
            next.splice(targetIndex, 0, moved);
            return next;
        });
    }

    function handlePanelDragEnd(): void {
        setDraggingPanelId(null);
    }

    // Calculate range from all data sources (points + timespans)
    const allTimePoints = useMemo(() => {
        const times: number[] = [];
        displayPoints.forEach(p => {
            times.push(p.timeUs);
        });
        timespans.forEach(ts => {
            ts.spans.forEach(span => {
                times.push(span.startUs, span.endUs);
            });
        });
        return times.sort((a, b) => a - b);
    }, [displayPoints, timespans]);

    useEffect(() => {
        if (!allTimePoints.length) {
            return;
        }
        const start = allTimePoints[0];
        let end = allTimePoints[allTimePoints.length - 1];
        if (end <= start) {
            end = start + 1;
        }
        setRange({ start, end });
    }, [alignSources, allTimePoints, setRange]);

    async function loadFromUrl(urlPath: string): Promise<void> {
        try {
            const response = await fetch(urlPath, { cache: "no-store" });
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            const parsed = parsePayload(await response.text());
            applyParsedData(parsed, urlPath);
        } catch (error) {
            const message = error instanceof Error ? error.message : String(error);
            clearDataWithStatus(`Failed to load JSON from URL: ${message}`);
        }
    }

    useEffect(() => {
        const dataPath = new URLSearchParams(window.location.search).get("data");
        if (dataPath) {
            loadFromUrl(dataPath);
        }
    }, []);

    function handleFileChange(event: ChangeEvent<HTMLInputElement>): void {
        const file = event.target.files?.[0];
        if (!file) {
            return;
        }
        file
            .text()
            .then((text) => {
                const parsed = parsePayload(text);
                applyParsedData(parsed, file.name);
            })
            .catch((error: unknown) => {
                const message = error instanceof Error ? error.message : String(error);
                clearDataWithStatus(`Failed to parse JSON: ${message}`);
            });
    }

    const hasAnyData = displayPoints.length > 0 || timespans.length > 0;

    return (
        <>
            <TopBar
                onFileChange={handleFileChange}
                onFitAll={() => fitAll(displayPoints)}
                downsampleMode={downsampleMode}
                onDownsampleModeChange={setDownsampleMode}
                alignSources={alignSources}
                onAlignSourcesChange={setAlignSources}
                movingAvgWindow={movingAvgWindow}
                onMovingAvgWindowChange={setMovingAvgWindow}
            />
            <MetaBar text={metaText} />
            {visibleTracks.length > 0 && displaySeries.length > 0 && (
                <TrackControl tracks={tracks} onToggle={toggleTrack} onReorder={reorderTracks} />
            )}
            <section
                className="chart-area"
                onWheel={onWheel}
                onMouseDown={onMouseDown}
            >
                <div className="timeline-stack" aria-label="Power timeline chart">
                    {!hasAnyData && (
                        <div className="timeline-empty">
                            <div className="timeline-empty-card">
                                <span className="timeline-empty-title">No data loaded</span>
                                <span className="timeline-empty-sub">Load a JSON file to view timeline</span>
                            </div>
                        </div>
                    )}

                    {orderedPanelSpecs.map((panel) => {
                        const commonPanelProps = {
                            panelId: panel.id,
                            isCollapsed: collapsedPanelIds.has(panel.id),
                            isDragging: draggingPanelId === panel.id,
                            onToggleCollapsed: togglePanelCollapsed,
                            onPanelDragStart: handlePanelDragStart,
                            onPanelDragOver: handlePanelDragOver,
                            onPanelDrop: handlePanelDragEnd,
                            onPanelDragEnd: handlePanelDragEnd,
                        };

                        if (panel.kind === "gantt") {
                            return (
                                <GanttPanel
                                    key={panel.id}
                                    {...commonPanelProps}
                                    source={panel.source}
                                    range={range}
                                    layout={layout}
                                />
                            );
                        }

                        const visiblePoints = panel.sourceSeries.points.filter(
                            (point) => point.timeUs >= range.start && point.timeUs <= range.end
                        );

                        return (
                            <TimelineChart
                                key={panel.id}
                                {...commonPanelProps}
                                layout={layout}
                                tracks={panel.group.tracks}
                                series={[panel.sourceSeries]}
                                points={visiblePoints}
                                range={range}
                                downsampleMode={downsampleMode}
                                onPointHover={setHoverStatus}
                                panelTitle={panel.group.title}
                            />
                        );
                    })}
                </div>
            </section>
            <section className="status-row">{statusText}</section>
        </>
    );
}
