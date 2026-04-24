export type MetricKey = "voltage" | "current" | "power" | "temp" | "vshunt" | "charge" | "energy";

export type DownsampleMode = "none" | "min-max" | "lttb";

export type SourceId = "legacy" | "pico" | "onboard_cpp" | string;

export interface Point {
    sourceId: SourceId;
    sourceLabel: string;
    seq: number;
    timeUs: number;
    voltage: number | null;
    current: number | null;
    power: number;
    temp: number | null;
    vshunt: number | null;
    charge: number | null;
    energy: number | null;
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
    series: Series[];
}

export interface Series {
    id: SourceId;
    label: string;
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
