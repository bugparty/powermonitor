import type { ParsedMeta, ParsedPayload, Point, Series, SourceId, TimeSpan, TimeSpanSource } from "../types";

interface RawSample {
    engineering?: {
        vbus_v?: unknown;
        current_a?: unknown;
        power_w?: unknown;
        temp_c?: unknown;
        vshunt_v?: unknown;
        charge_c?: unknown;
        energy_j?: unknown;
    };
    latency?: {
        m2d_ns?: unknown;   // Motion-to-Display (nanoseconds)
        c2d_ns?: unknown;   // Camera-to-Display (nanoseconds)
        p2d_ns?: unknown;   // Prediction-to-Display (nanoseconds)
        r2d_ns?: unknown;   // Render-to-Display (nanoseconds)
    };
    freqs?: {
        cpu_cluster0_hz?: unknown;  // CPU cluster 0 frequency (Hz)
        cpu_cluster1_hz?: unknown;  // CPU cluster 1 frequency (Hz)
        gpu_hz?: unknown;           // GPU frequency (Hz)
        emc_hz?: unknown;           // EMC frequency (Hz)
    };
    seq?: unknown;
    device_timestamp_us?: unknown;
    device_timestamp_unix_us?: unknown;
    host_timestamp_us?: unknown;
    mono_ns?: unknown;
    unix_ns?: unknown;
    power_w?: unknown;
}

interface RawPayload {
    samples?: RawSample[];
    meta?: ParsedMeta;
    schema_version?: string;
    sources?: Record<string, { samples?: RawSample[] }>;
    timespans?: Record<string, RawTimeSpanSource>;
}

interface RawTimeSpanSource {
    label?: string;
    lane_labels?: string[];
    spans?: RawTimeSpan[];
}

interface RawTimeSpan {
    id?: string;
    lane?: unknown;
    start_us?: unknown;
    end_us?: unknown;
    color?: string;
    label?: string;
    metadata?: Record<string, unknown>;
}

function normalizeTimestamp(sample: RawSample): number {
    const unixUs = Number(sample.device_timestamp_unix_us);
    if (Number.isFinite(unixUs)) {
        return unixUs;
    }
    const unixNs = Number(sample.unix_ns);
    if (Number.isFinite(unixNs)) {
        return unixNs / 1000.0;
    }
    const deviceUs = Number(sample.device_timestamp_us);
    if (Number.isFinite(deviceUs)) {
        return deviceUs;
    }
    const hostUs = Number(sample.host_timestamp_us);
    if (Number.isFinite(hostUs)) {
        return hostUs;
    }
    const monoNs = Number(sample.mono_ns);
    if (Number.isFinite(monoNs)) {
        return monoNs / 1000.0;
    }
    return 0;
}

function humanizeSourceLabel(sourceId: string): string {
    if (sourceId === "pico") {
        return "Pico Powermonitor";
    }
    if (sourceId === "onboard_cpp") {
        return "Nano Onboard";
    }
    return sourceId;
}

// Default colors for time spans (cycling through signal colors)
const DEFAULT_SPAN_COLORS = [
    "#22c55e", // green (--sig-voltage)
    "#f59e0b", // amber (--sig-current)
    "#a855f7", // violet (--sig-power)
    "#3b82f6", // blue (--sig-energy)
    "#06b6d4", // cyan (--sig-vshunt)
    "#f97316", // orange (--sig-charge)
    "#ef4444", // red (--sig-temp)
];

function parseTimeSpans(raw: Record<string, RawTimeSpanSource>): TimeSpanSource[] {
    return Object.entries(raw).map(([sourceId, source]) => {
        const laneLabels = source.lane_labels || [];
        const laneCount = laneLabels.length || 1;
        const spans: TimeSpan[] = (source.spans || []).map((rawSpan, index) => {
            const lane = Number(rawSpan.lane ?? 0);
            const startUs = Number(rawSpan.start_us ?? 0);
            const endUs = Number(rawSpan.end_us ?? startUs);
            const color = rawSpan.color || DEFAULT_SPAN_COLORS[lane % DEFAULT_SPAN_COLORS.length];
            return {
                id: rawSpan.id || `${sourceId}_${index}`,
                lane: Number.isFinite(lane) ? lane : 0,
                startUs: Number.isFinite(startUs) ? startUs : 0,
                endUs: Number.isFinite(endUs) ? endUs : startUs,
                color,
                label: rawSpan.label,
                metadata: rawSpan.metadata,
            };
        });
        return {
            id: sourceId,
            label: source.label || sourceId,
            spans,
            laneCount,
            laneLabels,
        };
    });
}

