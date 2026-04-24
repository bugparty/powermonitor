import { useEffect, useMemo, useState } from "react";
import type { ChangeEvent } from "react";
import { layout } from "./constants/layout";
import { parsePayload } from "./domain/parsePayload";
import { useTimelineState } from "./state/useTimelineState";
import { usePanZoom } from "./state/usePanZoom";
import type { DownsampleMode, Point, Series } from "./types";
import TopBar from "./components/TopBar";
import MetaBar from "./components/MetaBar";
import TrackControl from "./components/TrackControl";
import TimelineChart from "./components/TimelineChart";

export default function App() {
    const [downsampleMode, setDownsampleMode] = useState<DownsampleMode>("none");
    const [alignSources, setAlignSources] = useState(false);
    const {
        tracks,
        visibleTracks,
        series,
        points,
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
        if (!alignSources) {
            return series;
        }
        const starts = series.map((item) => item.points[0]?.timeUs).filter((value): value is number => Number.isFinite(value));
        if (!starts.length) {
            return series;
        }
        const anchor = Math.min(...starts);
        return series.map((item) => {
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
    }, [alignSources, series]);

    const displayPoints = useMemo(
        () => displaySeries.flatMap((item) => item.points).sort((a, b) => a.timeUs - b.timeUs),
        [displaySeries]
    );

    const { onWheel, onMouseDown } = usePanZoom({ points: displayPoints, range, setRange, layout });

    useEffect(() => {
        if (!displayPoints.length) {
            return;
        }
        const start = displayPoints[0].timeUs;
        let end = displayPoints[displayPoints.length - 1].timeUs;
        if (end <= start) {
            end = start + 1;
        }
        setRange({ start, end });
    }, [alignSources, displayPoints, setRange]);

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

    return (
        <>
            <TopBar
                onFileChange={handleFileChange}
                onFitAll={() => fitAll(displayPoints)}
                downsampleMode={downsampleMode}
                onDownsampleModeChange={setDownsampleMode}
                alignSources={alignSources}
                onAlignSourcesChange={setAlignSources}
            />
            <MetaBar text={metaText} />
            <TrackControl tracks={tracks} onToggle={toggleTrack} onReorder={reorderTracks} />
            <TimelineChart
                layout={layout}
                tracks={visibleTracks}
                series={displaySeries}
                points={displayPoints}
                range={range}
                downsampleMode={downsampleMode}
                onWheel={onWheel}
                onMouseDown={onMouseDown}
                onPointHover={setHoverStatus}
            />
            <section className="status-row">{statusText}</section>
        </>
    );
}
