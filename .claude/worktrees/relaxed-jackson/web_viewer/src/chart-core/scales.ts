import type { Range } from "../types";

export function createXScale(range: Range, plotLeft: number, plotWidth: number): (timeUs: number) => number {
    const span = Math.max(1, range.end - range.start);
    return (timeUs: number) => plotLeft + ((timeUs - range.start) / span) * plotWidth;
}

export function yScale(value: number, min: number, max: number, laneTop: number, laneHeight: number): number {
    const span = Math.max(1e-9, max - min);
    return laneTop + laneHeight - ((value - min) / span) * laneHeight;
}

export function toLocalX(svgEl: SVGSVGElement, clientX: number, viewBoxWidth: number): number {
    const rect = svgEl.getBoundingClientRect();
    if (rect.width <= 0) {
        return 0;
    }
    return ((clientX - rect.left) / rect.width) * viewBoxWidth;
}
