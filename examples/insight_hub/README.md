# USB Insight Hub — Web UI Example

A browser-based UI for the [USB Insight Hub](https://github.com/Aeriosolutions/USB-Insight-HUB-Software),
demonstrating how `embedded-menu`'s WebSocket bridge can front-end an existing
JSON serial protocol without modifying the device firmware.

## Protocol

The Insight Hub speaks a line-based JSON-RPC protocol over USB CDC:

```
→ {"action":"get","params":["all","CH1","CH2","CH3"]}
← {"status":"ok","data":{"vbus":"4929","CH1":{"voltage":"4982.0","current":"44.2",...},...}}
```

## Usage

```bash
python3 web/bridge.py \
    --port $(usb-device port "USB Insight Hub A1") \
    --spa examples/insight_hub/index.html
```

Or with an explicit port:

```bash
python3 web/bridge.py --port /dev/cu.usbmodemXXXX --spa examples/insight_hub/index.html
```

The bridge opens the SPA in your default browser automatically.

## Features

- Per-channel voltage (V) and current (mA) readings
- Power and data enable/disable per channel
- Forward, back, and short-circuit alert indicators
- System info: vbus, uptime, free heap, firmware version
- Manual refresh and 2-second auto-poll
- Terminal panel for raw JSON commands
