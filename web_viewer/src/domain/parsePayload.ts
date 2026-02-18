import type { ParsedPayload, TimelinePoint } from "../types";

interface RawSample {
    seq?: unknown;
    device_timestamp_us?: unknown;
    host_timestamp_us?: unknown;
    engineering?: {
        vbus_v?: unknown;
        current_a?: unknown;
    };
}

interface RawPayload {
    meta?: Record<string, unknown>;
    samples?: RawSample[];
}

export function parsePayload(text: string): ParsedPayload {
    const root = JSON.parse(text) as RawPayload;
    const samples = Array.isArray(root.samples) ? root.samples : [];
    const points: TimelinePoint[] = samples
        .map((sample) => {
            const engineering = sample.engineering ?? {};
            const voltage = Number(engineering.vbus_v);
            const current = Number(engineering.current_a);
            if (!Number.isFinite(voltage) || !Number.isFinite(current)) {
                return null;
            }

            const timestamp = Number(sample.device_timestamp_us ?? sample.host_timestamp_us ?? 0);
            return {
                seq: Number(sample.seq ?? 0),
                timeUs: Number.isFinite(timestamp) ? timestamp : 0,
                voltage,
                current,
                power: voltage * current
            };
        })
        .filter((point): point is TimelinePoint => point !== null)
        .sort((a, b) => a.timeUs - b.timeUs);

    if (!points.length) {
        throw new Error("No valid engineering samples found");
    }

    return { meta: root.meta ?? {}, points };
}
