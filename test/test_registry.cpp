#include <doctest/doctest.h>
#include <embedded_menu/registry.h>

using namespace emenu;

static bool handler_called = false;
static void test_handler(Cmd&) { handler_called = true; }
static void other_handler(Cmd&) {}

TEST_SUITE("Registry") {

TEST_CASE("add and find") {
    Registry<8> reg;
    auto r = reg.add("ping", {test_handler, "Echo a ping"});
    CHECK(r == Result::OK);
    CHECK(reg.count() == 1);

    const auto* e = reg.find("ping");
    REQUIRE(e != nullptr);
    CHECK(strcmp(e->name, "ping") == 0);
    CHECK(strcmp(e->help, "Echo a ping") == 0);
}

TEST_CASE("find by alias") {
    Registry<8> reg;
    reg.add("power.ammeter", {test_handler, "Ammeter mode", "power", {"ammeter", "amm"}});
    CHECK(reg.find("ammeter") != nullptr);
    CHECK(reg.find("amm") != nullptr);
    CHECK(reg.find("power.ammeter") != nullptr);
}

TEST_CASE("find unknown returns nullptr") {
    Registry<8> reg;
    reg.add("ping", {test_handler});
    CHECK(reg.find("unknown") == nullptr);
}

TEST_CASE("execute calls handler") {
    Registry<8> reg;
    reg.add("ping", {test_handler});
    handler_called = false;

    BufferWriter<64> w;
    Cmd cmd(w);
    auto r = reg.execute("ping", cmd);
    CHECK(r == Result::OK);
    CHECK(handler_called);
}

TEST_CASE("execute not found") {
    Registry<8> reg;
    BufferWriter<64> w;
    Cmd cmd(w);
    CHECK(reg.execute("nope", cmd) == Result::NOT_FOUND);
}

TEST_CASE("table full") {
    Registry<2> reg;
    CHECK(reg.add("a", {test_handler}) == Result::OK);
    CHECK(reg.add("b", {other_handler}) == Result::OK);
    CHECK(reg.add("c", {test_handler}) == Result::TABLE_FULL);
}

TEST_CASE("duplicate") {
    Registry<8> reg;
    CHECK(reg.add("ping", {test_handler}) == Result::OK);
    CHECK(reg.add("ping", {other_handler}) == Result::DUPLICATE);
}

TEST_CASE("iteration") {
    Registry<8> reg;
    reg.add("a", {test_handler});
    reg.add("b", {other_handler});

    int count = 0;
    for (const auto& e : reg) {
        CHECK(e.valid());
        count++;
    }
    CHECK(count == 2);
}

TEST_CASE("find by group") {
    Registry<8> reg;
    reg.add("power.ammeter", {test_handler, nullptr, "power"});
    reg.add("power.voltmeter", {other_handler, nullptr, "power"});
    reg.add("screen.brightness", {test_handler, nullptr, "screen"});

    const CommandEntry* results[8];
    size_t n = reg.find_by_group("power", results, 8);
    CHECK(n == 2);
}

}  // TEST_SUITE
