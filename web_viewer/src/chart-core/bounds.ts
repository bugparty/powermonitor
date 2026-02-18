import type { Layout } from "../types";

export function computeBounds(values: number[]): { min: number; max: number } {
    let min = Number.POSITIVE_INFINITY;
    let max = Number.NEGATIVE_INFINITY;
    values.forEach((value) => {
        if (value < min) min = value;
        if (value > max) max = value;
    });

    if (!Number.isFinite(min) || !Number.isFinite(max)) {
        return { min: 0, max: 1 };
    }
    if (min === max) {
        const padding = Math.max(0.1, Math.abs(min) * 0.05);
        return { min: min - padding, max: max + padding };
    }
    const padding = (max - min) * 0.1;
    return { min: min - padding, max: max + padding };
}

export function laneHeight(laneCount: number, layout: Layout): number {
    return (layout.height - layout.top - layout.bottom - layout.laneGap * (laneCount - 1)) / laneCount;
}
