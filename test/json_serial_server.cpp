/// Minimal JSON serial server over stdin/stdout.
/// Used by Python integration tests to verify the protocol end-to-end.

#include <embedded_menu/transport/json_serial.h>
#include <cstdio>

using namespace emenu;

/// Writer that emits \n + fflush on end_frame(), so each JSON response
/// is a complete line that the Python side can readline().
class LineFlushedWriter : public ebridge::Writer {
public:
    size_t write(uint8_t c) override {
        fputc(c, stdout);
        return 1;
    }
    void end_frame() override {
        fputc('\n', stdout);
        fflush(stdout);
    }
};

static void ping_handler(Cmd& cmd) {
    cmd.reply("ok", true);
}

static void echo_handler(Cmd& cmd) {
    cmd.reply("value", cmd.param_str("value", ""));
}

static void add_handler(Cmd& cmd) {
    int a = cmd.param_int("a", 0);
    int b = cmd.param_int("b", 0);
    cmd.reply("sum", a + b);
}

int main() {
    Registry<8> reg;
    reg.add("ping", {ping_handler, "Echo a ping"});
    reg.add("echo", {echo_handler, "Echo a value"});
    reg.add("add",  {add_handler,  "Add two numbers", "math"});

    LineFlushedWriter writer;
    JsonSerial<8> js(reg, writer);

    // TUI banner — simulates what real firmware prints on boot
    fprintf(stdout, "=== Menu ===\n");
    fprintf(stdout, "Type help for commands\n");
    fprintf(stdout, "> ");
    fflush(stdout);

    bool pending_newline = false;
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        // Track when a line was just dispatched
        bool was_at_zero = (c == '\n' || c == '\r');
        js.process_byte(static_cast<uint8_t>(c));

        // After a line is dispatched, emit a TUI prompt if TUI is enabled
        if (was_at_zero && js.tui_enabled()) {
            fprintf(stdout, "> ");
            fflush(stdout);
        }
    }

    return 0;
}
