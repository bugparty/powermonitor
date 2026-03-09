import type { Track } from "../types";

export const defaultTracks: Track[] = [
    // Use concrete colors so Chart.js (canvas) can render them correctly
    { id: "voltage", key: "voltage", label: "Voltage (V)", color: "#22c55e", visible: true }, // var(--voltage)
    { id: "current", key: "current", label: "Current (A)", color: "#f59e0b", visible: true }, // var(--current)
    { id: "power", key: "power", label: "Power (W)", color: "#a855f7", visible: true } // var(--power)
];
