import type { MouseEvent as ReactMouseEvent, RefObject } from "react";
import { toLocalX } from "../../chart-core/scales";
import type { LayoutConfig } from "../../types";

interface HoverLayerProps {
    svgRef: RefObject<SVGSVGElement | null>;
    layout: LayoutConfig;
    chartRight: number;
    hoverX: number | null;
    onHoverMove: (localX: number | null) => void;
    onHoverLeave: () => void;
}

export default function HoverLayer({
    svgRef,
    layout,
    chartRight,
    hoverX,
    onHoverMove,
    onHoverLeave
}: HoverLayerProps) {
    function handleMouseMove(event: ReactMouseEvent<SVGRectElement>): void {
        if (!svgRef.current) {
            return;
        }
        const localX = toLocalX(svgRef.current, event.clientX, layout.width);
        if (localX < layout.left || localX > chartRight) {
            onHoverMove(null);
            return;
        }
        onHoverMove(localX);
    }

    return (
        <>
            {hoverX !== null && (
                <line
                    x1={hoverX}
                    y1={layout.top}
                    x2={hoverX}
                    y2={layout.height - layout.bottom}
                    className="hover-line"
                />
            )}
            <rect
                x={layout.left}
                y={layout.top}
                width={chartRight - layout.left}
                height={layout.height - layout.top - layout.bottom}
                fill="transparent"
                cursor="crosshair"
                onMouseMove={handleMouseMove}
                onMouseLeave={onHoverLeave}
            />
        </>
    );
}
