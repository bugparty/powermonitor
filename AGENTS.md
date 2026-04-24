# General Instruction

- `REPO-ROOT` refers to the root directory of this repository (the `powermonitor/` folder).
- Before writing to a source file, read it again and make sure you respect the user's parallel editing.
- If any `*.prompt.md` file is referenced, take immediate action following the instructions in that file.
- All outputs (docs, comments, commits, test names) must be in English.
- DO NOT create or delete files unless explicitly directed.

## Finding Documents

Do not preload all documents. Read them on demand when you need specific knowledge.

- Project overview and building: `REPO-ROOT/README.md`
- Protocol specification (framing, CRC, MSGIDs, endianness, 20-bit packing): `REPO-ROOT/docs/protocol/uart_protocol.md`
- Test expectations and assertions: `REPO-ROOT/docs/pc_sim/simulator_tests.md`, `REPO-ROOT/docs/pc_sim/state_machine_tests.md`
- Device serial test plan: `REPO-ROOT/docs/pc_client/device_serial_tests.md`
- Doc-to-code path mapping: `REPO-ROOT/docs/naming_convention.md`
- Device-side firmware and timing: `REPO-ROOT/device/README.md`, `REPO-ROOT/docs/device/time_sync.md`

## Coding Guidelines

The C++ project in this repo uses its own build wrapper (`workflow.ps1`).
You must strictly follow the instructions in the following documents:

- Building and Testing: `REPO-ROOT/.github/Guidelines/Building.md` **(MUST READ)**
- Coding Style and Naming: `REPO-ROOT/.github/Guidelines/CodingStyle.md` **(MUST READ)**
- Protocol-Critical Rules: `REPO-ROOT/.github/Guidelines/ProtocolRules.md`
- Simulation and Node Behavior: `REPO-ROOT/.github/Guidelines/SimulationGuide.md`

## Accessing Task Documents

Task documents are only created when needed. They are stored in `REPO-ROOT/.github/TaskLogs/`:
- `Task_Design.md` - Created by the `design` prompt for significant feature work
- `Task_Investigate.md` - Created by the `investigate` prompt for bug tracing

## Accessing Prompt Files

Prompt files are in `REPO-ROOT/.github/prompts/`. See `CLAUDE.md` for the trigger-word routing table.
Available prompts: `design`, `verify`, `ask`, `investigate`, `code`, `review`.

## Working Agreement

- Keep repo clean: never reset user changes; do not amend unless explicitly asked.
- Prefer small, reviewable changes; keep CMake targets building.
- TDD bias: add/adjust a test before changing behavior, then implement, then refactor.
- If tests fail, fix first; do not commit failing state.
- No force pushes. Stage only relevant files; avoid committing secrets (env, creds).
- Do not skip tests before commit; all tests must pass.
- Keep test seeds deterministic; avoid real randomness without seeding.
