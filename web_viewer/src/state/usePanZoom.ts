import { useEffect, useRef } from "react";
import type { MouseEvent as ReactMouseEvent, RefObject, WheelEvent } from "react";
import { toLocalX } from "../chart-core/scales";
import type { LayoutConfig, TimeRange, TimelinePoint } from "../types";

const MIN_SPAN_US = 200;

interface PanState {
    startClientX: number;
    baseRange: TimeRange;
}

interface UsePanZoomProps {
    points: TimelinePoint[];
    range: TimeRange;
    setRange: (value: TimeRange) => void;
    layout: LayoutConfig;
    svgRef: RefObject<SVGSVGElement | null>;
}

export function usePanZoom({ points, range, setRange, layout, svgRef }: UsePanZoomProps) {
    const panRef = useRef<PanState | null>(null);
    const chartRight = layout.width - layout.right;
    const plotWidth = chartRight - layout.left;

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

            setRange({ start, end });
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
        const span = Math.max(1, range.end - range.start);
        const anchor = range.start + span * cursorRatio;
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

        setRange({ start, end });
    }

    function onWheel(event: WheelEvent<SVGSVGElement>): void {
        if (!points.length || !svgRef.current) {
            return;
        }
        event.preventDefault();
        const localX = toLocalX(svgRef.current, event.clientX, layout.width);
        if (localX < layout.left || localX > chartRight) {
            return;
        }
        const factor = event.deltaY < 0 ? 0.8 : 1.25;
        zoomAt(factor, localX);
    }

    function onMouseDown(event: ReactMouseEvent<SVGSVGElement>): void {
        if (!points.length) {
            return;
        }
        panRef.current = {
            startClientX: event.clientX,
            baseRange: { ...range }
        };
    }

    return { onWheel, onMouseDown };
}
