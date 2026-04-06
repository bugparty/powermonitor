# Protocol-Critical Rules

Critical protocol rules that must be respected in all code changes.
For the full protocol specification, see `REPO-ROOT/docs/protocol/uart_protocol.md`.

## Frame Layout

```
SOF (0xAA 0x55) + VER + TYPE + FLAGS + SEQ + LEN(LE) + MSGID + DATA + CRC16
```

- CRC16/CCITT-FALSE: poly `0x1021`, init `0xFFFF`, no reflection, xorout `0x0000`.
- LEN includes MSGID.

## Sequence Numbers

- CMD/RSP share PC-driven seq space.
- DATA/EVT use device seq space.
- Keep them independent.

## Error Recovery

- CRC failures: drop silently; rely on PC retransmit for CMD.
- Parser must resync on SOF; handle fragmentation/sticking.
- Max length: enforce sane cap (recommend 256-1024) before allocation/copy.

## DATA_SAMPLE Packing

- 20-bit LE-packed values.
- `timestamp_us` accumulates from STREAM_START.
- Flags byte minimal; `dietemp` i16 LE.
- Clamp 20-bit signed/unsigned values before packing; respect little-endian.
