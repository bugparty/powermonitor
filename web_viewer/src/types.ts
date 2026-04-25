export type PowerMetricKey = "voltage" | "current" | "power" | "temp" | "vshunt" | "charge" | "energy";

export type LatencyMetricKey = "m2d" | "c2d" | "p2d" | "r2d";

export type FreqMetricKey = "cpu0_freq" | "cpu1_freq" | "gpu_freq" | "emc_freq";

export type MetricKey = PowerMetricKey | LatencyMetricKey | FreqMetricKey;

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
    // Latency metrics (in milliseconds, converted from nanoseconds)
    m2d: number | null;  // Motion-to-Display latency
    c2d: number | null;  // Camera-to-Display latency
    p2d: number | null;  // Prediction-to-Display latency
    r2d: number | null;  // Render-to-Display latency
    // Frequency metrics (in MHz, converted from Hz)
    cpu0_freq: number | null;  // CPU cluster 0 frequency
    cpu1_freq: number | null;  // CPU cluster 1 frequency
    gpu_freq: number | null;   // GPU frequency
    emc_freq: number | null;   // EMC (memory controller) frequency
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

// ============ Time Span / Gantt Types ============

export interface TimeSpan {
    id: string;           // Unique span ID (e.g., "gldemo_0", "T29")
    lane: number;         // Lane index (0-based)
    startUs: number;      // Start time in microseconds
    endUs: number;        // End time in microseconds
    color: string;        // Fill color (CSS color string)
    label?: string;       // Optional label to display inside the bar
    metadata?: Record<string, unknown>;  // Additional data (iteration_no, skips, etc.)
}

export interface TimeSpanSource {
    id: SourceId;
    label: string;        // Panel title (e.g., "scheduling · per-plugin task spans")
    spans: TimeSpan[];
    laneCount: number;    // Number of lanes
    laneLabels: string[]; // Labels for each lane (e.g., ["core 0", "core 1", ...])
}

export interface ParsedPayload {
    meta: ParsedMeta;
    series: Series[];
    timespans?: TimeSpanSource[];  // Optional time span data for Gantt panels
}
