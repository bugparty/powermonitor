import type { Point } from "../types";

/**
 * Returns the index of the first point with timeUs >= timeUs.
 * If all points are < timeUs, returns points.length.
 */
export function findStartIndex(points: Point[], timeUs: number): number {
    let left = 0;
    let right = points.length - 1;
    let result = points.length;

    while (left <= right) {
        const mid = (left + right) >>> 1; // Unsigned right shift for safe integer division
        if (points[mid].timeUs >= timeUs) {
            result = mid;
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    return result;
}

/**
 * Returns the index of the last point with timeUs <= timeUs.
 * If all points are > timeUs, returns -1.
 */
export function findEndIndex(points: Point[], timeUs: number): number {
    let left = 0;
    let right = points.length - 1;
    let result = -1;

    while (left <= right) {
        const mid = (left + right) >>> 1;
        if (points[mid].timeUs <= timeUs) {
            result = mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return result;
}

/**
 * Returns the index of the point closest to timeUs.
 * Returns -1 if points array is empty.
 */
export function findNearestIndex(points: Point[], timeUs: number): number {
    if (points.length === 0) return -1;

    let left = 0;
    let right = points.length - 1;

    // Binary search for insertion point
    while (left <= right) {
        const mid = (left + right) >>> 1;
        if (points[mid].timeUs === timeUs) return mid;

        if (points[mid].timeUs < timeUs) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    // left is the insertion point
    const idx1 = left - 1;
    const idx2 = left;

    if (idx1 < 0) return idx2;
    if (idx2 >= points.length) return idx1;

    const diff1 = Math.abs(points[idx1].timeUs - timeUs);
    const diff2 = Math.abs(points[idx2].timeUs - timeUs);

    return diff1 <= diff2 ? idx1 : idx2;
}
