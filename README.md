# embedded-menu

Header-only C++17 library for embedded command dispatch. Define commands once,
dispatch them from any transport — interactive console, JSON API, or framed
binary link.

```
Serial bytes → [Transport] → Registry → Handler(Cmd&) → response
                                ↑
Cloud notes  → [Transport] ────┘
```

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
```

Framing headers are **not** included by `embedded_menu.h` — include the
specific protocol you want. This keeps the default zero-overhead.

## License

BSD-3-Clause
