# embedded-menu

Header-only C++17 library for embedded command dispatch. Define commands once,
dispatch them from any transport — interactive console, JSON API, or framed
binary link. Built on [embedded-bridge](https://github.com/m-mcgowan/embedded-bridge)
for structured host-side communication with embedded devices.

```
Serial bytes → [Transport] → Registry → Handler(Cmd&) → response
                                ↑
Cloud notes  → [Transport] ────┘
```

## What you can do with it

- **Add a command interface to any firmware** — debug, configure, and
  query a device over serial without a custom protocol
- **Build a browser dashboard** — the WebSocket bridge and SPA give you a
  live terminal and plugin panels with zero frontend tooling
- **Accept commands from Notehub** — the same handlers work for cloud-sent
  JSON notes and local serial input
- **Reliably communicate over noisy UART** — HDLC framing with CRC-16
  handles corruption, byte stuffing, and flow control
- **Stream sensor data to a host** — binary framing (HDLC/SLIP/COBS)
  carries raw payloads alongside text commands on the same serial link
- **Add a CLI to your test firmware** — interactive console with help,
  history, and tab completion for hardware bring-up

## Features

- **Single command definition** — handlers receive a transport-agnostic `Cmd`;
  the same handler works from console, JSON, or future transports
- **Zero heap** — fixed-capacity arrays, no `std::vector` or `std::map` in the
  core. Optional `std::function` handlers via `EMENU_USE_STD_FUNCTION`
- **Header-only** — no `.cpp` files to compile, no build system integration
  required beyond adding `src/` to your include path
- **No mandatory dependencies** — Arduino `Print` adapter compiles in
  automatically when `Arduino.h` is detected
- **Pluggable framing** — HDLC (with CRC-16), SLIP, and COBS framers for
  reliable communication over real UART links
- **Browser UI** — single-file SPA with serial-to-WebSocket bridge and a
  plugin system for custom dashboards

## Quick start

### PlatformIO

Add to `platformio.ini`:

```ini
lib_deps =
    https://github.com/user/embedded-menu.git
```

### CMake

```cmake
add_subdirectory(embedded-menu)
target_link_libraries(my_app embedded_menu)
```

### Arduino sketch

```cpp
#include <embedded_menu.h>

using namespace emenu;

Registry<8>    registry;
PrintWriter    output(Serial);
JsonSerial<8>  js(registry, output);

void setup() {
    Serial.begin(115200);
    registry.add("ping", {[](Cmd& cmd) { cmd.reply("ok", true); }});
}

void loop() {
    while (Serial.available()) js.process_byte(Serial.read());
}
```

Send `{"cmd":"ping"}` and receive `{"cmd":"ping","ok":true}`.

## Core concepts

### Registry

`Registry<N>` is a fixed-capacity command table. `N` is the maximum number of
commands (default 32).

```cpp
Registry<16> registry;

// Function pointer handler
void ping(Cmd& cmd) { cmd.reply("ok", true); }
registry.add("ping", {ping, "Echo a ping"});

// Lambda handler (requires EMENU_USE_STD_FUNCTION)
registry.add("echo", {[](Cmd& cmd) {
    cmd.reply("msg", cmd.param_str("msg"));
}, "Echo back a message"});

// With group and aliases
registry.add("led", {led_handler, "Control LED", "hardware", {"l"}});
```

`add()` returns a `Result` enum: `OK`, `DUPLICATE`, `TABLE_FULL`, or
`INVALID_ARG`.

### Cmd

`Cmd` is the interface between transports and handlers. Handlers read parameters
and produce output through it:

```cpp
void handler(Cmd& cmd) {
    // Read parameters (type-converting, with defaults)
    int    count = cmd.param_int("count", 1);
    float  rate  = cmd.param_float("rate", 0.5f);
    const char* name = cmd.param_str("name", "default");
    bool   verbose   = cmd.param_bool("verbose", false);

    // Produce output
    cmd.reply("status", "ok");       // string
    cmd.reply("count", count);       // int
    cmd.reply("rate", rate);         // float
    cmd.reply("active", true);       // bool

    // Direct writer access (for console-style output)
    cmd.out().println("Done.");
}
```

The same handler works regardless of transport. Use `cmd.source()` to
detect origin if you need transport-specific formatting:

```cpp
if (cmd.source() == CmdSource::ARGV) {
    cmd.out().printf("LED %s\n", state ? "on" : "off");
} else {
    cmd.reply("state", state);
}
```

### Writer

`Writer` is the abstract output sink, replacing Arduino's `Print&`:

| Class | Purpose |
|-------|---------|
| `PrintWriter` | Wraps Arduino `Print&` (auto-detected) |
| `BufferWriter<N>` | Fixed-size buffer for tests and capture |
| `StdioWriter` | Writes to `FILE*` (default `stdout`) |
| `NullWriter` | Discards all output |

All writers support `write()`, `print()`, `println()`, `printf()`, and
`end_frame()` (no-op unless using a framing writer).

## Transports

### JsonSerial

JSON lines over a byte stream. Each request is a single JSON object;
each response is a single JSON object.

```
→ {"cmd":"add","a":2,"b":3}
← {"cmd":"add","sum":5}
```

Two input modes:

```cpp
// Byte-at-a-time (interrupt/poll loop)
while (Serial.available()) js.process_byte(Serial.read());

// Complete line (from framer callback, test, or other source)
js.process_line(R"({"cmd":"ping"})");
```

Errors produce JSON responses:

```
→ not valid json
← {"error":"parse error"}

→ {"cmd":"nope"}
← {"cmd":"nope","error":"not found"}
```

### BasicSerial

Interactive console with whitespace tokenization, backspace handling, and a
built-in `help` command.

```
> help
  ping                 Echo a ping
  echo                 Echo back a message

[hardware]
  led                  Control LED

> ping
pong
```

Configuration:

```cpp
BasicSerial<16> console(registry, output);
console.set_echo(true);           // echo typed characters
console.set_show_prompt(true);    // show "> " after each command
console.set_prompt("$ ");         // custom prompt
```

Parameters are passed as positional pairs: `led state true` sets
`param_str("state")` to `"true"`.

### Dual transport

Route input to either transport based on content:

```cpp
if (line[0] == '{') {
    json_transport.process_line(line);
} else {
    console.process_line(line);
}
```

See `examples/all_interfaces/` for a complete example.

## Framing

For communication over real UART (not just USB CDC), the library provides
three standard framing protocols. Framing is **orthogonal** to transport —
a framer sits between the raw byte stream and the transport:

```
Raw bytes → Framer → payload → Transport.process_line()
                                       ↓
Raw bytes ← FramingWriter ←── Transport writes response
```

Each protocol has two components:
- **Framer** (input) — stateful decoder, delivers complete payloads via callback
- **FramingWriter** (output) — `Writer` subclass, encodes on `end_frame()`

### HDLC (recommended)

RFC 1662 style. Built-in CRC-16 integrity checking. Best for most use cases.

```cpp
#include <embedded_menu/framing/hdlc.h>

PrintWriter          raw(Serial);
HdlcFramingWriter<>  framed_out(raw);
JsonSerial<8>        js(registry, framed_out);

void on_frame(void* ctx, const uint8_t* data, size_t len) {
    // Null-terminate and dispatch
    char buf[256];
    memcpy(buf, data, len);
    buf[len] = '\0';
    static_cast<JsonSerial<8>*>(ctx)->process_line(buf);
}
HdlcFramer<> framer(on_frame, &js);

void loop() {
    while (Serial.available()) framer.process_byte(Serial.read());
}
```

Properties:
- Delimiter: `0x7E` (flag)
- Byte stuffing: `0x7D` escape with XOR `0x20`
- Integrity: CRC-16/HDLC (automatic — corrupt frames silently dropped)
- Flow control: optional XON/XOFF via `framer.set_flow_control(true)`
- Overhead: 2 bytes CRC + delimiters + escaped bytes

### SLIP

RFC 1055. Minimal overhead, no built-in CRC.

```cpp
#include <embedded_menu/framing/slip.h>

PrintWriter          raw(Serial);
SlipFramingWriter<>  framed_out(raw);
JsonSerial<8>        js(registry, framed_out);

void on_frame(void* ctx, const uint8_t* data, size_t len) { /* ... */ }
SlipFramer<> framer(on_frame, &js);
```

Properties:
- Delimiter: `0xC0` (END)
- Byte stuffing: `0xDB` escape, `0xDC` = END, `0xDD` = ESC
- No CRC — add application-layer checksumming if needed
- Double-END framing (leading END flushes line noise)

### COBS

Consistent Overhead Byte Stuffing. Deterministic overhead, no special
characters in encoded output except the `0x00` delimiter.

```cpp
#include <embedded_menu/framing/cobs.h>

PrintWriter          raw(Serial);
CobsFramingWriter<>  framed_out(raw);
JsonSerial<8>        js(registry, framed_out);

void on_frame(void* ctx, const uint8_t* data, size_t len) { /* ... */ }
CobsFramer<> framer(on_frame, &js);
```

Properties:
- Delimiter: `0x00`
- Overhead: exactly 1 byte per 254 data bytes (worst case)
- No CRC — add application-layer checksumming if needed
- No byte stuffing (COBS encoding eliminates zeros)

### Choosing a protocol

| | HDLC | SLIP | COBS |
|--|------|------|------|
| CRC included | Yes (CRC-16) | No | No |
| Overhead (typical) | ~4 bytes + escapes | ~2 bytes + escapes | 1 byte per 254 |
| Worst-case expansion | 2x (all bytes need escaping) | 2x | ~0.4% |
| Complexity | Medium | Minimal | Low |
| Best for | General UART, noisy links | Simple point-to-point | Binary-heavy payloads |

Use HDLC unless you have a specific reason not to. It handles integrity
checking so your application doesn't have to.

## Web UI

A single-file SPA (`web/index.html`) and serial-to-WebSocket bridge
(`web/bridge.py`) let you interact with a device from a browser. The bridge
relays JSON lines between the serial port and WebSocket clients.

### Running

```bash
pip install pyserial websockets
python web/bridge.py --port /dev/cu.usbmodem1433101
```

This starts the WebSocket server (auto-assigned port), opens the SPA in
your browser, and begins relaying. Use `--ws-port 9000` for a fixed port
or `--no-browser` to skip auto-open.

The bridge handles device disconnection (deep sleep, USB reset) by
automatically reconnecting when the serial port reappears.

### SPA features

The SPA connects to the bridge via WebSocket and provides:

- **Terminal** — send commands (JSON or plain text), see responses with
  directional markers (`▸` sent, `◂` received)
- **Command history** — arrow keys navigate previous commands
- **Connection status** — header badge shows connected/disconnected/connecting
  with auto-reconnect on disconnect
- **Plugin sidebar** — custom panels that update in response to command data

### Plugins

Register plugins to create live-updating panels from command responses.
A built-in Device Info plugin is included as an example:

```javascript
MenuUI.registerPlugin({
    name: 'Device Info',
    subscribe: 'info',          // update when {"cmd":"info",...} is received
    render(el, data) {
        el.innerHTML = Object.entries(data)
            .filter(([k]) => k !== 'cmd')
            .map(([k, v]) =>
                `<div class="kv">
                   <span class="key">${k}</span>
                   <span class="val">${v}</span>
                 </div>`)
            .join('');
    }
});
```

Send `{"cmd":"info"}` and the panel populates with the response fields.

Plugin API:

| Property | Type | Description |
|----------|------|-------------|
| `name` | `string` | Panel title |
| `subscribe` | `string` or `string[]` | Command name(s) to listen for |
| `render` | `(el, data) => void` | Called with the panel body element and parsed JSON response |

Global API on `window.MenuUI`:

| Method | Description |
|--------|-------------|
| `registerPlugin(plugin)` | Add a sidebar panel |
| `send(text)` | Send a command string to the device |
| `appendLine(text, cls)` | Add a line to the terminal (`'sent'`, `'received'`, `'error'`, `'info'`) |

### Custom pages

Create a custom HTML page that loads the SPA and adds project-specific
plugins:

```html
<script>
// After MenuUI is available:
MenuUI.registerPlugin({
    name: 'Sensors',
    subscribe: ['temperature', 'humidity'],
    render(el, data) {
        el.innerHTML = `<div class="kv">
            <span class="key">${data.cmd}</span>
            <span class="val">${data.value}</span>
        </div>`;
    }
});
</script>
```

The WebSocket URL defaults to `ws://localhost:8765` but can be overridden
with the `?ws=` query parameter:
`file:///path/to/index.html?ws=ws://localhost:9000`

## Configuration

| Macro | Default | Effect |
|-------|---------|--------|
| `EMENU_USE_STD_FUNCTION` | not defined | Handler becomes `std::function<void(Cmd&)>` (enables captures, costs heap) |
| `EMENU_MAX_PARAMS` | `16` | Max named parameters per `Cmd` |
| `EMENU_MAX_ALIASES` | `4` | Max aliases per command |
| `EMENU_CRC16_USE_TABLE` | not defined | CRC-16 uses 512-byte lookup table instead of bit-by-bit computation |

## Building tests

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Project structure

```
src/
  embedded_menu.h                    — convenience header (core + transports)
  embedded_menu/
    writer.h                         — Writer base class and subclasses
    handler.h                        — Handler type, CommandEntry
    cmd.h                            — Cmd (parameter access + reply)
    registry.h                       — Registry<N> command table
    transport/
      json_serial.h                  — JSON lines transport
    console/
      basic_serial.h                 — Interactive console
    framing/
      hdlc.h                         — HDLC framer + writer (CRC-16)
      slip.h                         — SLIP framer + writer
      cobs.h                         — COBS framer + writer
    detail/
      json_parser.h                  — JSON parser + builder (internal)
      crc16.h                        — CRC-16/HDLC (internal)
web/
  index.html                         — single-file SPA (terminal + plugin sidebar)
  bridge.py                          — serial ↔ WebSocket bridge
examples/
  all_interfaces/                    — dual transport (JSON + console) on one serial port
```

Framing headers are **not** included by `embedded_menu.h` — include the
specific protocol you want. This keeps the default zero-overhead.

## License

BSD-3-Clause
