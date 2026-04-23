import { useEffect, useMemo, useState } from "react";
import type { ChangeEvent } from "react";
import { layout } from "./constants/layout";
import { parsePayload } from "./domain/parsePayload";
import { useTimelineState } from "./state/useTimelineState";
import { usePanZoom } from "./state/usePanZoom";
import type { DownsampleMode, Point, Series } from "./types";
import TopBar, { type MovingAvgWindow } from "./components/TopBar";
import MetaBar from "./components/MetaBar";
import TrackControl from "./components/TrackControl";
import TimelineChart from "./components/TimelineChart";
import GanttPanel from "./components/GanttPanel";

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

                    {/* Gantt panels for time spans */}
                    {timespans.map((tsSource) => (
                        <GanttPanel
                            key={tsSource.id}
                            source={tsSource}
                            range={range}
                            layout={layout}
                        />
                    ))}

                    {/* Timeline charts for power data */}
                    {displayPoints.length > 0 && displaySeries.map((sourceSeries, index) => {
                        const visiblePoints = sourceSeries.points.filter(
                            (point) => point.timeUs >= range.start && point.timeUs <= range.end
                        );
                        return (
                            <TimelineChart
                                key={sourceSeries.id}
                                layout={layout}
                                tracks={visibleTracks}
                                series={[sourceSeries]}
                                points={visiblePoints}
                                range={range}
                                downsampleMode={downsampleMode}
                                onPointHover={setHoverStatus}
                            />
                        );
                    })}
                </div>
            </section>
            <section className="status-row">{statusText}</section>
        </>
    );
}
