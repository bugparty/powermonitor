import React from "react";
import type { ChangeEvent } from "react";
import type { DownsampleMode } from "../types";

interface TopBarProps {
    onFileChange: (event: ChangeEvent<HTMLInputElement>) => void;
    onFitAll: () => void;
    downsampleMode: DownsampleMode;
    onDownsampleModeChange: (mode: DownsampleMode) => void;
}

export default function TopBar({ onFileChange, onFitAll, downsampleMode, onDownsampleModeChange }: TopBarProps) {
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
            </div>
        </header>
    );
}
