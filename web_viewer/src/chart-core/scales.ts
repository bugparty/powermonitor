import type { TimeRange } from "../types";

export function createXScale(range: TimeRange, plot_left: number, plot_width: number): (time_us: number) => number {
    const span = Math.max(1, range.end - range.start);
    return (time_us) => plot_left + ((time_us - range.start) / span) * plot_width;
}

export function yScale(value: number, min: number, max: number, lane_top: number, lane_height: number): number {
    const span = Math.max(1e-9, max - min);
    return lane_top + lane_height - ((value - min) / span) * lane_height;
}

export function toLocalX(svg_el: SVGSVGElement, client_x: number, view_box_width: number): number {
    const rect = svg_el.getBoundingClientRect();
    if (rect.width <= 0) {
        return 0;
    }
    return ((client_x - rect.left) / rect.width) * view_box_width;
}
