export function formatValue(value) {
    if (!Number.isFinite(value)) {
        return "n/a";
    }
    return Math.abs(value) >= 100 ? value.toFixed(1) : value.toFixed(3);
}

export function formatTimeUs(timeUs) {
    return `${(timeUs / 1e6).toFixed(3)} s`;
}

