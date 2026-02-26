export type MetricKey = "voltage" | "current" | "power";

export type DownsampleMode = "none" | "min-max" | "lttb";

export interface Point {
    seq: number;
    timeUs: number;
    voltage: number;
    current: number;
    power: number;
}

export interface ParsedMeta {
    schema_version?: string;
    protocol_version?: string;
    config?: {
        stream_period_us?: number;
    };
    [key: string]: unknown;
}

export interface ParsedPayload {
    meta: ParsedMeta;
    points: Point[];
}

export interface Range {
    start: number;
    end: number;
}

export interface Track {
    id: MetricKey;
    key: MetricKey;
    label: string;
    color: string;
    visible: boolean;
}

export interface Layout {
    width: number;
    height: number;
    left: number;
    right: number;
    top: number;
    bottom: number;
    laneGap: number;
}

export interface TimeTicks {
    majorTicks: number[];
    minorTicks: number[];
    majorStepUs: number;
    minorStepUs: number;
}
