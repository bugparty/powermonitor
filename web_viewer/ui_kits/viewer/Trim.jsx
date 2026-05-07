// MetaBar, StatusRow, EmptyState — small trim components
function MetaBar({ text }) {
  return <section className="pm-meta">{text}</section>;
}

function StatusRow({ text }) {
  return <section className="pm-status">{text}</section>;
}

function EmptyState({ onLoad }) {
  return (
    <div className="pm-empty">
      <div className="pm-empty-inner">
        <div className="pm-empty-title">No data loaded</div>
        <div className="pm-empty-sub">Load a JSON file to view timeline</div>
        <button type="button" className="pm-btn pm-btn-file" onClick={onLoad}>Load JSON</button>
        <div className="pm-empty-hint">Or try the demo dataset below</div>
      </div>
    </div>
  );
}

Object.assign(window, { PMMetaBar: MetaBar, PMStatusRow: StatusRow, PMEmptyState: EmptyState });
