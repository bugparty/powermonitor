import { formatTimeUs } from "../../domain/formatters";
import type { Layout, TimeTicks } from "../../types";

interface TimeAxisProps {
    layout: Layout;
    timeTicks: TimeTicks;
    xScale: (timeUs: number) => number;
}

export default function TimeAxis({ layout, timeTicks, xScale }: TimeAxisProps) {
    return (
        <>
            {timeTicks.minorTicks.map((timeUs) => {
                const x = xScale(timeUs);
                return (
                    <line
                        key={`minor-${timeUs}`}
                        x1={x}
                        y1={layout.top}
                        x2={x}
                        y2={layout.height - layout.bottom}
                        className="axis-line-minor"
                    />
                );
            })}

            {timeTicks.majorTicks.map((timeUs) => {
                const x = xScale(timeUs);
                return (
                    <g key={`major-${timeUs}`}>
                        <line x1={x} y1={layout.top} x2={x} y2={layout.height - layout.bottom} className="axis-line" />
                        <text x={x} y={layout.height - 12} textAnchor="middle" className="axis-label">
                            {formatTimeUs(timeUs)}
                        </text>
                    </g>
                );
            })}
        </>
    );
}
