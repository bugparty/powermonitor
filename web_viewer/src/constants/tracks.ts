import type { Track } from "../types";

export const defaultTracks: Track[] = [
    { id: "voltage", key: "voltage", label: "Voltage (V)", color: "var(--voltage)", visible: true },
    { id: "current", key: "current", label: "Current (A)", color: "var(--current)", visible: true },
    { id: "power", key: "power", label: "Power (W)", color: "var(--power)", visible: true }
];
