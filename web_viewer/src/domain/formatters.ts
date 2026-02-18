export function formatValue(value: number): string {
    if (!Number.isFinite(value)) {
        return "n/a";
    }
    return Math.abs(value) >= 100 ? value.toFixed(1) : value.toFixed(3);
}

export function formatTimeUs(time_us: number): string {
    return `${(time_us / 1e6).toFixed(3)} s`;
}
