#include <doctest/doctest.h>
#include <embedded_menu/console/basic_serial.h>
#include <string.h>

using namespace emenu;

static bool ping_called = false;
static void ping_handler(Cmd& cmd) {
    ping_called = true;
    cmd.out().println("pong");
}

static int last_value = 0;
static void set_handler(Cmd& cmd) {
    last_value = cmd.param_int("brightness", -1);
    cmd.out().printf("brightness=%d\r\n", last_value);
}

TEST_SUITE("BasicSerial") {

TEST_CASE("simple dispatch") {
    Registry<8> reg;
    reg.add("ping", {ping_handler, "Echo a ping"});

    BufferWriter<512> out;
    BasicSerial<8> bs(reg, out);
    ping_called = false;
    bs.process_line("ping");

    CHECK(ping_called);
    CHECK(strstr(out.str(), "pong") != nullptr);
}

TEST_CASE("argv params") {
    Registry<8> reg;
    reg.add("set", {set_handler});

    BufferWriter<512> out;
    BasicSerial<8> bs(reg, out);
    last_value = 0;
    bs.process_line("set brightness 75");

    CHECK(last_value == 75);
}

TEST_CASE("unknown command") {
    Registry<8> reg;
    BufferWriter<512> out;
    BasicSerial<8> bs(reg, out);
    bs.process_line("nonexistent");

    CHECK(strstr(out.str(), "Unknown command") != nullptr);
}

TEST_CASE("empty line ignored") {
    Registry<8> reg;
    BufferWriter<512> out;
    BasicSerial<8> bs(reg, out);
    bs.process_line("");
    bs.process_line("   ");

    CHECK(out.len() == 0);
}

TEST_CASE("help lists commands") {
    Registry<8> reg;
    reg.add("ping", {ping_handler, "Echo a ping"});
    reg.add("set", {set_handler, "Set a value"});

    BufferWriter<512> out;
    BasicSerial<8> bs(reg, out);
    bs.process_line("help");

    CHECK(strstr(out.str(), "ping") != nullptr);
    CHECK(strstr(out.str(), "set") != nullptr);
}

TEST_CASE("help specific command") {
    Registry<8> reg;
    reg.add("ping", {ping_handler, "Echo a ping"});

    BufferWriter<512> out;
    BasicSerial<8> bs(reg, out);
    bs.process_line("help ping");

    CHECK(strstr(out.str(), "ping") != nullptr);
    CHECK(strstr(out.str(), "Echo a ping") != nullptr);
}

TEST_CASE("byte at a time") {
    Registry<8> reg;
    reg.add("ping", {ping_handler});

    BufferWriter<512> out;
    BasicSerial<8> bs(reg, out);
    ping_called = false;

    const char* input = "ping\n";
    while (*input) bs.process_byte(static_cast<uint8_t>(*input++));

    CHECK(ping_called);
}

}  // TEST_SUITE
