export function parsePayload(text) {
    const root = JSON.parse(text);
    const samples = Array.isArray(root.samples) ? root.samples : [];
    const points = samples
        .map((sample) => {
            const engineering = sample.engineering || {};
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
        .filter(Boolean)
        .sort((a, b) => a.timeUs - b.timeUs);

    if (!points.length) {
        throw new Error("No valid engineering samples found");
    }

    return { meta: root.meta || {}, points };
}

