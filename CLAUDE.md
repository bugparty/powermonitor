# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Important Documents

**Read these before starting development:**
- `AGENTS.md` - AI agent guidelines, protocol specs, development workflow, testing requirements (MUST READ)
- `README.md` - Project overview, building, quick start
- `docs/protocol/uart_protocol.md` - Complete UART protocol specification

## Architecture Overview

Power Monitor is an INA228-based power monitoring system with:
- **Raspberry Pi Pico firmware** (`device/`) - I2C sensor driver, USB serial communication
- **PC client** (`pc_client/`) - Serial communication, YAML/JSON config, CLI interface
- **PC simulator** (`pc_sim/`) - Hardware-free testing with fault injection

### Core Modules

| Directory | Purpose |
|-----------|---------|
| `protocol/` | Shared protocol: CRC16, frame builder, 4-state FSM parser, 20-bit unpacking |
| `sim/` | EventLoop (discrete event simulator), VirtualLink (fault injection) |
| `node/` | PCNode (command/response handling), DeviceNode (device simulation), INA228Model |

## Code Style

- C++17, 4-space indentation (see `.editorconfig`)
- Classes: CamelCase (`PCNode`, `EventLoop`)
- Functions/methods: snake_case (`send_ping()`)
- Constants: kCamelCase (`kSof0`, `kProtoVersion`)
- Private members: trailing underscore (`parser_`, `endpoint_`)
