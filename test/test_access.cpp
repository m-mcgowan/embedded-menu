#include <doctest/doctest.h>
#include <embedded_menu/transport/json_serial.h>
#include <string.h>

using namespace emenu;

namespace {
int ran = 0;
void status_handler(Cmd& cmd) { ran++; cmd.reply("ok", true); }
void reset_handler(Cmd& cmd) { ran++; cmd.reply("ok", true); }

constexpr uint8_t READ = 1;
constexpr uint8_t WRITE = 2;

Registry<8> make_registry() {
    Registry<8> reg;
    RegistrationOptions status{status_handler, "Report status"};
    status.access = READ;
    reg.add("status", status);
    RegistrationOptions reset{reset_handler, "Restart the device"};
    reset.access = WRITE;
    reg.add("reset", reset);
    return reg;
}
}  // namespace

TEST_SUITE("Access") {

TEST_CASE("a command above the session's access is refused and not run") {
    Registry<8> reg = make_registry();
    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.set_access(READ);
    ran = 0;
    js.process_line(R"({"cmd":"reset"})");
    CHECK(ran == 0);
    CHECK(strstr(out.str(), "\"error\":\"not permitted\"") != nullptr);
}

TEST_CASE("a command at or below the session's access runs") {
    Registry<8> reg = make_registry();
    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.set_access(WRITE);
    ran = 0;
    js.process_line(R"({"cmd":"status"})");
    js.process_line(R"({"cmd":"reset"})");
    CHECK(ran == 2);
}

TEST_CASE("no access refuses everything that needs any") {
    Registry<8> reg = make_registry();
    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.set_access(0);
    ran = 0;
    js.process_line(R"({"cmd":"status"})");
    CHECK(ran == 0);
}

TEST_CASE("help lists only the commands the session may run") {
    Registry<8> reg = make_registry();
    BufferWriter<1024> out;
    JsonSerial<8> js(reg, out);
    js.set_access(READ);
    js.process_line(R"({"cmd":"help"})");
    CHECK(strstr(out.str(), "\"name\":\"status\"") != nullptr);
    CHECK(strstr(out.str(), "\"name\":\"reset\"") == nullptr);
}

TEST_CASE("help on a command the session may not run is not found") {
    Registry<8> reg = make_registry();
    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    js.set_access(READ);
    js.process_line(R"({"cmd":"help","topic":"reset"})");
    CHECK(strstr(out.str(), "\"error\":\"not found\"") != nullptr);
}

TEST_CASE("without set_access every command runs, as before") {
    Registry<8> reg = make_registry();
    BufferWriter<512> out;
    JsonSerial<8> js(reg, out);
    ran = 0;
    js.process_line(R"({"cmd":"reset"})");
    CHECK(ran == 1);
}

}  // TEST_SUITE