function parseSeries(samples: RawSample[], sourceId: SourceId, sourceLabel: string): Point[] {
    return samples
        .map((sample): Point | null => {
            const engineering = sample.engineering || {};
            const voltage = Number(engineering.vbus_v);
            const current = Number(engineering.current_a);
            const engineeringPower = Number(engineering.power_w);
            const directPower = Number(sample.power_w);
            const hasVoltage = Number.isFinite(voltage);
            const hasCurrent = Number.isFinite(current);
            const power = Number.isFinite(engineeringPower)
                ? engineeringPower
                : Number.isFinite(directPower)
                    ? directPower
                    : hasVoltage && hasCurrent
                        ? voltage * current
                        : NaN;

            // Check if this sample has either power or latency data
            const hasPowerData = Number.isFinite(power);
            const latency = sample.latency || {};
            const m2dNs = Number(latency.m2d_ns);
            const c2dNs = Number(latency.c2d_ns);
            const p2dNs = Number(latency.p2d_ns);
            const r2dNs = Number(latency.r2d_ns);
            const hasLatencyData = Number.isFinite(m2dNs) || Number.isFinite(c2dNs) ||
                                    Number.isFinite(p2dNs) || Number.isFinite(r2dNs);

            // Parse frequency data (convert Hz → MHz)
            const freqs = sample.freqs || {};
            const cpu0Hz = Number(freqs.cpu_cluster0_hz);
            const cpu1Hz = Number(freqs.cpu_cluster1_hz);
            const gpuHz = Number(freqs.gpu_hz);
            const emcHz = Number(freqs.emc_hz);
            const hasFreqData = Number.isFinite(cpu0Hz) || Number.isFinite(cpu1Hz) ||
                                Number.isFinite(gpuHz) || Number.isFinite(emcHz);

            if (!hasPowerData && !hasLatencyData && !hasFreqData) {
                return null;
            }

            const temp = Number(engineering.temp_c);
            const vshunt = Number(engineering.vshunt_v);
            const charge = Number(engineering.charge_c);
            const energy = Number(engineering.energy_j);

            // Convert latency from nanoseconds to milliseconds
            return {
                sourceId,
                sourceLabel,
                seq: Number(sample.seq ?? 0),
                timeUs: normalizeTimestamp(sample),
                voltage: hasVoltage ? voltage : null,
                current: hasCurrent ? current : null,
                power: hasPowerData ? power : null,
                temp: Number.isFinite(temp) ? temp : null,
                vshunt: Number.isFinite(vshunt) ? vshunt : null,
                charge: Number.isFinite(charge) ? charge : null,
                energy: Number.isFinite(energy) ? energy : null,
                // Latency metrics: convert ns → ms (divide by 1,000,000)
                m2d: Number.isFinite(m2dNs) ? m2dNs / 1_000_000 : null,
                c2d: Number.isFinite(c2dNs) ? c2dNs / 1_000_000 : null,
                p2d: Number.isFinite(p2dNs) ? p2dNs / 1_000_000 : null,
                r2d: Number.isFinite(r2dNs) ? r2dNs / 1_000_000 : null,
                // Frequency metrics: convert Hz → MHz (divide by 1,000,000)
                cpu0_freq: Number.isFinite(cpu0Hz) ? cpu0Hz / 1_000_000 : null,
                cpu1_freq: Number.isFinite(cpu1Hz) ? cpu1Hz / 1_000_000 : null,
                gpu_freq: Number.isFinite(gpuHz) ? gpuHz / 1_000_000 : null,
                emc_freq: Number.isFinite(emcHz) ? emcHz / 1_000_000 : null,
            };
        })
        .filter((point): point is Point => point !== null)
        .sort((a, b) => a.timeUs - b.timeUs);
}

function buildLegacyPayload(root: RawPayload): ParsedPayload {
    const samples = Array.isArray(root.samples) ? root.samples : [];
    const points = parseSeries(samples, "legacy", "Powermonitor");
    if (!points.length) {
        throw new Error("No valid engineering samples found");
    }
    const result: ParsedPayload = {
        meta: root.meta || {},
        series: [{ id: "legacy", label: "Powermonitor", points }]
    };
    if (root.timespans) {
        result.timespans = parseTimeSpans(root.timespans);
    }
    return result;
}

function buildBundlePayload(root: RawPayload): ParsedPayload {
    const rawSources = root.sources || {};
    const orderedSourceIds = Object.keys(rawSources).sort((a, b) => {
        const rank = (sourceId: string) => (sourceId === "pico" ? 0 : sourceId === "onboard_cpp" ? 1 : 2);
        return rank(a) - rank(b) || a.localeCompare(b);
    });
    const series: Series[] = orderedSourceIds
        .map((sourceId) => {
            const source = rawSources[sourceId];
            const samples = Array.isArray(source?.samples) ? source.samples : [];
            const points = parseSeries(samples, sourceId, humanizeSourceLabel(sourceId));
            if (!points.length) {
                return null;
            }
            return {
                id: sourceId,
                label: humanizeSourceLabel(sourceId),
                points
            };
        })
        .filter((item): item is Series => item !== null);

    const result: ParsedPayload = {
        meta: {
            ...(root.meta || {}),
            schema_version: root.schema_version || root.meta?.schema_version
        },
        series: series.length > 0 ? series : []
    };

    if (root.timespans) {
        result.timespans = parseTimeSpans(root.timespans);
    }

    return result;
}

export function parsePayload(text: string): ParsedPayload {
    const root = JSON.parse(text) as RawPayload;
    if (root.sources && typeof root.sources === "object") {
        return buildBundlePayload(root);
    }
    return buildLegacyPayload(root);
}
