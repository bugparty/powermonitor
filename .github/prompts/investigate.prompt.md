# Investigate

- Check out `Finding Documents` in `REPO-ROOT/AGENTS.md` to locate relevant source files and docs.
- Check out `Coding Guidelines` in `REPO-ROOT/AGENTS.md` for build and test instructions.
- Task document: `REPO-ROOT/.github/TaskLogs/Task_Investigate.md`.
  - If you cannot find this file, create it.

## Goal

Trace a bug or unexpected behaviour, confirm it with a test, propose solutions one at a time, and verify each.

## Task_Investigate.md Structure

- `# PROBLEM`: An exact copy of the problem description.
- `# UPDATES`: Follow-up updates. Always present, even if empty.
- `# TEST`: How to confirm/repro the problem, and the success criteria.
- `# PROPOSALS`: List and details of proposed fixes.
  - `- No.X Title [CONFIRMED|DENIED]`
  - `## No.X Title`
    - `### CODE CHANGE`: the exact change made to implement this proposal.
    - `### CONFIRMED` or `### DENIED`: explanation of why it worked or did not.

## Step 1. Identify the Request

Find `# Repro` or `# Continue` in the LATEST chat message.
- `# Repro` — fresh bug: create/overwrite `Task_Investigate.md` from scratch, then go to Step 2.
- `# Continue` — resume: append any new content under `# UPDATES`, then go to Step 4.
- Neither — treat as `# Continue`.

## Step 2. Write a Test

- Construct a Google Test case (or identify an existing one) that reproduces the problem.
- Build with `pwsh workflow.ps1` to confirm the test compiles.
- Write the repro idea and success criteria under `# TEST`.

## Step 3. Confirm the Problem

- Run the test. To target a single test: `./build_linux/bin/pc_sim_test --gtest_filter=SuiteName.TestName`
- If the issue only touches `web_viewer/`, use `npm run build:web` instead of `pwsh workflow.ps1` to confirm the change.
- If the problem reproduces, mark `# TEST [CONFIRMED]` and proceed to Step 4.
- If it does not reproduce, adjust the test and repeat.

## Step 4. Propose and Test Solutions

For each proposal:
1. Record `- No.X Title` in the list under `# PROPOSALS` and save the document.
2. Add `## No.X Title` with `### CODE CHANGE` and implement the fix in the source.
3. Run `pwsh workflow.ps1` and check all tests.
4. If all tests pass: mark `[CONFIRMED]`, write `### CONFIRMED` explanation, commit `Task_Investigate.md`. Done.
5. If tests fail: mark `[DENIED]`, write `### DENIED` explanation, **revert all source code changes** for this proposal, commit `Task_Investigate.md`, then return to step 1 of this loop for the next proposal.
6. If every proposal is `[DENIED]`, go back to Step 2 and revisit the root cause analysis.
