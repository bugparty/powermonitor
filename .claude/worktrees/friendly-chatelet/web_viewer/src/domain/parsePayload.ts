import type { ParsedMeta, ParsedPayload, Point, Series, SourceId } from "../types";

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

            if (!Number.isFinite(power)) {
                return null;
            }

            const temp = Number(engineering.temp_c);
            const vshunt = Number(engineering.vshunt_v);
            const charge = Number(engineering.charge_c);
            const energy = Number(engineering.energy_j);

            return {
                sourceId,
                sourceLabel,
                seq: Number(sample.seq ?? 0),
                timeUs: normalizeTimestamp(sample),
                voltage: hasVoltage ? voltage : null,
                current: hasCurrent ? current : null,
                power,
                temp: Number.isFinite(temp) ? temp : null,
                vshunt: Number.isFinite(vshunt) ? vshunt : null,
                charge: Number.isFinite(charge) ? charge : null,
                energy: Number.isFinite(energy) ? energy : null
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
    return {
        meta: root.meta || {},
        series: [{ id: "legacy", label: "Powermonitor", points }]
    };
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

    if (!series.length) {
        throw new Error("No valid source samples found");
    }

    return {
        meta: {
            ...(root.meta || {}),
            schema_version: root.schema_version || root.meta?.schema_version
        },
        series
    };
}

export function parsePayload(text: string): ParsedPayload {
    const root = JSON.parse(text) as RawPayload;
    if (root.sources && typeof root.sources === "object") {
        return buildBundlePayload(root);
    }
    return buildLegacyPayload(root);
}
