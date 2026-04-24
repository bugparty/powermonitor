// TopBar — product name + primary controls
// Matches: web_viewer_ref/src/components/TopBar.tsx
function TopBar({ onLoad, onFit, downsample, onDownsample, alignStarts, onAlignStarts, fileName }) {
  return (
    <header className="pm-topbar">
      <div className="pm-brand">
        <svg width="22" height="22" viewBox="0 0 64 64" aria-hidden="true">
          <rect width="64" height="64" rx="10" fill="#181b21" stroke="#2f3643"/>
          <path d="M10,32 Q18,14 26,32 T42,32 T58,32" fill="none" stroke="#22c55e" strokeWidth="2.2" strokeLinecap="round"/>
          <path d="M10,40 Q20,30 30,40 T50,40 T60,38" fill="none" stroke="#f59e0b" strokeWidth="2" strokeLinecap="round" opacity="0.9"/>
        </svg>
        <h1>Power Monitor Timeline Viewer</h1>
      </div>
      <div className="pm-controls">
        <button type="button" className="pm-btn pm-btn-file" onClick={onLoad}>Load JSON</button>
        <button type="button" className="pm-btn" onClick={onFit}>Fit All</button>
        <label className="pm-inline">
          Downsample
          <select value={downsample} onChange={(e) => onDownsample(e.target.value)} className="pm-select">
            <option value="none">None</option>
            <option value="min-max">Min-Max</option>
            <option value="lttb">LTTB</option>
          </select>
        </label>
        <label className="pm-inline">
          <input type="checkbox" checked={alignStarts} onChange={(e) => onAlignStarts(e.target.checked)} />
          Align starts
        </label>
        {fileName && <span className="pm-filename" title={fileName}>{fileName}</span>}
      </div>
    </header>
  );
}

Object.assign(window, { PMTopBar: TopBar });
