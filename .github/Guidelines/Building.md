# Building and Testing

## Primary Tool

- Use `pwsh workflow.ps1` from `REPO-ROOT` for all builds.
- DO NOT call `cmake` directly unless `workflow.ps1` is unavailable.

## Build Commands

| Command | Purpose |
|---------|---------|
| `pwsh workflow.ps1` | Full build + test (required after any code change except `web_viewer/`-only) |
| `pwsh workflow.ps1 -Clean` | Clean rebuild + test |
| `pwsh workflow.ps1 -Rebuild` | Rebuild + test |
| `pwsh workflow.ps1 -Verbose` | Verbose output |
| `pwsh workflow.ps1 -Help` | Show all options |
| `pwsh workflow.ps1 -GenerateSolution` | Generate Visual Studio solution (Windows only) |
| `pwsh workflow.ps1 -OpenVS` | Open in Visual Studio (Windows only) |
| `pwsh workflow.ps1 -BuildDevice` | Build Pico firmware (requires `PICO_SDK_PATH`) |

## Running Tests Directly

```bash
# Linux/WSL
./build_linux/bin/pc_sim_test

# Windows
.\build\bin\Debug\pc_sim_test.exe

# Single test case
--gtest_filter=SuiteName.TestName
```

## Web Viewer

- For `web_viewer/`-only changes: `npm run build:web` (no need for full `workflow.ps1`).

## Quick Single-Test Iteration

```bash
pwsh workflow.ps1
./build_linux/bin/pc_sim_test --gtest_filter=ParserStateMachineTests.SyncAfterNoise
```
