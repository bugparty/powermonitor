# Coding Style

## Language and Formatting

- C++17/20, 4-space indentation (see `.editorconfig`).
- LF line endings; trim trailing whitespace; final newline required.
- Includes: standard headers first, then third-party, then local (relative). Angled brackets for system/third-party, quotes for project headers. Avoid unused includes.

## Naming Convention

| Element | Convention | Examples |
|---------|-----------|----------|
| Variables / Functions | `snake_case` | `send_ping()`, `frame_count` |
| Classes / Types | `PascalCase` | `PCNode`, `EventLoop` |
| Constants | `kCamelCase` | `kSof0`, `kProtoVersion` |
| Macros / const globals | `ALL_CAPS` | only when truly constant |
| Private members | trailing `_` | `parser_`, `endpoint_` |

- Avoid abbreviations unless well-known (CRC, LSB, SEQ).
- Align with protocol names from `docs/protocol/uart_protocol.md`.

## Types and Memory

- Prefer fixed-width ints (`uint8_t`, `int32_t`, `uint16_t`) for protocol payloads.
- Use `size_t` for sizes.
- Use `auto` when type is obvious from initializer, but avoid for integral protocol fields where width matters.
- Const-correctness: mark inputs `const`, pass by reference (`const T&`) for non-trivial types.
- Prefer `std::array` or `std::span` for fixed buffers.
- Use RAII; avoid raw `new`/`delete`; prefer `std::unique_ptr` for ownership, references for non-owning.

## Error Handling

- Return status enums/structs; avoid exceptions in hot paths.
- Validate lengths and bounds before reads.
- CRC failure -> drop silently per protocol (no logging storm).
- Use early returns for invalid frames.

## Threading and Logging

- Design is single-process event loop; avoid blocking sleeps in critical paths; timers via `EventLoop`.
- Keep logging concise; count failures (CRC, drops, timeouts) rather than printing every occurrence.

## Comments and Documentation

- English only.
- Explain intent, not the obvious.
- Reference protocol sections when handling edge cases.
- No new files unless needed; keep ASCII.

## Docs and Tests Synchronization

- If you change test logic, update `docs/pc_sim/state_machine_tests.md` or `simulator_tests.md` accordingly.
- If you change those docs, update tests. Never diverge.
- Follow `docs/naming_convention.md` whenever adding or renaming docs.
