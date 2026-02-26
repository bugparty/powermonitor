import { useEffect, useState } from "react";
import type { ChangeEvent } from "react";
import { layout } from "./constants/layout";
import { parsePayload } from "./domain/parsePayload";
import { useTimelineState } from "./state/useTimelineState";
import { usePanZoom } from "./state/usePanZoom";
import type { DownsampleMode } from "./types";
import TopBar from "./components/TopBar";
import MetaBar from "./components/MetaBar";
import TrackControl from "./components/TrackControl";
import TimelineChart from "./components/TimelineChart";

export default function App() {
    const [downsampleMode, setDownsampleMode] = useState<DownsampleMode>("none");
    const {
        tracks,
        visibleTracks,
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

    const { onWheel, onMouseDown } = usePanZoom({ points, range, setRange, layout });

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
                onFitAll={() => fitAll()}
                downsampleMode={downsampleMode}
                onDownsampleModeChange={setDownsampleMode}
            />
            <MetaBar text={metaText} />
            <TrackControl tracks={tracks} onToggle={toggleTrack} onReorder={reorderTracks} />
            <TimelineChart
                layout={layout}
                tracks={visibleTracks}
                points={points}
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
