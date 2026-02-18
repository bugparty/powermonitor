import type { ChangeEvent } from "react";

interface TopBarProps {
    onFileChange: (event: ChangeEvent<HTMLInputElement>) => void;
    onFitAll: () => void;
}

export default function TopBar({ onFileChange, onFitAll }: TopBarProps) {
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
            </div>
        </header>
    );
}
