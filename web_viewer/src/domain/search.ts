import type { Point } from "../types";

/**
 * Finds the index of the first point with timeUs >= targetTime.
 * Returns points.length if all points are earlier than targetTime.
 * This is equivalent to std::lower_bound.
 */
export function findStartIndex(points: Point[], targetTime: number): number {
    let low = 0;
    let high = points.length - 1;
    let result = points.length;

    while (low <= high) {
        const mid = (low + high) >>> 1;
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
 * Finds the index of the last point with timeUs <= targetTime.
 * Returns -1 if all points are later than targetTime.
 */
export function findEndIndex(points: Point[], targetTime: number): number {
    let low = 0;
    let high = points.length - 1;
    let result = -1;

    while (low <= high) {
        const mid = (low + high) >>> 1;
        if (points[mid].timeUs <= targetTime) {
            result = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return result;
}

/**
 * Finds the point closest to targetTime.
 * Returns null if points array is empty.
 */
export function findNearestPoint(points: Point[], targetTime: number): Point | null {
    if (points.length === 0) {
        return null;
    }

    // Binary search to find insertion point
    const idx = findStartIndex(points, targetTime);

    // Candidates are points[idx] and points[idx-1]
    const pAfter = idx < points.length ? points[idx] : null;
    const pBefore = idx > 0 ? points[idx - 1] : null;

    if (pAfter && pBefore) {
        return (pAfter.timeUs - targetTime) < (targetTime - pBefore.timeUs) ? pAfter : pBefore;
    }
    return pAfter || pBefore;
}
