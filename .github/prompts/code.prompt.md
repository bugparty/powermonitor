# Task

- Check out `Coding Guidelines` in `REPO-ROOT/AGENTS.md` and read all **(MUST READ)** documents before writing code.
- Check out `Finding Documents` in `REPO-ROOT/AGENTS.md` to find relevant documents on demand.
- If the change touches `protocol/` or `node/`, also read `REPO-ROOT/.github/Guidelines/ProtocolRules.md`.
- If the change touches `sim/` or `node/`, also read `REPO-ROOT/.github/Guidelines/SimulationGuide.md`.

## Goal and Constraints

- Follow the chat message to implement the task.
- You must ensure the source code compiles.
- You must ensure all tests pass.

## Step 1. Implement the Request

- Follow the chat message to make the code changes.
- Before writing to any file, read it first to respect any parallel edits.
- **TDD bias**: if the change affects existing behaviour, write or adjust a test case first, then implement, then refactor.

## Step 2. Build and Fix

- Run `pwsh workflow.ps1` from `REPO-ROOT`.
- If there are compile errors:
  - Carefully identify whether the issue is on the callee side or the caller side; check similar existing code before deciding.
  - Only fix warnings introduced by your change; do not touch pre-existing warnings.
  - Fix and re-run until the build is clean.

## Step 3. Test and Fix

- Run the full test suite via `pwsh workflow.ps1`.
- If a test fails:
  - Read the failing `ASSERT` output carefully — it usually points to the exact condition.
  - To iterate on a single failing test: `./build_linux/bin/pc_sim_test --gtest_filter=SuiteName.TestName`
  - If the cause is unclear, add a temporary `std::cerr` or `printf` to print variable state, then remove it after fixing.
  - Do not delete any existing test case.
  - Fix and re-run until all tests pass.

## Step 4. Check It Again

- Go back to Step 2 and follow all steps once more to confirm nothing was missed.
