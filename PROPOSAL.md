# embedded-menu — Proposal

## Problem

Embedded firmware frequently needs to expose the same operations through
multiple interfaces: an interactive serial console for bench debugging, a JSON
API for test automation, a binary protocol for high-throughput tooling, and
cloud command-and-control for deployed devices. These interfaces have different
wire formats but dispatch to the same underlying logic.

In practice, this leads to parallel dispatch chains — one per interface — that
must be kept in sync as commands are added. The USB Insight Hub (UIH) project
is a concrete example: adding a single configurable parameter requires edits in
three places (serial JSON if-chain, WebSocket state blob, on-board menu
get/setParamValue), each with different string keys, different validation code,
and no shared schema. This pattern repeats across projects of all sizes.

The building blocks for solving this — command registries, dispatch tables,
transport adapters — are well-established patterns in other domains. Web
frameworks (Express, Flask), RPC systems (gRPC, Cap'n Proto), and OS-level
shells (Zephyr) all cleanly separate command definition from transport. But
these patterns haven't been clearly expressed as a reusable library for the
embedded space, where the constraints are different: no heap, static allocation,
`Print&` output sinks, coexistence with Arduino and ESP-IDF frameworks, and
often a need to bridge local commands to cloud services like Blues Notecard.

## What exists today

### Console / CLI libraries

| Library | Strengths | Limitation |
|---------|-----------|------------|
| embedded-cli (funbiscuit) | Single-header C, 1KB RAM, history, autocompletion | Interactive terminal only, single transport, maintenance mode |
| SimpleCLI | Clean typed arguments, MIT | Heap-heavy (32KB+), serial-only, unmaintained |
| ArduinoMenu | Multi-I/O (encoders, LCD, serial, web) | Menu navigation system, not command dispatch. LGPL |
| esp_console | Built into ESP-IDF, linenoise, argtable3 | ESP-IDF only, interactive text only, can't inject from other sources cleanly |
| Zephyr Shell | 9 transport backends, static registration via linker sections | Zephyr-only. All backends carry interactive text — no structured dispatch |

**Key observation**: Zephyr Shell comes closest with 9 transport backends, but
they all multiplex the same *interactive text* experience. There is no library
that takes one command definition and presents it as a menu item AND a JSON
endpoint AND a binary protocol command AND a cloud action.

### Command registries and dispatch patterns

The patterns needed for command registries have been implemented in other
frameworks — web routers (Express, Flask), RPC systems (gRPC, Cap'n Proto),
OS shells (Zephyr), and message routers (ETL). But they haven't been clearly
expressed as a reusable library for the embedded space, where the constraints
are fundamentally different.

**Embedded-specific registries** (string-keyed, transport-agnostic):

| Library / Pattern | What it does | Limitation |
|-------------------|-------------|------------|
| Zephyr `SHELL_CMD_REGISTER` | Compile-time macro → linker section. Perfect registry-transport separation | Requires Zephyr kernel, linker script cooperation |
| ushell (ilya-sotnikov) | Static `{name, help, handler}` table, zero heap, C89 freestanding | Archived. Bundles line processing with registry. No metadata/params |
| embedded-cli (funbiscuit) | Command bindings with name/help/callback, ~1KB RAM, ISR-safe input | Maintenance mode. Console coupled to registry |
| CmdMessenger | Transport separation by design (v3.2). PC-side Python/.NET | Integer command IDs, not string names. Uses heap |

**Dispatch infrastructure** (type/ID-keyed, not string-keyed):

| Library / Pattern | What it does | Limitation |
|-------------------|-------------|------------|
| ETL `message_router` | CRTP, no heap, no vtables, transport-agnostic dispatch | Routes by C++ type or integer ID, not string name |
| COMMS (commschamp) | Compile-time dispatch strategy selection (polymorphic/binary-search/linear), MPL-2.0 | Binary protocol domain, integer IDs |
| gRPC `Service` | Named method handlers, completely decoupled from transport | Massive dependency (protobuf, HTTP/2, async) |

**Compile-time lookup building blocks**:

| Library | What it does | Limitation |
|---------|-------------|------------|
| Frozen (serge-sans-paille) | Constexpr `unordered_map` with O(1) perfect-hash lookup. Apache-2.0 | Immutable — no runtime registration |
| Mapbox Eternal | Constexpr `map` (binary search) and `hash_map`. ISC license. ~2ns lookup | Same — immutable, no metadata |
| constexpr `std::array` | Sorted pairs + `std::lower_bound`. No deps | Manual, no library support |

**The gap**: No existing embedded library combines string-keyed dispatch,
transport-agnostic handlers, structured parameter access (`argc/argv` AND
JSON AND binary), output routing (`Print&`), zero heap allocation, and command
metadata (help, groups, aliases, parameter schema) in a single composable
package. The individual pieces exist — Frozen for fast lookup, ETL for
fixed-capacity containers, Zephyr for the design pattern, embedded-cli for
console UX — but nobody has composed them for the Arduino/PlatformIO/CMake
ecosystem.

### Recommended building blocks

Rather than depending on external libraries for the core, we borrow patterns
and optionally integrate with these as building blocks:

| Building block | Source | How we use it |
|---|---|---|
| Static command table | ushell pattern | `{name, help, handler}` struct array — the proven foundation |
| Constexpr lookup | Frozen or Eternal | Optional: O(1) lookup for compile-time-known commands |
| Transport I/O abstraction | embedded-cli | `writeChar` callback model, generalized to `Print&` + `Cmd&` |
| Dispatch strategy | COMMS | Auto-select linear/binary-search based on command count |
| Decentralized registration | Zephyr shell | Declare at point of definition (C++ static init, not linker sections) |
| Console integration | embedded-cli, linenoise | Adapters, not dependencies — console libraries plug in optionally |

### Existing work in this codebase

The `cli_service_plan.md` in the firmware project already describes the right
architecture: single `CommandRegistry`, multiple frontends (Serial REPL,
Notecard inbound, test code, startup menu), `Print&` output capture.
embedded-menu is the extraction of that pattern into a standalone library.

## Design

### Architecture: two layers

```
┌──────────────────────────────────────────────────────────────┐
│  embedded-menu                                                │
│                                                              │
│  Console adapters              Machine transports            │
│  (human-interactive,           (structured, stack many)      │
│   pick one)                                                  │
│  ┌─────────────┐              ┌──────────────────┐          │
│  │ linenoise   │              │ JSON serial      │          │
│  │ embedded-cli│              │ Binary protocol  │          │
│  │ esp_console │              │ Notecard C&C     │          │
│  │ Zephyr shell│              │ WebSocket        │          │
│  │ basic serial│              │ (future: BLE,    │          │
│  └──────┬──────┘              │  HTTP, MQTT)     │          │
│         │                     └────────┬─────────┘          │
│         │                              │                     │
│         ▼                              ▼                     │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              Command Registry (core)                  │    │
│  │                                                       │    │
│  │  - String-keyed dispatch with aliases                 │    │
│  │  - Handler: (Cmd&) → result                          │    │
│  │  - Cmd wraps params (argv, JSON, binary) + output    │    │
│  │  - Metadata: name, help, group, param schema         │    │
│  │  - Introspection: enumerate, search, tab-complete     │    │
│  │  - Static or dynamic registration                     │    │
│  │  - Zero mandatory dependencies                        │    │
│  └─────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────┘
```

The **command registry** is the core — transport-agnostic, no platform deps.
It could be factored into its own library if the separation proves useful.

**Console adapters** are thin wrappers that integrate existing interactive
libraries (linenoise, embedded-cli, esp_console, Zephyr shell) with the
registry. embedded-menu doesn't reimplement line editing or VT100 escape
sequences — it lets the best tool for each platform handle the interactive
experience. Pick one per project.

**Machine transports** handle structured command dispatch: JSON serial for test
automation, binary for high-throughput tooling, Notecard for cloud C&C.
These are independent and stackable — use as many as needed.

### Core handler model

```cpp
struct Cmd {
    // Parameter access — transport-agnostic
    int param_int(const char* name, int default_val);
    float param_float(const char* name, float default_val);
    const char* param_str(const char* name, const char* default_val);
    bool param_bool(const char* name, bool default_val);

    // Raw access for advanced use
    int argc();               // available when called from CLI
    char** argv();            // available when called from CLI
    J* json();                // available when called from JSON/Notecard
    const uint8_t* binary();  // available when called from binary transport

    // Output — goes wherever the transport directs it
    Print& out();             // text output (terminal, StringPrint capture, etc.)
    void reply(const char* key, const char* value);  // structured key-value
    void reply(J* json);      // structured JSON response
};

using Handler = void(*)(Cmd& cmd);
// or: using Handler = std::function<void(Cmd&)>;  (configurable)

registry.add("power.ammeter", {
    .handler = set_ammeter_mode,
    .help = "Switch to ammeter measurement mode",
    .aliases = {"ammeter"},
    .group = "power",
});
```

The `Cmd` object is the key abstraction. It wraps wherever the parameters came
from (argv, JSON, binary) and wherever the response goes (terminal, JSON
response, Notecard outbound note). The handler doesn't know or care which
transport invoked it.

### Compile-time conditional transports

```cpp
// Auto-detected from include path — zero configuration
#if __has_include(<Notecard.h>)        // note-arduino
  #define EMBEDDED_MENU_HAS_NOTECARD 1
#elif __has_include(<note.h>)          // note-c
  #define EMBEDDED_MENU_HAS_NOTECARD 1
#endif

#if __has_include(<embedded_cli.h>)
  #define EMBEDDED_MENU_HAS_EMBEDDED_CLI 1
#endif

// etc. for linenoise, esp_console, Zephyr shell
```

`__has_include` works across GCC 5+, Clang 3+, MSVC 2017+ — covers PlatformIO,
Arduino IDE, and CMake builds.

### Notecard integration

Two built-in command patterns when Notecard headers are detected:

**1. Cloud C&C dispatch** — commands arrive as inbound Notes, responses go as
outbound Notes:

```
Inbound note (cmd.qi):  {"body": {"cmd": "power.ammeter"}}
→ Registry dispatches to set_ammeter_mode handler
Outbound note (cmd.qo): {"body": {"cmd": "power.ammeter", "ok": true}}
```

**2. Notecard passthrough** — a built-in `notecard` command that forwards the
request body directly to the Notecard and returns the response. Useful for
remote `hub.set`, `card.wifi`, diagnostics:

```
Inbound:  {"body": {"cmd": "notecard", "req": "card.version"}}
→ Strips "cmd", calls notecard.Transaction(remainder)
Outbound: {"body": {"cmd": "notecard", "ok": true, "rsp": {"version": "..."}}}
```

### Host-side mirror (embedded-bridge integration)

embedded-menu defines commands on the firmware side. The host-side counterpart
is embedded-bridge (Python), which:

- Speaks the JSON serial transport natively (already does via `TestSession`)
- Can validate commands against a schema exported by the firmware
- Can auto-generate Python command stubs from the registry's introspection
- Provides rich TUI for devices that only speak JSON (e.g., the switch board)

The JSON serial protocol is shared — embedded-menu's JSON transport speaks
exactly what embedded-bridge's `TestSession` already understands.

### UIH integration example

The USB Insight Hub currently has three parallel dispatch chains. With
embedded-menu, parameters are defined once:

```cpp
// Define once
registry.add("screen.brightness", {
    .handler = [](Cmd& cmd) {
        int val = cmd.param_int("value", -1);
        if (val < 0) {
            cmd.reply("brightness", globalConfig.brightness);
        } else {
            globalConfig.brightness = constrain(val, 10, 100);
            cmd.reply("ok", true);
        }
    },
    .help = "Get/set screen brightness (10-100)",
    .group = "screen",
    .param_schema = {{"value", "int", "10-100", "optional"}},
});

// Dispatched from all three interfaces automatically:
// Serial JSON: {"action":"set","params":{"screen.brightness":{"value":75}}}
// Web UI: queries registry schema, renders slider with min=10 max=100
// On-board menu: auto-generated RANGE item from param_schema
```

The on-board menu tree can be auto-generated from command metadata (group
hierarchy, param_schema for types/ranges), or manually structured with the
registry providing the handler lookup.

## Notecard as a command set

The Notecard's JSON API (`card.version`, `hub.set`, `card.temp`, etc.) is
itself a command registry — 74 endpoints with documented parameters. Today
you interact with it by typing raw JSON into a terminal or the blues.dev
in-browser REPL. embedded-menu can expose it through a friendlier interface.

### CLI syntax

```
> notecard card.version
device:    notecard
version:   notecard-7.2.2.16380
sku:       NOTE-NBGL-500

> notecard hub.set mode=periodic outbound=30
ok

> notecard card.temp
value:     23.75

> help notecard
Passthrough to Notecard. Tab-complete endpoint names.
  notecard card.version    Device info
  notecard card.temp       Read temperature
  notecard hub.set         Configure Notehub connection
  ...
```

Under the hood, `notecard hub.set mode=periodic outbound=30` becomes
`{"req":"hub.set","mode":"periodic","outbound":30}`. The console adapter
parses `key=value` pairs into the JSON request, coercing types (integers,
booleans, strings). Tab completion on endpoint names and parameter hints
come from note-cpp's API definitions when available (`__has_include`), or a
static table bundled with the Notecard transport adapter.

Some legacy Notecard commands are most useful in their raw form — `info`,
`trace`, `sync` and similar debugging/status commands that return dense or
streaming output. These work best as raw passthrough where the response is
printed verbatim rather than reformatted.

### Three levels of Notecard interaction

| Level | Interface | Example |
|---|---|---|
| Friendly CLI | Console | `notecard hub.set mode=periodic` |
| Structured | JSON serial / Notecard C&C | `{"cmd":"notecard","req":"hub.set","mode":"periodic"}` |
| Raw JSON | Console or structured | `notecard {"req":"hub.set","mode":"periodic"}` |

All three dispatch through the same registry handler.

## Web UI

embedded-menu's JSON transport and schema introspection enable web UIs that
connect directly to devices via the **Web Serial API** — no firmware changes,
no embedded web server, no flash overhead. The UI runs entirely in the
browser; the device just speaks JSON lines over USB-CDC, which it already
does through embedded-menu's JSON serial transport.

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Browser (SvelteKit SPA)                                     │
│                                                             │
│  Auto-generated from registry schema:                        │
│  - Command list with help text and groups                   │
│  - Parameter inputs: sliders (ranges), dropdowns (enums),   │
│    toggles (bools), text fields (strings)                   │
│  - Live state polling                                       │
│  - Response formatting (tree view, key-value, raw)          │
│                                                             │
│  + Custom views per application (optional):                  │
│  - Notecard: endpoint browser, request builder, REPL        │
│  - Provisioner: test progress, sensor results, pass/fail    │
│  - EEQ: live mode viz, GPS track, battery, phase timeline   │
├─────────────────────────────────────────────────────────────┤
│  Connection layer (same SPA, multiple backends):             │
│  - Web Serial API: browser ↔ USB-CDC directly               │
│  - WebSocket: browser ↔ host bridge ↔ serial                │
│  - Notehub REST: browser ↔ Notehub API ↔ Notecard C&C      │
│  Protocol: embedded-menu JSON lines                          │
└─────────────────────────────────────────────────────────────┘

  Device firmware: zero additional overhead.
  Just the JSON serial transport it already has.
```

### Connection modes

| Mode | Connection | Use case |
|---|---|---|
| **Web Serial** | Browser ↔ USB-CDC directly | Local dev, bench testing. Zero firmware cost |
| **Host bridge** | Browser ↔ WebSocket ↔ Python ↔ serial | Remote bench (SSH tunnel), CI dashboards |
| **Notehub API** | Browser ↔ Notehub REST ↔ Notecard C&C | Deployed devices, fleet management |
| **Device-hosted** | ESP32 serves SPA from LittleFS (UIH pattern) | Opt-in for WiFi devices needing standalone operation. Resource-heavy |

The primary path is **Web Serial** — the SPA is hosted on GitHub Pages (or
localhost), connects to the device over USB, and discovers the command schema
on connect. No firmware changes needed beyond the JSON serial transport
that embedded-menu already provides.

Device-hosted (the UIH/ESP32-SvelteKit pattern) remains an option for devices
with WiFi that need standalone browser access without a computer, but it's
resource-heavy (flash for SPA, RAM for WebSocket server, framework overhead)
and not the default path.

### Hosting

The SPA is a static site — no server-side logic. Hosting options:

- **GitHub Pages** — free, versioned, works for public and org repos
- **localhost** (`npm run dev`) — during UI development
- **blues.dev or equivalent** — could replace the current Notecard in-browser
  REPL with a richer Web Serial-based application

### Target applications

**Notecard developer UI** — replaces the blues.dev in-browser REPL with a
proper application: endpoint browser with inline documentation, request
builder with typed fields (not raw JSON), response explorer (expandable tree),
history/favorites, environment variable editor with schema awareness from
embedded-config-cpp. Connects via Web Serial to any Notecard dev kit. This
overlaps with the planned `notehub-ui` in ensemble — the device-facing
Notecard UI shares components (endpoint browser, request builder), while
notehub-ui adds cloud management (route editor, fleet config, IaC diff).

**Provisioner UI** — web dashboard for the provisioning/test bench. Shows
test catalog, run progress, sensor results with pass/fail, power profiling
charts. Replaces the current serial menu for interactive use while the JSON
API continues to serve automated test runners. Connects via Web Serial
or host bridge.

**EEQ application UI** — when the main firmware's menu system is reworked,
the web UI provides: live mode visualization, GPS track overlay, battery and
charging status, phase timeline, capture statistics, configuration editor.
The serial CLI remains for bench debugging; the web UI is for richer
interaction. Connects via Web Serial when on the bench, Notehub API when
deployed.

### Shared web UI framework

The auto-generated portion (command list, parameter controls, state display)
is a reusable SvelteKit component library. Custom views are application-
specific pages that compose the shared components with domain-specific
visualization. This means:

- New command → automatically appears in the web UI (from schema)
- New parameter with range → automatically gets a slider
- Custom views (GPS map, waveform chart) are additive, not required

The schema introspection endpoint (`{"cmd":"schema"}`) returns the full
command catalog with parameter types, ranges, groups, and help text — enough
for the web UI to render controls without any application-specific frontend
code.

## Relationship to other projects

### In the ensemble ecosystem

```
┌──────────────────────────────────────────────────────────────────┐
│  Layer 2.5 — Command infrastructure                              │
│                                                                  │
│  embedded-menu                 embedded-bridge                   │
│  Firmware-side command         Host-side Python counterpart.     │
│  registry + transport          Drives commands over serial,      │
│  adapters. Define once,        provides TUI, captures output.    │
│  dispatch from serial,         Consumes JSON transport.          │
│  JSON, binary, Notecard.       Mirror registry for validation.   │
├──────────────────────────────────────────────────────────────────┤
```

| Relationship | Description |
|---|---|
| **note-cpp / note-arduino** | Auto-detected. Enables Notecard transport, passthrough command, endpoint tab-completion |
| **embedded-bridge** | Host-side consumer of JSON serial transport. WebSocket bridge for web UIs |
| **embedded-config-cpp** | Config schema drives get/set commands. Env var editor in web UI |
| **note-app** | Channel abstraction could serve as Notecard transport backend |
| **notehub-ui** | Cloud-side web UI shares the Notecard endpoint browser and request builder |
| **UIH** | First external consumer — replaces triplicated dispatch, web UI follows same SvelteKit pattern |
| **fixture-board** | First greenfield consumer — Pico switch board firmware |

### Independent use

embedded-menu has zero mandatory dependencies. The core registry works on any
platform with a C++17 compiler. Notecard, Arduino, and platform-specific
features compile in only when their headers are detected.

## Build system support

| Build system | How it works |
|---|---|
| **PlatformIO** | `lib_deps = embedded-menu` in platformio.ini |
| **Arduino IDE** | Install via Library Manager or manual zip |
| **CMake** | `add_subdirectory(embedded-menu)` or `FetchContent` |

Library metadata in `library.json` (PlatformIO) and `library.properties`
(Arduino IDE). CMakeLists.txt for CMake/Zephyr/Pico SDK.

## Console library integration

embedded-menu doesn't replace console libraries — it makes your commands
available through them. These are supported as optional console adapters:

| Library | Detection | What embedded-menu provides |
|---|---|---|
| **linenoise** | `__has_include(<linenoise.h>)` | Completion callback, command iteration |
| **embedded-cli** | `__has_include(<embedded_cli.h>)` | Command registration bridge, non-blocking char-at-a-time |
| **esp_console** | `__has_include(<esp_console.h>)` | Registers commands into esp_console's table |
| **Zephyr shell** | `__has_include(<shell/shell.h>)` | Registers as Zephyr shell commands |
| **Basic serial** | Fallback (Arduino `Stream`) | Simple line-buffer + dispatch, no deps |

The console adapter handles the interactive experience (line editing, history,
VT100). embedded-menu handles command lookup, parameter parsing, help text,
and output routing.

## File structure

```
embedded-menu/
├── src/
│   ├── embedded_menu/
│   │   ├── registry.h           # core: command table, dispatch, introspection
│   │   ├── cmd.h                # Cmd abstraction (params + output)
│   │   ├── handler.h            # Handler type, CommandEntry, metadata
│   │   ├── schema.h             # introspection: command catalog, param types/ranges
│   │   ├── console/
│   │   │   ├── linenoise.h      # adapter (compile-time conditional)
│   │   │   ├── embedded_cli.h   # adapter
│   │   │   ├── esp_console.h    # adapter
│   │   │   ├── zephyr_shell.h   # adapter
│   │   │   └── basic_serial.h   # fallback, Arduino Stream
│   │   ├── transport/
│   │   │   ├── json_serial.h    # JSON lines over Stream/stdio (also serves Web Serial)
│   │   │   ├── notecard.h       # Notecard inbound/outbound Notes
│   │   │   └── binary.h         # framed binary protocol
│   │   └── notecard/
│   │       ├── passthrough.h    # raw JSON passthrough command
│   │       └── friendly.h       # CLI-friendly syntax (key=value → JSON)
│   └── embedded_menu.h          # convenience include
├── web/                         # SvelteKit web UI framework (Phase 4)
│   ├── src/lib/
│   │   ├── components/          # shared: CommandList, ParamInput, ResponseView
│   │   └── stores/              # registry schema, WebSocket state
│   └── package.json
├── library.json                 # PlatformIO
├── library.properties           # Arduino IDE
├── CMakeLists.txt               # CMake / Zephyr / Pico SDK
├── test/                        # native tests (no hardware deps)
├── examples/
│   ├── basic_json/              # minimal: registry + JSON serial
│   ├── pico_switch_board/       # Pico with JSON + basic serial
│   └── notecard_c2/             # Notecard cloud command & control
├── PROPOSAL.md                  # this document
└── README.md                    # positioning, quick start, prior art
```

## Phased implementation

### Phase 1: Core registry + JSON serial
- `registry.h` — command table, `add()`, `execute()`, `find()`, iteration
- `cmd.h` — `Cmd` with `param_*()` accessors, `Print& out()`, `reply()`
- `json_serial.h` — JSON lines transport (request/response)
- `basic_serial.h` — simple line-buffer console
- Native tests, no hardware deps
- **Validation target**: fixture-board Pico switch board firmware

### Phase 2: Console adapters + Notecard
- `linenoise.h`, `embedded_cli.h` adapters
- `notecard.h` transport with passthrough + friendly CLI syntax
- Notecard endpoint tab-completion from note-cpp schema (when available)
- **Validation target**: firmware `cli_service_plan.md` migration

### Phase 3: Schema introspection + binary
- `{"cmd":"schema"}` endpoint returning full command catalog with types/ranges
- `esp_console.h`, `zephyr_shell.h` adapters
- `binary.h` transport (embedded-bridge binary protocol)
- **Validation target**: UIH migration

### Phase 4: Web UI framework
- SvelteKit component library: auto-generated command controls from schema
- WebSocket transport adapter (device-hosted or host-bridged)
- Shared components: command list, parameter inputs, response explorer
- **Validation targets**:
  - Notecard developer UI (endpoint browser, request builder, REPL)
  - Provisioner dashboard (test progress, sensor results, pass/fail)
  - EEQ application UI (live mode, GPS, battery, phase timeline)

## Positioning: why this, why now

### What this is NOT

**Not another CLI library.** embedded-cli, SimpleCLI, and esp_console are
good at interactive serial consoles. embedded-menu doesn't replace them — it
*adapts* them. Use linenoise for line editing, embedded-cli for autocompletion,
esp_console if you're on ESP-IDF. embedded-menu provides the command
definitions they dispatch to.

**Not another menu system.** ArduinoMenu handles encoders, LCDs, and TFT
navigation trees. embedded-menu can drive a menu (commands have groups and
parameter schemas), but the rendering and input handling belong to whatever
UI toolkit fits your hardware.

**Not an RPC framework.** gRPC, Cap'n Proto, and nanopb define wire formats
and generate code. embedded-menu is simpler — it's the dispatch layer that
sits between your transport (whatever it is) and your handlers.

### What this IS

A way to **define a command once and dispatch it from everywhere**: serial
terminal, JSON API, binary protocol, cloud. The patterns for this exist in
web frameworks, OS shells, and RPC systems, but they haven't been packaged
for the embedded space where heap is scarce, transports vary wildly, and
the same firmware might talk to a developer's terminal, a test harness, and
a cloud service simultaneously.

### When you need this

- You have 2+ interfaces to the same device (serial + web, serial + cloud,
  serial + test automation)
- Adding a command means editing dispatch code in multiple places
- Your test harness parses the same serial output a human would read
- You want remote command-and-control but keep building one-off handlers
- You're about to write `if (strcmp(cmd, "...") == 0)` for the Nth time

### When you don't need this

- Single interface (just serial, just BLE)
- Handful of commands that won't grow
- Existing CLI library does everything you need

## Open questions

- **Registry factoring**: Should the command registry be its own micro-library
  (e.g., `embedded-registry`) separate from the transport/console layer? The
  registry is ~100 lines with no dependencies. Keeping it together is simpler;
  splitting it enables use without any transport overhead.

- **Handler signature**: `void(*)(Cmd&)` (function pointer, no heap) vs
  `std::function<void(Cmd&)>` (captures, heap-allocated). Could be configurable
  via a template parameter or build flag.

- **Parameter schema**: How rich? Simple `{name, type, range}` tuples for
  introspection, or full validation with error messages? The UIH use case wants
  ranges for auto-generating menu items and web UI sliders.

- **Menu tree generation**: Auto-generate on-board menu structure from command
  groups and param_schema, or require manual tree definition with registry
  lookup for handlers? Auto-generation is convenient but inflexible for
  custom menu layouts.

- **Notecard env vars**: Should the Notecard transport also bridge
  `env.get` / `card.attn` environment variable changes, or only inbound Notes?
  embedded-config-cpp already handles env vars — maybe they compose rather than
  overlap.
