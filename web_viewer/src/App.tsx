import { useEffect, useRef } from "react";
import type { ChangeEvent } from "react";
import { layout } from "./constants/layout";
import { parsePayload } from "./domain/parsePayload";
import { useTimelineState } from "./state/useTimelineState";
import { usePanZoom } from "./state/usePanZoom";
import TopBar from "./components/TopBar";
import MetaBar from "./components/MetaBar";
import TrackControl from "./components/TrackControl";
import TimelineChart from "./components/TimelineChart";
import type { TimelinePoint } from "./types";

export default function App() {
    const svgRef = useRef<SVGSVGElement | null>(null);
    const {
        tracks,
        visibleTracks,
        points,
        range,
        metaText,
        statusText,
        hoverX,
        setRange,
        setHoverX,
        fitAll,
        applyParsedData,
        clearDataWithStatus,
        toggleTrack,
        reorderTracks,
        setHoverStatus
    } = useTimelineState();

    const { onWheel, onMouseDown } = usePanZoom({
        points,
        range,
        setRange,
        layout,
        svgRef
    });

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
            void loadFromUrl(dataPath);
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

    function handleHoverMove(localX: number | null, nearestPoint: TimelinePoint | null): void {
        setHoverX(localX);
        setHoverStatus(nearestPoint);
    }

    function handleHoverLeave(): void {
        setHoverX(null);
    }

    return (
        <>
            <TopBar onFileChange={handleFileChange} onFitAll={() => fitAll()} />
            <MetaBar text={metaText} />
            <TrackControl tracks={tracks} onToggle={toggleTrack} onReorder={reorderTracks} />
            <TimelineChart
                svgRef={svgRef}
                layout={layout}
                tracks={visibleTracks}
                points={points}
                range={range}
                hoverX={hoverX}
                onWheel={onWheel}
                onMouseDown={onMouseDown}
                onHoverMove={handleHoverMove}
                onHoverLeave={handleHoverLeave}
            />
            <section className="status-row">{statusText}</section>
        </>
    );
}
