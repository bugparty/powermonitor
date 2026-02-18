export type TrackKey = "voltage" | "current" | "power";

export interface Track {
    id: string;
    key: TrackKey;
    label: string;
    color: string;
    visible: boolean;
}

export interface TimelinePoint {
    seq: number;
    timeUs: number;
    voltage: number;
    current: number;
    power: number;
}

export interface TimeRange {
    start: number;
    end: number;
}

export interface LayoutConfig {
    width: number;
    height: number;
    left: number;
    right: number;
    top: number;
    bottom: number;
    laneGap: number;
}

export interface ParsedPayload {
    meta: Record<string, unknown>;
    points: TimelinePoint[];
}

export interface TimeTicks {
    majorTicks: number[];
    minorTicks: number[];
    majorStepUs: number;
    minorStepUs: number;
}
