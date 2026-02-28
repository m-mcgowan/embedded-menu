#include <doctest/doctest.h>
#include <embedded_menu/transport/json_serial.h>
#include <string.h>

using namespace emenu;

static void ping_handler(Cmd& cmd) {
    cmd.reply("ok", true);
}

static void echo_handler(Cmd& cmd) {
    cmd.reply("value", cmd.param_int("value"));
}

static void greeting_handler(Cmd& cmd) {
    const char* name = cmd.param_str("name", "world");
    cmd.reply("greeting", name);
}

TEST_SUITE("JsonSerial") {

TEST_CASE("basic dispatch") {
    Registry<8> reg;
    reg.add("ping", {ping_handler, "Echo a ping"});

    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"ping"})");

    CHECK(strstr(out.str(), "\"cmd\":\"ping\"") != nullptr);
    CHECK(strstr(out.str(), "\"ok\":true") != nullptr);
}

TEST_CASE("with params") {
    Registry<8> reg;
    reg.add("echo", {echo_handler});

    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"echo","value":42})");

    CHECK(strstr(out.str(), "\"value\":42") != nullptr);
}

TEST_CASE("string param") {
    Registry<8> reg;
    reg.add("greet", {greeting_handler});

    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"greet","name":"Alice"})");

    CHECK(strstr(out.str(), "\"greeting\":\"Alice\"") != nullptr);
}

TEST_CASE("not found") {
    Registry<8> reg;
    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"unknown"})");

    CHECK(strstr(out.str(), "\"error\":\"not found\"") != nullptr);
}

TEST_CASE("missing cmd") {
    Registry<8> reg;
    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"value":42})");

    CHECK(strstr(out.str(), "\"error\":\"missing cmd\"") != nullptr);
}

TEST_CASE("parse error") {
    Registry<8> reg;
    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.process_line("not json");

    CHECK(strstr(out.str(), "\"error\":\"parse error\"") != nullptr);
}

TEST_CASE("byte at a time") {
    Registry<8> reg;
    reg.add("ping", {ping_handler});

    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);

    const char* line = "{\"cmd\":\"ping\"}\n";
    while (*line) js.process_byte(static_cast<uint8_t>(*line++));

    CHECK(strstr(out.str(), "\"ok\":true") != nullptr);
}

TEST_CASE("multiple lines") {
    Registry<8> reg;
    reg.add("ping", {ping_handler});
    reg.add("echo", {echo_handler});

    BufferWriter<1024> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"ping"})");
    js.process_line(R"({"cmd":"echo","value":99})");

    CHECK(strstr(out.str(), "\"ok\":true") != nullptr);
    CHECK(strstr(out.str(), "\"value\":99") != nullptr);
}

TEST_CASE("no reply gives ok") {
    Registry<8> reg;
    reg.add("noop", {[](Cmd&) {}});

    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"noop"})");

    CHECK(strstr(out.str(), "\"ok\":true") != nullptr);
}

}  // TEST_SUITE
