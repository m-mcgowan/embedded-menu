# USB Insight Hub — Web UI Example

A browser-based UI for the [USB Insight Hub](https://github.com/Aeriosolutions/USB-Insight-HUB-Software),
demonstrating how `embedded-menu`'s WebSocket bridge can front-end an existing
JSON serial protocol without modifying the device firmware.

## Architecture

```
┌─────────────────────────────┐     USB CDC (JSON lines)    ┌──────────────────┐
│  examples/insight_hub/      │                             │  Insight Hub     │
│  index.html  (custom)       │◄───── web/bridge.py ───────►│  firmware        │
│                             │       (generic)             │  (unmodified)    │
└─────────────────────────────┘                             └──────────────────┘
         ▲ WebSocket (ws://localhost:PORT)
```

### What `embedded-menu` provides (generic, reusable)

**`web/bridge.py`** — the serial↔WebSocket relay. Opens a serial port, starts
a local WebSocket server, and forwards JSON lines in both directions. Handles
device disconnection and reconnection automatically. Protocol-agnostic: it relays
whatever the device sends without interpreting it.

**`web/index.html`** — the base SPA shell. Provides the WebSocket connection,
terminal panel (scrolling log of sent/received lines), command input with history,
connection status indicator, and a plugin system for registering data panels.

### What is custom in `index.html` (this example)

Everything in this file is specific to the Insight Hub's protocol:

- **Protocol mapping**: translates the Insight Hub's `{"action":"get/set","params":[...]}` /
  `{"status":"ok","data":{...}}` format. The base SPA uses `{"cmd":...}` / `{"cmd":...,
  "ok":true}` for embedded-menu devices.

- **Channel panels**: three per-channel panels (CH1/CH2/CH3) showing live voltage
  and current readings, power/data enable toggles, and alert badges. These are
  rendered by `updatePanels()` which reads from `data.CH1`, `data.CH2`, `data.CH3`.

- **System info panel**: renders `pcConnected`, `vbus`, `uptime`, `freeHeap`,
  `cpu_ver`, and `meterInit` from the Insight Hub's state response.

- **Auto-fetch on connect**: sends `{"action":"get","params":["all","CH1_all","CH2_all","CH3_all"]}`
  immediately on WebSocket open to populate the panels.

- **Toggle controls**: `togglePower(ch)` / `toggleData(ch)` send
  `{"action":"set","params":{"CHx_pwr_en": true/false}}` and optimistically
  update the UI before the confirmation arrives.

- **Unit conversion**: voltage values from the API are in millivolts; divided
  by 1000 for display. Current values are in milliamps.

## Protocol

The Insight Hub speaks a line-based JSON protocol over USB CDC at 115200 baud:

```
→ {"action":"get","params":["all","CH1","CH2","CH3"]}
← {"status":"ok","data":{"vbus":"4929","freeHeap":91240,"CH1":{"voltage":"4982.0","current":"44.2","powerEn":true,...},...}}

→ {"action":"set","params":{"CH1_pwr_en": false}}
← {"status":"ok","data":{}}
```

## Usage

```bash
# From the embedded-menu repo root:
python3 web/bridge.py \
    --port /dev/cu.usbmodemXXXX \
    --spa examples/insight_hub/index.html
```

The bridge opens the SPA in your default browser automatically. The port can
be resolved by name if you have `usb-device` installed:

```bash
python3 web/bridge.py \
    --port $(usb-device port "USB Insight Hub A1") \
    --spa examples/insight_hub/index.html
```

## Features

- Per-channel voltage (V) and current (mA) live readings
- Power and data enable/disable toggles per channel
- Forward, back, and short-circuit alert indicators
- System info: vbus, uptime, free heap, firmware version
- Manual refresh and 2-second auto-poll
- Terminal panel for raw JSON commands
