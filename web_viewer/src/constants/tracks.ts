import type { Track } from "../types";

export const defaultTracks: Track[] = [
    // Use concrete colors so Chart.js (canvas) can render them correctly
    { id: "voltage", key: "voltage", label: "Voltage (V)", color: "#22c55e", visible: true }, // var(--voltage)
    { id: "current", key: "current", label: "Current (A)", color: "#f59e0b", visible: true }, // var(--current)
    { id: "power", key: "power", label: "Power (W)", color: "#a855f7", visible: true }, // var(--power)
    { id: "temp", key: "temp", label: "Temperature (°C)", color: "#ef4444", visible: false },
    { id: "vshunt", key: "vshunt", label: "Shunt Voltage (V)", color: "#06b6d4", visible: false },
    { id: "charge", key: "charge", label: "Charge (C)", color: "#f97316", visible: false },
    { id: "energy", key: "energy", label: "Energy (J)", color: "#3b82f6", visible: false }
];
