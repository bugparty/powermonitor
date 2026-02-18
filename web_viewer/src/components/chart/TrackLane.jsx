import React from "react";
import { computeBounds } from "../../chart-core/bounds";
import { yScale } from "../../chart-core/scales";
import { formatValue } from "../../domain/formatters";

export default function TrackLane({
    track,
    laneTop,
    laneHeight,
    layout,
    chartRight,
    visiblePoints,
    xScale
}) {
    const bounds = computeBounds(visiblePoints.map((point) => point[track.key]));
    const pathD =
        visiblePoints.length > 1
            ? visiblePoints
                  .map((point, index) => {
                      const x = xScale(point.timeUs);
                      const y = yScale(point[track.key], bounds.min, bounds.max, laneTop, laneHeight);
                      return `${index === 0 ? "M" : "L"}${x},${y}`;
                  })
                  .join(" ")
            : "";

    return (
        <g>
            <rect
                x={layout.left}
                y={laneTop}
                width={chartRight - layout.left}
                height={laneHeight}
                fill="none"
                stroke="#2f3643"
            />
            <text x="10" y={laneTop + laneHeight / 2} className="track-label">
                {track.label}
            </text>
            {[0, 1, 2, 3, 4].map((tick) => {
                const y = laneTop + (laneHeight / 4) * tick;
                const value = bounds.max - ((bounds.max - bounds.min) * tick) / 4;
                return (
                    <g key={tick}>
                        <line x1={layout.left} y1={y} x2={chartRight} y2={y} className="grid-line" />
                        <text x={layout.left - 8} y={y + 4} textAnchor="end" className="axis-label">
                            {formatValue(value)}
                        </text>
                    </g>
                );
            })}
            {pathD && (
                <path
                    d={pathD}
                    fill="none"
                    stroke={track.color}
                    strokeWidth="1.6"
                    vectorEffect="non-scaling-stroke"
                />
            )}
        </g>
    );
}

