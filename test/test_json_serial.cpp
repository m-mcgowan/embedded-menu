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

TEST_CASE("help lists all commands") {
    Registry<8> reg;
    reg.add("ping", {ping_handler, "Echo a ping"});
    reg.add("echo", {echo_handler, "Echo back", nullptr, {"e"}});

    BufferWriter<1024> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"help"})");

    CHECK(strstr(out.str(), "\"cmd\":\"help\"") != nullptr);
    CHECK(strstr(out.str(), "\"commands\":[") != nullptr);
    CHECK(strstr(out.str(), "\"name\":\"ping\"") != nullptr);
    CHECK(strstr(out.str(), "\"help\":\"Echo a ping\"") != nullptr);
    CHECK(strstr(out.str(), "\"name\":\"echo\"") != nullptr);
    CHECK(strstr(out.str(), "\"aliases\":[\"e\"]") != nullptr);
}

TEST_CASE("help with topic") {
    Registry<8> reg;
    reg.add("ping", {ping_handler, "Echo a ping"});
    reg.add("echo", {echo_handler, "Echo back"});

    BufferWriter<1024> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"help","topic":"ping"})");

    CHECK(strstr(out.str(), "\"cmd\":\"help\"") != nullptr);
    CHECK(strstr(out.str(), "\"name\":\"ping\"") != nullptr);
    CHECK(strstr(out.str(), "\"help\":\"Echo a ping\"") != nullptr);
    // Should NOT contain commands array (single item response)
    CHECK(strstr(out.str(), "\"commands\"") == nullptr);
}

TEST_CASE("help with unknown topic") {
    Registry<8> reg;
    reg.add("ping", {ping_handler});

    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"help","topic":"nonexistent"})");

    CHECK(strstr(out.str(), "\"error\":\"not found\"") != nullptr);
}

TEST_CASE("help with groups") {
    Registry<8> reg;
    reg.add("ping", {ping_handler, "Ping", nullptr});
    reg.add("led", {[](Cmd& cmd) { cmd.reply("ok", true); }, "LED ctrl", "hw"});

    BufferWriter<1024> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"help"})");

    CHECK(strstr(out.str(), "\"group\":null") != nullptr);  // ping has no group
    CHECK(strstr(out.str(), "\"group\":\"hw\"") != nullptr); // led has group
}

TEST_CASE("help empty registry") {
    Registry<8> reg;

    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"help"})");

    CHECK(strstr(out.str(), "\"commands\":[]") != nullptr);
}

TEST_CASE("tui enabled by default") {
    Registry<8> reg;
    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);

    CHECK(js.tui_enabled() == true);
}

TEST_CASE("tui disable") {
    Registry<8> reg;
    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);

    js.process_line(R"({"cmd":"tui","enabled":false})");

    CHECK(js.tui_enabled() == false);
    CHECK(strstr(out.str(), "\"cmd\":\"tui\"") != nullptr);
    CHECK(strstr(out.str(), "\"enabled\":false") != nullptr);
}

TEST_CASE("tui enable") {
    Registry<8> reg;
    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);

    // Disable first, then re-enable
    js.process_line(R"({"cmd":"tui","enabled":false})");
    CHECK(js.tui_enabled() == false);

    out.clear();
    js.process_line(R"({"cmd":"tui","enabled":true})");

    CHECK(js.tui_enabled() == true);
    CHECK(strstr(out.str(), "\"enabled\":true") != nullptr);
}

TEST_CASE("tui query without enabled param") {
    Registry<8> reg;
    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);

    // Just query current state
    js.process_line(R"({"cmd":"tui"})");

    CHECK(js.tui_enabled() == true);  // unchanged
    CHECK(strstr(out.str(), "\"enabled\":true") != nullptr);
}

TEST_CASE("tui does not conflict with registered commands") {
    Registry<8> reg;
    reg.add("ping", {ping_handler});

    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);

    js.process_line(R"({"cmd":"tui","enabled":false})");
    CHECK(js.tui_enabled() == false);

    out.clear();
    js.process_line(R"({"cmd":"ping"})");
    CHECK(strstr(out.str(), "\"ok\":true") != nullptr);
}

TEST_CASE("topic field does not leak into handler params") {
    // A handler that checks if it received a "topic" param
    Registry<8> reg;
    reg.add("check", {[](Cmd& cmd) {
        // If topic leaked through, param_str would find it
        const char* t = cmd.param_str("topic", "ABSENT");
        cmd.reply("topic_param", t);
    }});

    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"check","topic":"leaked"})");

    // topic should NOT appear as a handler param
    CHECK(strstr(out.str(), "\"topic_param\":\"ABSENT\"") != nullptr);
}

TEST_CASE("null reply value serializes as null") {
    // When a reply field has a null value (e.g. from buffer overflow),
    // the JSON serializer should emit null, not crash.
    Registry<8> reg;
    reg.add("nulltest", {[](Cmd& cmd) {
        cmd.reply("good", "hello");
        cmd.reply("bad", static_cast<const char*>(nullptr));
    }});

    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"nulltest"})");

    CHECK(strstr(out.str(), "\"good\":\"hello\"") != nullptr);
    CHECK(strstr(out.str(), "\"bad\":null") != nullptr);
}

TEST_CASE("control chars in help text are escaped") {
    Registry<8> reg;
    reg.add("test", {[](Cmd&) {}, "line1\x01line2"});

    BufferWriter<1024> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"help"})");

    // \x01 should be escaped as \u0001
    CHECK(strstr(out.str(), "\\u0001") != nullptr);
    // Should NOT contain raw control char
    CHECK(strstr(out.str(), "\x01") == nullptr);
}

TEST_CASE("control chars in reply string are escaped") {
    Registry<8> reg;
    reg.add("ctrl", {[](Cmd& cmd) {
        cmd.reply("msg", "hello\x02world");
    }});

    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"ctrl"})");

    CHECK(strstr(out.str(), "\\u0002") != nullptr);
}

TEST_CASE("large response streams without truncation") {
    // With streaming JsonWriter, responses are no longer limited to a
    // fixed buffer size. This test verifies a response that would have
    // exceeded the old 256-byte limit now succeeds.
    Registry<8> reg;
    reg.add("big", {[](Cmd& cmd) {
        char big[250];
        memset(big, 'X', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        cmd.reply("data", big);
    }});

    BufferWriter<1024> out;
    JsonSerial<8> js(reg, out);
    js.process_line(R"({"cmd":"big"})");

    CHECK(strstr(out.str(), "\"cmd\":\"big\"") != nullptr);
    CHECK(strstr(out.str(), "\"data\":\"XXXX") != nullptr);
    // Verify full 249 X's are present (not truncated)
    const char* data_start = strstr(out.str(), "\"data\":\"");
    REQUIRE(data_start != nullptr);
    data_start += 8;  // skip "data":"
    int x_count = 0;
    while (*data_start == 'X') { x_count++; data_start++; }
    CHECK(x_count == 249);
}

}  // TEST_SUITE
