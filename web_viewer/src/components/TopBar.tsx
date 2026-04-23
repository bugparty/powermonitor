import React from "react";
import type { ChangeEvent } from "react";
import type { DownsampleMode } from "../types";

export type MovingAvgWindow = 0 | 5 | 10 | 20;

interface TopBarProps {
    onFileChange: (event: ChangeEvent<HTMLInputElement>) => void;
    onFitAll: () => void;
    downsampleMode: DownsampleMode;
    onDownsampleModeChange: (mode: DownsampleMode) => void;
    alignSources: boolean;
    onAlignSourcesChange: (enabled: boolean) => void;
    movingAvgWindow: MovingAvgWindow;
    onMovingAvgWindowChange: (window: MovingAvgWindow) => void;
}

export default function TopBar({
    onFileChange,
    onFitAll,
    downsampleMode,
    onDownsampleModeChange,
    alignSources,
    onAlignSourcesChange,
    movingAvgWindow,
    onMovingAvgWindowChange
}: TopBarProps) {
    return (
        <header className="topbar">
            <h1>Power Monitor Timeline Viewer</h1>
            <div className="controls">
                <label className="file-button" htmlFor="json-file">
                    Load JSON
                </label>
                <input id="json-file" type="file" accept="application/json,.json" onChange={onFileChange} />
                <button type="button" onClick={onFitAll}>
                    Fit All
                </button>
                <label style={{ display: "inline-flex", alignItems: "center", gap: 4, fontSize: 13, color: "var(--muted)" }}>
                    Downsample
                    <select
                        value={downsampleMode}
                        onChange={(e) => onDownsampleModeChange(e.target.value as DownsampleMode)}
                        className="select"
                    >
                        <option value="none">None</option>
                        <option value="min-max">Min-Max</option>
                        <option value="lttb">LTTB</option>
                    </select>
                </label>
                <label style={{ display: "inline-flex", alignItems: "center", gap: 4, fontSize: 13, color: "var(--muted)" }}>
                    Moving Avg
                    <select
                        value={movingAvgWindow}
                        onChange={(e) => onMovingAvgWindowChange(Number(e.target.value) as MovingAvgWindow)}
                        className="select"
                    >
                        <option value={0}>Off</option>
                        <option value={5}>5ms</option>
                        <option value={10}>10ms</option>
                        <option value={20}>20ms</option>
                    </select>
                </label>
                <label style={{ display: "inline-flex", alignItems: "center", gap: 6, fontSize: 13, color: "var(--muted)" }}>
                    <input
                        type="checkbox"
                        checked={alignSources}
                        onChange={(e) => onAlignSourcesChange(e.target.checked)}
                    />
                    Align starts
                </label>
            </div>
        </header>
    );
}
