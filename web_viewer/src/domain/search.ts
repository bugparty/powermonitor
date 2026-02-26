import type { Point } from "../types";

/**
 * Finds the index of the first point with timeUs >= targetTime using binary search.
 * Returns points.length if no such point exists (all points are smaller).
 */
export function findStartIndex(points: Point[], targetTime: number): number {
    let low = 0;
    let high = points.length - 1;
    let result = points.length;

    while (low <= high) {
        const mid = Math.floor((low + high) / 2);
        if (points[mid].timeUs >= targetTime) {
            result = mid;
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }
    return result;
}

/**
 * Finds the index of the last point with timeUs <= targetTime using binary search.
 * Returns -1 if no such point exists (all points are larger).
 */
export function findEndIndex(points: Point[], targetTime: number): number {
    let low = 0;
    let high = points.length - 1;
    let result = -1;

    while (low <= high) {
        const mid = Math.floor((low + high) / 2);
        if (points[mid].timeUs <= targetTime) {
            result = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return result;
}
