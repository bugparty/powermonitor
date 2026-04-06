# Simulation Guide

## Virtual Link

- Use `sim/virtual_link` for chunking, delay, drop, flip.
- Each direction has independent `LinkConfig` (min/max chunk, min/max delay us, drop_prob, flip_prob).
- Default debug config: `min_chunk=1 max_chunk=16`, delay `0-2000us`, `drop_prob=0`, `flip_prob=0`.
- Event loop tick: call `link.pump(now)` then feed parsers and timers.

## PCNode Expectations

- Maintain `cmd_seq`; store outstanding commands with deadlines and retry budget (<=3).
- On RSP: verify `orig_msgid` and SEQ, cancel timer.
- On CFG_REPORT: persist `current_lsb_nA`, `adcrange`, `stream_period_us`, `stream_mask`.
- On DATA_SAMPLE: track frame-loss via SEQ discontinuity; optionally convert to engineering units.

## DeviceNode Expectations

- SET_CFG: update config, reply RSP(OK), then send CFG_REPORT immediately.
- GET_CFG: reply RSP(OK) then CFG_REPORT.
- STREAM_START: set period/mask, `streaming_on=true`, reply RSP(OK), schedule next sample.
- STREAM_STOP: `streaming_on=false`, reply RSP(OK).
- Waveform: vbus 12.0V +/- 0.1V sine + noise; current 0.5A +/- 0.2A sine; temp ~35C random walk. Clamp to 20-bit limits.
