import { useMemo, useState } from "react";
import type { Dispatch, SetStateAction } from "react";
import { defaultTracks } from "../constants/tracks";
import type { ParsedPayload, TimeRange, TimelinePoint, Track } from "../types";

interface TimelineState {
    tracks: Track[];
    visibleTracks: Track[];
    points: TimelinePoint[];
    range: TimeRange;
    metaText: string;
    statusText: string;
    hoverX: number | null;
    setRange: Dispatch<SetStateAction<TimeRange>>;
    setHoverX: Dispatch<SetStateAction<number | null>>;
    fitAll: (input_points?: TimelinePoint[]) => void;
    applyParsedData: (parsed: ParsedPayload, source_label: string) => void;
    clearDataWithStatus: (message: string) => void;
    toggleTrack: (track_id: string) => void;
    reorderTracks: (source_id: string | null, target_id: string) => void;
    setHoverStatus: (point: TimelinePoint | null) => void;
}

export function useTimelineState(): TimelineState {
    const [tracks, setTracks] = useState<Track[]>(defaultTracks);
    const [points, setPoints] = useState<TimelinePoint[]>([]);
    const [range, setRange] = useState<TimeRange>({ start: 0, end: 1 });
    const [metaText, setMetaText] = useState("No file loaded.");
    const [statusText, setStatusText] = useState("Select a powermonitor JSON file to render timeline.");
    const [hoverX, setHoverX] = useState<number | null>(null);

    const visibleTracks = useMemo(() => tracks.filter((track) => track.visible), [tracks]);

    function fitAll(input_points: TimelinePoint[] = points): void {
        if (!input_points.length) {
            return;
        }
        const start = input_points[0].timeUs;
        let end = input_points[input_points.length - 1].timeUs;
        if (end <= start) {
            end = start + 1;
        }
        setRange({ start, end });
    }

    function applyParsedData(parsed: ParsedPayload, source_label: string): void {
        const meta = parsed.meta as {
            schema_version?: string;
            protocol_version?: string;
            config?: { stream_period_us?: number };
        };
        setPoints(parsed.points);
        setMetaText(
            `schema=${meta.schema_version || "n/a"} protocol=${meta.protocol_version || "n/a"} samples=${parsed.points.length} stream_period_us=${meta.config?.stream_period_us ?? "n/a"}`
        );
        setStatusText(`Loaded ${source_label}. Mouse wheel to zoom, drag to pan, hover to inspect.`);
        setHoverX(null);
        fitAll(parsed.points);
    }

    function clearDataWithStatus(message: string): void {
        setPoints([]);
        setHoverX(null);
        setStatusText(message);
    }

    function toggleTrack(track_id: string): void {
        setTracks((prev) =>
            prev.map((track) => (track.id === track_id ? { ...track, visible: !track.visible } : track))
        );
    }

    function reorderTracks(source_id: string | null, target_id: string): void {
        if (!source_id || source_id === target_id) {
            return;
        }
        setTracks((prev) => {
            const source_index = prev.findIndex((track) => track.id === source_id);
            const target_index = prev.findIndex((track) => track.id === target_id);
            if (source_index < 0 || target_index < 0) {
                return prev;
            }
            const next = [...prev];
            const [item] = next.splice(source_index, 1);
            next.splice(target_index, 0, item);
            return next;
        });
    }

    function setHoverStatus(point: TimelinePoint | null): void {
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
        hoverX,
        setRange,
        setHoverX,
        fitAll,
        applyParsedData,
        clearDataWithStatus,
        toggleTrack,
        reorderTracks,
        setHoverStatus
    };
}
