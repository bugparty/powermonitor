# AGENTS.md

Practical handbook for AI agents working in this repo. Keep outputs (docs, comments, commits, test names) in English. No Cursor/Copilot rules exist here.

---

## Must-Read First
- README.md – layout, build, quick start.
- docs/protocol/uart_protocol.md – framing, CRC, MSGIDs, endianness, 20-bit packing.
- docs/pc_sim/simulator_tests.md and docs/pc_sim/state_machine_tests.md – what tests assert; keep docs and tests in sync.
- docs/pc_client/device_serial_tests.md – on-device serial test plan and ordering.
- docs/naming_convention.md – how docs map to code paths.
- device/README.md and docs/device/time_sync.md – device-side expectations.

---

## Build, Test, Run
- Quick full test: `pwsh workflow.ps1` (required after any code change except changes limited to `web_viewer/`; for `web_viewer/`-only changes use `npm run build:web`).
- Clean rebuild + test: `pwsh workflow.ps1 -Clean` or `pwsh workflow.ps1 -Rebuild`.
- Verbose output: `pwsh workflow.ps1 -Verbose`.
- Help: `pwsh workflow.ps1 -Help` for all options.
- Windows-only: `pwsh workflow.ps1 -GenerateSolution` to create VS solution, `-OpenVS` to open in Visual Studio.
- Device firmware: `pwsh workflow.ps1 -BuildDevice` (requires PICO_SDK_PATH environment variable).
- Run tests directly: `./build_linux/bin/pc_sim_test` (Linux/WSL) or `.\build\bin\Debug\pc_sim_test.exe` (Windows).
- Single test case: `--gtest_filter=SuiteName.TestName`.
- Do not skip tests before commit; 34/34 must pass.

---

## Working Agreement
- Keep repo clean: never reset user changes; do not amend unless explicitly asked.
- Prefer small, reviewable changes; keep CMake targets building.
- TDD bias: add/adjust a test before changing behavior, then implement, then refactor.
- No new files unless needed; keep ASCII. Comments only when clarifying non-obvious code.
- If tests fail, fix first; do not commit failing state.

---

## Code Style (C++17/20)
- Formatting: 4-space indent (see .editorconfig); LF; trim trailing whitespace; final newline required.
- Includes: standard headers first, then third-party, then local (relative) headers. Use angled for system/third-party, quotes for project headers. Avoid unused includes.
- Names: snake_case for variables/functions; PascalCase for types/classes; ALL_CAPS for macros/const globals only when truly constant; avoid abbreviations unless well-known (CRC, LSB, SEQ). Align with protocol names from docs.
- Types: prefer fixed-width ints (`uint8_t`, `int32_t`, `uint16_t`) for protocol payloads; use `size_t` for sizes; use `auto` when type is obvious from initializer but avoid for integral protocol fields where width matters.
- Const-correctness: mark inputs `const` and pass by reference (`const T&`) for non-trivial types; prefer `std::array` or `std::span` for fixed buffers.
- Error handling: return status enums/structs; avoid exceptions in hot paths; validate lengths and bounds before reads; CRC failure → drop silently per protocol (no logging storm). Use early returns for invalid frames.
- Ownership: use RAII; avoid raw `new`/`delete`; prefer `std::unique_ptr` for ownership, references for non-owning.
- Threading: design is single-process event loop; avoid blocking sleeps in critical paths; timers via event_loop.
- Logging: keep concise; avoid spamming during DATA stream; count failures (CRC, drops, timeouts) rather than printing every occurrence.
- Numeric rules: clamp 20-bit signed/unsigned values before packing; respect endianness (little); keep LEN upper bound check (discard if LEN==0 or >MAX_LEN).
- Tests: keep deterministic seeds unless test explicitly checks robustness; avoid real randomness without seeding.
- Comments: English only; explain intent, not the obvious; reference protocol sections when handling edge cases.

---

## Protocol-Critical Reminders
- Frame layout: SOF 0xAA 0x55 + VER + TYPE + FLAGS + SEQ + LEN(LE) + MSGID + DATA + CRC16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, xorout 0x0000). LEN includes MSGID.
- SEQ spaces: CMD/RSP share PC-driven seq; DATA/EVT use device seq. Keep them independent.
- CRC failures: drop silently; rely on PC retransmit for CMD.
- Noise handling: parser must resync on SOF; handle fragmentation/sticking.
- Max length: enforce sane cap (recommend 256–1024) before allocation/copy.
- DATA_SAMPLE packing: 20-bit LE-packed; timestamp_us accumulates from STREAM_START; flags byte minimal; dietemp i16 LE.

---

## Virtual Link & Simulation
- Use sim/virtual_link for chunking, delay, drop, flip; each direction has independent LinkConfig (min/max chunk, min/max delay us, drop_prob, flip_prob).
- Default suggested debug config: min_chunk=1 max_chunk=16, delay 0–2000us, drop_prob=0, flip_prob=0.
- Event loop tick: call link.pump(now) then feed parsers and timers.

---

## PC Node Expectations
- Maintain cmd_seq; store outstanding commands with deadlines and retry budget (≤3).
- On RSP: verify orig_msgid and SEQ, cancel timer.
- On CFG_REPORT: persist current_lsb_nA, adcrange, stream_period_us, stream_mask.
- On DATA_SAMPLE: track frame-loss via SEQ discontinuity; optionally convert to engineering units for logs/stats.

---

## Device Node Expectations
- SET_CFG: update config/shunt_cal/adc_config/tempco, reply RSP(OK), then send CFG_REPORT immediately.
- GET_CFG: reply RSP(OK) then CFG_REPORT.
- STREAM_START: set period/mask, streaming_on=true, reply RSP(OK), schedule next sample.
- STREAM_STOP: streaming_on=false, reply RSP(OK).
- Waveform suggestion: vbus 12.0V ±0.1V sine + noise; current 0.5A ±0.2A sine; temp ~35C random walk. Clamp to 20-bit limits before packing.

---

## Docs & Tests Sync
- If you change test logic, update docs/pc_sim/state_machine_tests.md or simulator_tests.md accordingly. If you change those docs, update tests. Never diverge.
- Follow docs/naming_convention.md whenever adding or renaming docs; keep doc paths mapping to code modules.
- All documentation, comments, and commit messages stay in English.

---

## Git & Review Hygiene
- Never revert user changes. No force pushes. No commit amend unless user asks.
- Stage only relevant files; avoid committing secrets (env, creds).
- Keep commits focused; ensure `./test.sh` passes before proposing a commit.

---

## File/Formatting Notes
- .editorconfig enforces UTF-8, LF, trim trailing whitespace; indent 4 spaces for C/C++/CMake; markdown keeps trailing spaces when intentional.
- No .clang-format present; follow existing style seen in code and rules above.

---

## Quick Triage Checklist
- Did you read README.md and protocol docs?
- Are LEN, CRC, endian, and SEQ rules respected?
- Did you run the tests (or single failing test) after changes?
- Are docs/tests consistent? English only?
- Are logs bounded and not flooding during streaming?

---

## Safe Single-Test Loop Example
```bash
pwsh workflow.ps1
./build_linux/bin/pc_sim_test --gtest_filter=ParserStateMachineTests.SyncAfterNoise
```
Use this pattern to iterate quickly on failing cases.

---

## No Cursor/Copilot Rules
- No .cursor/rules or .github/copilot-instructions.md in repo; nothing extra to honor beyond this guide.

---

Stay within these boundaries and keep the simulator deterministic and well-tested.
