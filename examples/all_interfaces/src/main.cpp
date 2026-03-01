/// embedded-menu example: all interfaces on a single serial port.
///
/// Lines starting with '{' are routed to the JSON serial transport.
/// Everything else is handled by the basic serial console.
///
/// Try both:
///   {"cmd":"ping"}             → {"cmd":"ping","ok":true}
///   ping                       → pong
///   help                       → lists all commands
///   echo msg hello             → msg: hello
///   {"cmd":"add","a":2,"b":3}  → {"cmd":"add","sum":5}

#include <Arduino.h>
#include <embedded_menu.h>

using namespace emenu;

// --- Command handlers (defined once, dispatched from both interfaces) ---

static void ping_handler(Cmd& cmd) {
    if (cmd.source() == CmdSource::ARGV) {
        cmd.out().println("pong");
    } else {
        cmd.reply("ok", true);
    }
}

static void echo_handler(Cmd& cmd) {
    const char* msg = cmd.param_str("msg", "");
    if (cmd.source() == CmdSource::ARGV) {
        cmd.out().printf("msg: %s\r\n", msg);
    } else {
        cmd.reply("msg", msg);
    }
}

static void led_handler(Cmd& cmd) {
    bool state = cmd.param_bool("state", false);
#ifdef LED_BUILTIN
    digitalWrite(LED_BUILTIN, state ? HIGH : LOW);
#endif
    if (cmd.source() == CmdSource::ARGV) {
        cmd.out().printf("LED %s\r\n", state ? "on" : "off");
    } else {
        cmd.reply("state", state);
    }
}

static void add_handler(Cmd& cmd) {
    int a = cmd.param_int("a", 0);
    int b = cmd.param_int("b", 0);
    if (cmd.source() == CmdSource::ARGV) {
        cmd.out().println(a + b);
    } else {
        cmd.reply("sum", a + b);
    }
}

static void info_handler(Cmd& cmd) {
    const char* board =
#if defined(ARDUINO_ARCH_ESP32)
        "esp32s3";
#elif defined(ARDUINO_ARCH_RP2040)
        "pico";
#else
        "unknown";
#endif

    if (cmd.source() == CmdSource::ARGV) {
        cmd.out().printf("board:    %s\r\n", board);
        cmd.out().printf("firmware: embedded-menu-example\r\n");
        cmd.out().printf("version:  0.1.0\r\n");
        cmd.out().printf("uptime:   %lu ms\r\n", millis());
    } else {
        cmd.reply("board", board);
        cmd.reply("firmware", "embedded-menu-example");
        cmd.reply("version", "0.1.0");
        cmd.reply("uptime_ms", static_cast<int>(millis()));
    }
}

static void sleep_handler(Cmd& cmd) {
    int ms = cmd.param_int("ms", 5000);
    if (cmd.source() == CmdSource::ARGV) {
        cmd.out().printf("deep sleep %d ms\r\n", ms);
    } else {
        cmd.reply("sleeping", ms);
    }
    // Flush and wait for USB-CDC to transmit before sleeping.
    // USB-CDC needs time to complete the USB transaction.
    Serial.flush();
    delay(500);
#if defined(ARDUINO_ARCH_ESP32)
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(ms) * 1000);
    esp_deep_sleep_start();
#elif defined(ARDUINO_ARCH_RP2040)
    // RP2040 doesn't have true deep sleep with timer wakeup via Arduino API,
    // fall back to light sleep
    delay(ms);
    // Force USB re-enumeration
    rp2040.reboot();
#endif
}

// --- Registry and both transports ---

static Registry<16> registry;
static PrintWriter serial_writer(Serial);
static JsonSerial<16> json_transport(registry, serial_writer);
static BasicSerial<16> console(registry, serial_writer);

// Line buffer for routing between transports
static constexpr size_t LINE_BUF_SIZE = 256;
static char line_buf[LINE_BUF_SIZE];
static size_t line_pos = 0;

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);

#ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
#endif

    registry.add("ping",  {ping_handler,  "Echo a ping"});
    registry.add("echo",  {echo_handler,  "Echo back a message", nullptr, {"e"}});
    registry.add("led",   {led_handler,   "Control built-in LED", "hw"});
    registry.add("add",   {add_handler,   "Add two numbers", "math"});
    registry.add("info",  {info_handler,  "Device info", "system"});
    registry.add("sleep", {sleep_handler, "Sleep for N ms", "system"});

    serial_writer.println("embedded-menu example — type 'help' or send JSON");
    console.set_show_prompt(true);
    console.prompt();
}

void loop() {
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n' || c == '\r') {
            if (line_pos > 0) {
                line_buf[line_pos] = '\0';
                // Route: '{' → JSON, anything else → console
                if (line_buf[0] == '{') {
                    json_transport.process_line(line_buf);
                } else {
                    console.process_line(line_buf);
                }
                line_pos = 0;
            }
            continue;
        }

        if (line_pos < LINE_BUF_SIZE - 1) {
            line_buf[line_pos++] = c;
        }
    }
}
