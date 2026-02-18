export function createXScale(range, plotLeft, plotWidth) {
    const span = Math.max(1, range.end - range.start);
    return (timeUs) => plotLeft + ((timeUs - range.start) / span) * plotWidth;
}

export function yScale(value, min, max, laneTop, laneHeight) {
    const span = Math.max(1e-9, max - min);
    return laneTop + laneHeight - ((value - min) / span) * laneHeight;
}

export function toLocalX(svgEl, clientX, viewBoxWidth) {
    const rect = svgEl.getBoundingClientRect();
    if (rect.width <= 0) {
        return 0;
    }
    return ((clientX - rect.left) / rect.width) * viewBoxWidth;
}

