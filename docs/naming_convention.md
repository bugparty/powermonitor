# Documentation Naming Convention

## Goals

- Make documentation searchable with predictable names and paths.
- Ensure documentation is discoverable by matching code locations.
- Avoid long or duplicated filenames by using directory grouping.

## Structure Rules

1. **Module folder mirrors code**
   - Each primary code module gets a matching folder under `docs/`.
   - Example: `pc_sim/` code maps to `docs/pc_sim/`.

2. **Document filename matches the component or topic**
   - Single canonical doc: `docs/<module>/<component>.md`.
   - Multi-doc topics: `docs/<module>/<topic>.md`.

3. **Directory-first naming**
   - Prefer subdirectories to repeated prefixes.
   - Avoid `module_component_component.md` patterns.

4. **English-only filenames**
   - File names must use ASCII and English words.

5. **Related code linkage**
   - Each document should include a "Related code" section near the top.
   - Use code paths as inline code with full relative paths.

## Examples

- Protocol specification: `docs/protocol/uart_protocol.md`
- Protocol parser design: `docs/protocol/parser_design.md`
- Simulator tests: `docs/pc_sim/simulator_tests.md`
- State machine tests: `docs/pc_sim/state_machine_tests.md`
- PC client overview: `docs/pc_client/overview.md`
- PC client implementation plan: `docs/pc_client/implementation_plan.md`
- Device time sync: `docs/device/time_sync.md`

## Mapping Guidelines

- `protocol/` code -> `docs/protocol/`
- `pc_sim/` code -> `docs/pc_sim/`
- `pc_client/` code -> `docs/pc_client/`
- `device/` code -> `docs/device/`
- Additional modules should follow the same pattern.
