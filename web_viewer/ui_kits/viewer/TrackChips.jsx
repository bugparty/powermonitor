// TrackChips — draggable visibility toggles for series
// Matches: web_viewer_ref/src/components/TrackControl.tsx
function TrackChips({ tracks, onToggle, onReorder }) {
  const dragId = React.useRef(null);
  return (
    <section className="pm-track-row">
      <span className="pm-track-title">Tracks (drag to reorder)</span>
      <ul className="pm-track-list">
        {tracks.map((t) => (
          <li
            key={t.id}
            className="pm-chip"
            draggable
            onDragStart={() => { dragId.current = t.id; }}
            onDragOver={(e) => e.preventDefault()}
            onDrop={() => { onReorder(dragId.current, t.id); dragId.current = null; }}
          >
            <label>
              <input type="checkbox" checked={t.visible} onChange={() => onToggle(t.id)} />
              <span className="pm-dot" style={{ background: t.color }} />
              {t.label}
            </label>
          </li>
        ))}
      </ul>
    </section>
  );
}

Object.assign(window, { PMTrackChips: TrackChips });
