import type { ParsedMeta, ParsedPayload, Point } from "../types";

interface RawSample {
    engineering?: {
        vbus_v?: unknown;
        current_a?: unknown;
    };
    seq?: unknown;
    device_timestamp_us?: unknown;
    device_timestamp_unix_us?: unknown;  // 新增：绝对时间戳
    host_timestamp_us?: unknown;
}

interface RawPayload {
    samples?: RawSample[];
    meta?: ParsedMeta;
}

export function parsePayload(text: string): ParsedPayload {
    const root = JSON.parse(text) as RawPayload;
    const samples = Array.isArray(root.samples) ? root.samples : [];
    const points: Point[] = samples
        .map((sample): Point | null => {
            const engineering = sample.engineering || {};
            const voltage = Number(engineering.vbus_v);
            const current = Number(engineering.current_a);
            if (!Number.isFinite(voltage) || !Number.isFinite(current)) {
                return null;
            }

            // 优先使用绝对时间戳，其次使用相对时间戳，最后使用主机时间戳
            const timestamp = Number(
                sample.device_timestamp_unix_us ??
                sample.device_timestamp_us ??
                sample.host_timestamp_us ??
                0
            );
            return {
                seq: Number(sample.seq ?? 0),
                timeUs: Number.isFinite(timestamp) ? timestamp : 0,
                voltage,
                current,
                power: voltage * current
            };
        })
        .filter((point): point is Point => point !== null)
        .sort((a, b) => a.timeUs - b.timeUs);

    if (!points.length) {
        throw new Error("No valid engineering samples found");
    }

    return { meta: root.meta || {}, points };
}
