# Review

- Check out `Coding Guidelines` in `REPO-ROOT/AGENTS.md` for the rules to review against.
- This is review work only. Do NOT modify source code unless explicitly asked.

## Goal

Review the code changes described in the chat message and provide actionable feedback.

## Step 1. Read Context

- Read the changed files.
- Read `REPO-ROOT/.github/Guidelines/CodingStyle.md`.
- Read `REPO-ROOT/.github/Guidelines/ProtocolRules.md` if the change touches `protocol/` or `node/`.
- Read `REPO-ROOT/.github/Guidelines/SimulationGuide.md` if the change touches `sim/` or `node/`.

## Step 2. Review Against Rules

Check for:
- **Protocol compliance**: frame layout, CRC, endianness, SEQ spaces, LEN bounds
- **Coding style**: naming, indentation, fixed-width types, RAII, error handling
- **Test coverage**: does new or changed behaviour have a corresponding test case?
- **Tests**: deterministic seeds, no deleted test cases, docs/tests in sync
- **Working agreement**: no new files unless needed, no secrets, changes are focused

## Step 3. Report

Use this format:

```
## Code Review

### Issues
1. [error|warning|suggestion] File:line — description

### Looks Good
- ...

### Verdict
Approve / Request Changes
```
