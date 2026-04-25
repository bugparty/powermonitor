# Design

- Check out `Finding Documents` in `REPO-ROOT/AGENTS.md` to locate relevant source files and docs.
- Check out `Coding Guidelines` in `REPO-ROOT/AGENTS.md` to understand project constraints.
- Task document: `REPO-ROOT/.github/TaskLogs/Task_Design.md`.
  - If you cannot find this file, create it.

## Goal

Write a design document in `Task_Design.md` that addresses the problem.
Do NOT modify any source code. This phase is design only.

## Task_Design.md Structure

- `# PROBLEM`: An exact copy of the problem description from the chat message.
- `# UPDATES`: For any follow-up updates. Always present, even if empty.
- `# DESIGN`: Your analysis, proposals, and reasoning.
  - Describe what needs to change at a high level — no actual code.
  - Explain why each change is necessary.
  - Support each claim with evidence from the source code (function names, file locations).
  - If multiple approaches exist, list pros/cons and pick the best one.
- `# AFFECTED COMPONENTS`: Which directories are affected and why.
  - e.g. `- protocol/` — new MSGID constant and frame builder change
- `# !!!FINISHED!!!` at the end when done.

## Step 1. Identify the Request

Find `# Problem` or `# Update` in the LATEST chat message.
- `# Problem` — fresh request: create/overwrite `Task_Design.md` from scratch.
- `# Update` — revision: append a new `## Update` under `# UPDATES` and revise `# DESIGN` accordingly.
- Neither — you were interrupted; read `Task_Design.md` and continue.

## Step 2. Analyse

- Read all relevant source files in the affected components.
- Read `REPO-ROOT/docs/protocol/uart_protocol.md` if the change touches the protocol.
- Read `REPO-ROOT/docs/pc_sim/simulator_tests.md` if the change affects tests.
- Do not assume anything you cannot prove from the source code.

## Step 3. Write the Design

- Write findings and proposals to `# DESIGN` and `# AFFECTED COMPONENTS` in `Task_Design.md`.
- Keep it high-level: describe *what* changes and *why*, not line-by-line code edits.
- End the file with `# !!!FINISHED!!!`.
