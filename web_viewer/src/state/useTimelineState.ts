import { useMemo, useState } from "react";
import { defaultTracks } from "../constants/tracks";
import type { ParsedPayload, Point, Range, Series, Track, TimeSpanSource } from "../types";

interface TimelineState {
    tracks: Track[];
    visibleTracks: Track[];
    series: Series[];
    points: Point[];
    timespans: TimeSpanSource[];
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
    const [series, setSeries] = useState<Series[]>([]);
    const [points, setPoints] = useState<Point[]>([]);
    const [timespans, setTimespans] = useState<TimeSpanSource[]>([]);
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
        const allPoints = parsed.series.flatMap((item) => item.points).sort((a, b) => a.timeUs - b.timeUs);
        setSeries(parsed.series);
        setPoints(allPoints);
        setTimespans(parsed.timespans || []);

        // Build meta text
        const parts = [
            `schema=${parsed.meta.schema_version || "n/a"}`,
            `protocol=${parsed.meta.protocol_version || "n/a"}`,
            `sources=${parsed.series.length}`,
            `samples=${allPoints.length}`,
        ];
        if (parsed.timespans?.length) {
            const totalSpans = parsed.timespans.reduce((sum, ts) => sum + ts.spans.length, 0);
            parts.push(`timespans=${totalSpans}`);
        }
        setMetaText(parts.join(" · "));
        setStatusText(`Loaded ${sourceLabel}. Mouse wheel to zoom, drag to pan, hover to inspect.`);
        fitAll(allPoints);
    }

    function clearDataWithStatus(message: string): void {
        setSeries([]);
        setPoints([]);
        setTimespans([]);
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
        const voltageText = point.voltage == null ? "n/a" : `${point.voltage.toFixed(4)}V`;
        const currentText = point.current == null ? "n/a" : `${point.current.toFixed(4)}A`;
        const parts = [
            `${point.sourceLabel}`,
            `t=${(point.timeUs / 1e6).toFixed(6)}s`,
            `seq=${point.seq}`,
            `V=${voltageText}`,
            `I=${currentText}`,
            `P=${point.power.toFixed(4)}W`
        ];
        // Add latency info if available
        if (point.m2d != null) {
            parts.push(`M2D=${point.m2d.toFixed(3)}ms`);
        }
        if (point.c2d != null) {
            parts.push(`C2D=${point.c2d.toFixed(3)}ms`);
        }
        if (point.p2d != null) {
            parts.push(`P2D=${point.p2d.toFixed(3)}ms`);
        }
        if (point.r2d != null) {
            parts.push(`R2D=${point.r2d.toFixed(3)}ms`);
        }
        setStatusText(parts.join(" "));
    }

    return {
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
    };
}
