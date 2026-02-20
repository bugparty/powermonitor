import { Point } from "../types";

/**
 * Binary search to find the index of the first point with timeUs >= target.
 * Assumes points are sorted by timeUs.
 */
export function findStartIndex(points: Point[], target: number): number {
    let left = 0;
    let right = points.length - 1;
    let result = points.length;

    while (left <= right) {
        const mid = (left + right) >>> 1;
        if (points[mid].timeUs >= target) {
            result = mid;
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    return result;
}

/**
 * Binary search to find the index of the first point with timeUs > target.
 * Assumes points are sorted by timeUs.
 */
export function findEndIndex(points: Point[], target: number): number {
    let left = 0;
    let right = points.length - 1;
    let result = points.length;

    while (left <= right) {
        const mid = (left + right) >>> 1;
        if (points[mid].timeUs > target) {
            result = mid;
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }
    return result;
}

/**
 * Binary search to find the point closest to target.
 * Assumes points are sorted by timeUs.
 */
export function findNearestPoint(points: Point[], target: number): Point | null {
    if (points.length === 0) return null;

    const idx = findStartIndex(points, target);

    if (idx === 0) return points[0];
    if (idx === points.length) return points[points.length - 1];

    const prev = idx - 1;
    const curr = idx;

    if (Math.abs(points[curr].timeUs - target) < Math.abs(points[prev].timeUs - target)) {
        return points[curr];
    } else {
        return points[prev];
    }
}
