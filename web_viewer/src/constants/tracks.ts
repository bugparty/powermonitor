import type { Track } from "../types";

export const defaultTracks: Track[] = [
    // Power metrics
    { id: "voltage", key: "voltage", label: "Voltage (V)", color: "#22c55e", visible: true }, // var(--voltage)
    { id: "current", key: "current", label: "Current (A)", color: "#f59e0b", visible: true }, // var(--current)
    { id: "power", key: "power", label: "Power (W)", color: "#a855f7", visible: true }, // var(--power)
    { id: "temp", key: "temp", label: "Temperature (°C)", color: "#ef4444", visible: false },
    { id: "vshunt", key: "vshunt", label: "Shunt Voltage (V)", color: "#06b6d4", visible: false },
    { id: "charge", key: "charge", label: "Charge (C)", color: "#f97316", visible: false },
    { id: "energy", key: "energy", label: "Energy (J)", color: "#3b82f6", visible: false },
    // Latency metrics (in milliseconds)
    { id: "m2d", key: "m2d", label: "M2D Latency (ms)", color: "#3b82f6", visible: true },  // blue
    { id: "c2d", key: "c2d", label: "C2D Latency (ms)", color: "#ef4444", visible: true },  // red
    { id: "p2d", key: "p2d", label: "P2D Latency (ms)", color: "#06b6d4", visible: false }, // cyan
    { id: "r2d", key: "r2d", label: "R2D Latency (ms)", color: "#f97316", visible: false }, // orange
    // Frequency metrics (in MHz)
    { id: "cpu0_freq", key: "cpu0_freq", label: "CPU0 Freq (MHz)", color: "#8b5cf6", visible: true },  // violet
    { id: "cpu1_freq", key: "cpu1_freq", label: "CPU1 Freq (MHz)", color: "#ec4899", visible: true },  // pink
    { id: "gpu_freq", key: "gpu_freq", label: "GPU Freq (MHz)", color: "#14b8a6", visible: true },    // teal
    { id: "emc_freq", key: "emc_freq", label: "EMC Freq (MHz)", color: "#facc15", visible: false },  // yellow
];
