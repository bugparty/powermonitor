import type { Point } from "../types";

/**
 * Finds the index of the first point where timeUs >= startTimeUs using binary search.
 * Returns points.length if no such point is found.
 */
export function findStartIndex(points: Point[], startTimeUs: number): number {
    let low = 0;
    let high = points.length - 1;
    let result = points.length;

    while (low <= high) {
        const mid = Math.floor((low + high) / 2);
        if (points[mid].timeUs >= startTimeUs) {
            result = mid;
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }
    return result;
}

/**
 * Finds the index of the last point where timeUs <= endTimeUs using binary search.
 * Returns -1 if no such point is found.
 */
export function findEndIndex(points: Point[], endTimeUs: number): number {
    let low = 0;
    let high = points.length - 1;
    let result = -1;

    while (low <= high) {
        const mid = Math.floor((low + high) / 2);
        if (points[mid].timeUs <= endTimeUs) {
            result = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return result;
}

/**
 * Finds the index of the point closest to targetTimeUs using binary search.
 * Returns -1 if points array is empty.
 */
export function findNearestIndex(points: Point[], targetTimeUs: number): number {
    if (points.length === 0) return -1;

    let low = 0;
    let high = points.length - 1;

    // Binary search for exact match or insertion point
    while (low <= high) {
        const mid = Math.floor((low + high) / 2);
        if (points[mid].timeUs === targetTimeUs) {
            return mid;
        } else if (points[mid].timeUs < targetTimeUs) {
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    // If exact match not found, low is the insertion point where element > target.
    // The nearest is either at low (if within bounds) or low - 1 (if within bounds).

    // Check boundary conditions
    if (low >= points.length) return points.length - 1;
    if (low <= 0) return 0;

    // Compare distances to adjacent points
    const diff1 = Math.abs(points[low - 1].timeUs - targetTimeUs);
    const diff2 = Math.abs(points[low].timeUs - targetTimeUs);

    return diff1 <= diff2 ? low - 1 : low;
}
