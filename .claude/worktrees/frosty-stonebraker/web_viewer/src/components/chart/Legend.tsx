import type { Layout, Track } from "../../types";

interface LegendProps {
    tracks: Track[];
    layout: Layout;
}

export default function Legend({ tracks, layout }: LegendProps) {
    return (
        <>
            {tracks.map((track, index) => {
                const x = layout.left + index * 210;
                return (
                    <g key={track.id}>
                        <line x1={x} y1="18" x2={x + 24} y2="18" stroke={track.color} strokeWidth="2" />
                        <text x={x + 30} y="22" className="legend">
                            {track.label}
                        </text>
                    </g>
                );
            })}
        </>
    );
}
