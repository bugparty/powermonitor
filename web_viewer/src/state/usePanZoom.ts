import { useEffect, useRef } from "react";
import type { MouseEvent as ReactMouseEvent, WheelEvent } from "react";
import type { Layout, Point, Range } from "../types";

const MIN_SPAN_US = 200;

interface UsePanZoomArgs {
    points: Point[];
    range: Range;
    setRange: (range: Range) => void;
    layout: Layout;
}

interface DragState {
    startClientX: number;
    baseRange: Range;
}

export function usePanZoom({ points, range, setRange, layout }: UsePanZoomArgs) {
    const panRef = useRef<DragState | null>(null);
    const rangeRef = useRef<Range>(range);
    const pendingRangeRef = useRef<Range | null>(null);
    const animationFrameRef = useRef<number | null>(null);
    const chartRight = layout.width - layout.right;
    const plotWidth = chartRight - layout.left;

    useEffect(() => {
        rangeRef.current = range;
    }, [range]);

    useEffect(() => () => {
        if (animationFrameRef.current != null) {
            window.cancelAnimationFrame(animationFrameRef.current);
        }
    }, []);

    function scheduleRange(nextRange: Range): void {
        rangeRef.current = nextRange;
        pendingRangeRef.current = nextRange;

        if (animationFrameRef.current != null) {
            return;
        }

        animationFrameRef.current = window.requestAnimationFrame(() => {
            animationFrameRef.current = null;
            const pendingRange = pendingRangeRef.current;
            if (!pendingRange) {
                return;
            }
            pendingRangeRef.current = null;
            setRange(pendingRange);
        });
    }

    useEffect(() => {
        function onMouseMove(event: MouseEvent): void {
            const drag = panRef.current;
            if (!drag || !points.length) {
                return;
            }

            const deltaX = event.clientX - drag.startClientX;
            const span = drag.baseRange.end - drag.baseRange.start;
            const deltaTime = (deltaX / plotWidth) * span;
            const minTime = points[0].timeUs;
            const maxTime = points[points.length - 1].timeUs;

            let start = drag.baseRange.start - deltaTime;
            let end = drag.baseRange.end - deltaTime;

            if (start < minTime) {
                end += minTime - start;
                start = minTime;
            }
            if (end > maxTime) {
                start -= end - maxTime;
                end = maxTime;
            }
            if (end <= start) {
                end = start + 1;
            }

            scheduleRange({ start, end });
        }

        function onMouseUp(): void {
            panRef.current = null;
        }

        window.addEventListener("mousemove", onMouseMove);
        window.addEventListener("mouseup", onMouseUp);
        return () => {
            window.removeEventListener("mousemove", onMouseMove);
            window.removeEventListener("mouseup", onMouseUp);
        };
    }, [points, plotWidth, setRange]);

    function zoomAt(factor: number, localX: number): void {
        if (!points.length) {
            return;
        }

        const minTime = points[0].timeUs;
        const maxTime = points[points.length - 1].timeUs;
        const cursorRatio = (localX - layout.left) / plotWidth;
        const currentRange = rangeRef.current;
        const span = Math.max(1, currentRange.end - currentRange.start);
        const anchor = currentRange.start + span * cursorRatio;
        const newSpan = Math.max(MIN_SPAN_US, span * factor);

        let start = anchor - newSpan * cursorRatio;
        let end = start + newSpan;

        if (start < minTime) {
            start = minTime;
            end = start + newSpan;
        }
        if (end > maxTime) {
            end = maxTime;
            start = end - newSpan;
        }
        if (end <= start) {
            end = start + 1;
        }

        scheduleRange({ start, end });
    }

    function onWheel(event: WheelEvent<HTMLElement>): void {
        if (!points.length) {
            return;
        }
        event.preventDefault();
        const rect = (event.currentTarget as HTMLElement).getBoundingClientRect();
        if (rect.width <= 0) {
            return;
        }
        const localX = ((event.clientX - rect.left) / rect.width) * layout.width;
        if (localX < layout.left || localX > chartRight) {
            return;
        }
        const factor = event.deltaY < 0 ? 0.8 : 1.25;
        zoomAt(factor, localX);
    }

    function onMouseDown(event: ReactMouseEvent<HTMLElement>): void {
        if (!points.length) {
            return;
        }
        panRef.current = {
            startClientX: event.clientX,
            baseRange: { ...rangeRef.current }
        };
    }

    return { onWheel, onMouseDown };
}
