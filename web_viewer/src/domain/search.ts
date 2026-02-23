import { Point } from "../types";

/**
 * Returns the index of the first point with timeUs >= targetTimeUs.
 * Returns points.length if all points are before targetTimeUs.
 */
export function findStartIndex(points: Point[], targetTimeUs: number): number {
    let left = 0;
    let right = points.length;

    while (left < right) {
        const mid = (left + right) >>> 1;
        if (points[mid].timeUs < targetTimeUs) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return left;
}

/**
 * Returns the index of the last point with timeUs <= targetTimeUs.
 * Returns -1 if all points are after targetTimeUs.
 */
export function findEndIndex(points: Point[], targetTimeUs: number): number {
    let left = 0;
    let right = points.length - 1;
    let result = -1;

    while (left <= right) {
        const mid = (left + right) >>> 1;
        if (points[mid].timeUs <= targetTimeUs) {
            result = mid;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    return result;
}

/**
 * Returns the point with timeUs closest to targetTimeUs.
 * Returns null if points array is empty.
 */
export function findNearestPoint(points: Point[], targetTimeUs: number): Point | null {
    if (points.length === 0) return null;

    let left = 0;
    let right = points.length - 1;

    // Binary search to narrow down
    while (left <= right) {
        const mid = (left + right) >>> 1;
        if (points[mid].timeUs === targetTimeUs) {
            return points[mid];
        } else if (points[mid].timeUs < targetTimeUs) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    // left is now the insertion index where points[left].timeUs > targetTimeUs (or left == length)
    // Check points at left and left-1 (if they exist)
    const p1 = points[left - 1];
    const p2 = points[left];

    if (!p1) return p2;
    if (!p2) return p1;

    return (targetTimeUs - p1.timeUs) <= (p2.timeUs - targetTimeUs) ? p1 : p2;
}
