# CLAUDE.md

- Read through `REPO-ROOT/AGENTS.md` before performing any work.
  - `AGENTS.md` contains coding guidelines, naming conventions, and process workflow.
  - For quick reference, essential commands are listed below.

## Project Overview

A power monitoring system for measuring power consumption of XR hardware during BOXR experiments.

**Hardware:**
- Raspberry Pi Pico (RP2040) microcontroller
- Texas Instruments INA228 current/voltage/power sensor (1 kHz sampling)
- USB CDC communication to PC

**Features:**
- Real-time power monitoring with custom UART protocol (CRC16 error detection)
- Jetson Nano integrated dual-source capture (INA228 + onboard telemetry)
- Web-based timeline viewer for data visualization
- Protocol simulator with fault injection for testing

## Quick Start

### Dependencies
- Pico SDK (set `PICO_SDK_PATH` environment variable for device firmware)
- CMake, C++ compiler
- Node.js (for web viewer)

### Build (Linux)

```bash
# Quick build (PC client + tests)
cmake -B build_linux -S .
cmake --build build_linux

# Or use PowerShell workflow script
pwsh workflow.ps1
```

### Run

```bash
# PC client (requires Pico device connected via USB)
./build_linux/bin/powermonitor

# Run tests
./build_linux/bin/pc_sim_test

# Web viewer for JSON data
cd web_viewer && npm install && npm run dev
# Opens http://localhost:5173/web_viewer/index.html
```

### Build Device Firmware

```bash
cd device
cmake -B build
cmake --build build
# Flash: copy build/powermonitor.uf2 to Pico in BOOTSEL mode
```

## Project Structure

```
powermonitor/
├── device/           # RP2040 firmware (INA228 driver, USB serial)
├── pc_client/        # PC serial client
├── pc_sim/           # Simulator + Google Test suite
├── protocol/         # UART protocol (CRC16, framing, parser)
├── node/             # Protocol node implementations
├── sim/              # Event loop simulator
├── web_viewer/       # Browser timeline viewer
└── docs/             # Documentation
```

## Testing

**PC Simulator Tests** (no hardware required):
```bash
./build_linux/bin/pc_sim_test
# Tests: protocol framing, state machines, fault injection
```

**Real Device Tests**:
- Test plan: `docs/pc_client/device_serial_tests.md`
- Requires Pico + INA228 hardware connected via USB

## Further Documentation

- `README.md` - Full project documentation
- `AGENTS.md` - Coding guidelines and process workflow (read before coding)
- `docs/protocol/uart_protocol.md` - UART protocol specification
- `docs/pc_sim/simulator_tests.md` - Test details
- `.github/Guidelines/` - Build guidelines, coding style, protocol rules

## Detailed Workflow

- Read through `REPO-ROOT/AGENTS.md` before performing any work.
  - `AGENTS.md` is the guideline you should follow.
  - Following `Finding Documents` in `AGENTS.md`, find relevant documents on demand when you need specific knowledge.
  - Following `Coding Guidelines` in `AGENTS.md`, read all **(MUST READ)** documents before touching the source code.
- Interpret the request (in the latest chat message, not including conversation history) following the steps:

## Step 1

Read the first word of the request, and read an additional instruction file when it is:
- "design": REPO-ROOT/.github/prompts/design.prompt.md
- "verify": REPO-ROOT/.github/prompts/verify.prompt.md
- "ask": REPO-ROOT/.github/prompts/ask.prompt.md
- "investigate": REPO-ROOT/.github/prompts/investigate.prompt.md
- "code": REPO-ROOT/.github/prompts/code.prompt.md
- "review": REPO-ROOT/.github/prompts/review.prompt.md

### Exceptions

- If the first word is not in the list:
  - Follow REPO-ROOT/.github/prompts/code.prompt.md
  - Skip Step 2

## Step 2

Only applies when the first word is "design" or "investigate".
Read the second word if it exists, convert it to a title `# THE-WORD`.

## Step 3

Keep the remaining as is.
Treat the processed request as "the LATEST chat message" in the additional instruction file.
Follow the additional instruction file and start working immediately, there will be no more input.

## Examples

When the request is `ask how does the parser resync on noise`, follow `ask.prompt.md` and "the LATEST chat message" becomes:
```
how does the parser resync on noise
```

When the request is `design problem add PING message`, follow `design.prompt.md` and "the LATEST chat message" becomes:
```
# Problem
add PING message
```

When the request is `fix the CRC bug in parser`, since the first word is not in the list, follow `code.prompt.md` and "the LATEST chat message" becomes:
```
fix the CRC bug in parser
```
