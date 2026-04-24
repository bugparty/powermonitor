import { useMemo, useState } from "react";
import { defaultTracks } from "../constants/tracks";
import type { ParsedPayload, Point, Range, Track } from "../types";

interface TimelineState {
    tracks: Track[];
    visibleTracks: Track[];
    points: Point[];
    range: Range;
    metaText: string;
    statusText: string;
    setRange: (range: Range) => void;
    fitAll: (inputPoints?: Point[]) => void;
    applyParsedData: (parsed: ParsedPayload, sourceLabel: string) => void;
    clearDataWithStatus: (message: string) => void;
    toggleTrack: (trackId: Track["id"]) => void;
    reorderTracks: (sourceId: Track["id"] | null, targetId: Track["id"]) => void;
    setHoverStatus: (point: Point | null) => void;
}

export function useTimelineState(): TimelineState {
    const [tracks, setTracks] = useState<Track[]>(defaultTracks);
    const [points, setPoints] = useState<Point[]>([]);
    const [range, setRange] = useState<Range>({ start: 0, end: 1 });
    const [metaText, setMetaText] = useState("No file loaded.");
    const [statusText, setStatusText] = useState("Select a powermonitor JSON file to render timeline.");

    const visibleTracks = useMemo(() => tracks.filter((track) => track.visible), [tracks]);

    function fitAll(inputPoints: Point[] = points): void {
        if (!inputPoints.length) {
            return;
        }
        const start = inputPoints[0].timeUs;
        let end = inputPoints[inputPoints.length - 1].timeUs;
        if (end <= start) {
            end = start + 1;
        }
        setRange({ start, end });
    }

    function applyParsedData(parsed: ParsedPayload, sourceLabel: string): void {
        setPoints(parsed.points);
        setMetaText(
            `schema=${parsed.meta.schema_version || "n/a"} protocol=${parsed.meta.protocol_version || "n/a"} samples=${parsed.points.length} stream_period_us=${parsed.meta.config?.stream_period_us ?? "n/a"}`
        );
        setStatusText(`Loaded ${sourceLabel}. Mouse wheel to zoom, drag to pan, hover to inspect.`);
        fitAll(parsed.points);
    }

    function clearDataWithStatus(message: string): void {
        setPoints([]);
        setStatusText(message);
    }

    function toggleTrack(trackId: Track["id"]): void {
        setTracks((prev) =>
            prev.map((track) => (track.id === trackId ? { ...track, visible: !track.visible } : track))
        );
    }

    function reorderTracks(sourceId: Track["id"] | null, targetId: Track["id"]): void {
        if (!sourceId || sourceId === targetId) {
            return;
        }
        setTracks((prev) => {
            const sourceIndex = prev.findIndex((track) => track.id === sourceId);
            const targetIndex = prev.findIndex((track) => track.id === targetId);
            if (sourceIndex < 0 || targetIndex < 0) {
                return prev;
            }
            const next = [...prev];
            const [item] = next.splice(sourceIndex, 1);
            next.splice(targetIndex, 0, item);
            return next;
        });
    }

    function setHoverStatus(point: Point | null): void {
        if (!point) {
            return;
        }
        setStatusText(
            `t=${(point.timeUs / 1e6).toFixed(6)}s seq=${point.seq} V=${point.voltage.toFixed(4)}V I=${point.current.toFixed(4)}A P=${point.power.toFixed(4)}W`
        );
    }

    return {
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
    };
}
