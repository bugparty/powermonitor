# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **Start here:** read `AGENTS.md` — it covers build/test commands, code style, protocol rules, working agreements, and docs/tests sync requirements.

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