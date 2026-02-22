import type { Point } from "../types";

/**
 * Finds the index of the first point with timeUs >= target.
 * Returns points.length if all points are earlier than target.
 * Uses binary search: O(log N).
 */
export function findStartIndex(points: Point[], target: number): number {
    let left = 0;
    let right = points.length;
    while (left < right) {
        const mid = Math.floor((left + right) / 2);
        if (points[mid].timeUs < target) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return left;
}

/**
 * Finds the index of the last point with timeUs <= target.
 * Returns -1 if all points are later than target.
 * Uses binary search: O(log N).
 */
export function findEndIndex(points: Point[], target: number): number {
    let left = -1;
    let right = points.length - 1;
    while (left < right) {
        const mid = Math.ceil((left + right) / 2);
        if (points[mid].timeUs > target) {
            right = mid - 1;
        } else {
            left = mid;
        }
    }
    return left;
}

/**
 * Finds the index of the point closest to the target timeUs.
 * Returns -1 if the array is empty.
 * Uses binary search: O(log N).
 */
export function findNearestIndex(points: Point[], target: number): number {
    if (points.length === 0) return -1;

    // idx is the first point where timeUs >= target
    const idx = findStartIndex(points, target);

    if (idx === 0) return 0;
    if (idx === points.length) return points.length - 1;

    const prev = points[idx - 1];
    const curr = points[idx];

    // If distances are equal, prefer the earlier point (prev)
    // Using strictly less (<) prefers prev on ties.
    // Using less or equal (<=) would prefer curr on ties.
    // Consistent with "findStartIndex" behavior, we check distances.
    return (Math.abs(curr.timeUs - target) < Math.abs(prev.timeUs - target)) ? idx : idx - 1;
}
