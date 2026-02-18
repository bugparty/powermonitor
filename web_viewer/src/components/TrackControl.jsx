import React, { useRef } from "react";

export default function TrackControl({ tracks, onToggle, onReorder }) {
    const dragTrackId = useRef(null);

    function handleDragStart(trackId) {
        dragTrackId.current = trackId;
    }

    function handleDrop(targetId) {
        const sourceId = dragTrackId.current;
        dragTrackId.current = null;
        onReorder(sourceId, targetId);
    }

    return (
        <section className="track-control-row">
            <span className="track-control-title">Tracks (drag to reorder)</span>
            <ul className="track-list">
                {tracks.map((track) => (
                    <li
                        key={track.id}
                        className="track-item"
                        draggable
                        onDragStart={() => handleDragStart(track.id)}
                        onDragOver={(event) => event.preventDefault()}
                        onDrop={() => handleDrop(track.id)}
                    >
                        <label>
                            <input type="checkbox" checked={track.visible} onChange={() => onToggle(track.id)} />
                            <span className="track-dot" style={{ background: track.color }} />
                            {track.label}
                        </label>
                    </li>
                ))}
            </ul>
        </section>
    );
}

