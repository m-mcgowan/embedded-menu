#include <doctest/doctest.h>
#include <embedded_menu/cmd.h>

using namespace emenu;

TEST_SUITE("Cmd") {

TEST_CASE("param_int") {
    BufferWriter<256> w;
    Cmd cmd(w);
    cmd.set_param("value", "42");
    CHECK(cmd.param_int("value") == 42);
    CHECK(cmd.param_int("missing", -1) == -1);
}

TEST_CASE("param_int negative") {
    BufferWriter<256> w;
    Cmd cmd(w);
    cmd.set_param("value", "-7");
    CHECK(cmd.param_int("value") == -7);
}

TEST_CASE("param_float") {
    BufferWriter<256> w;
    Cmd cmd(w);
    cmd.set_param("temp", "23.75");
    CHECK(cmd.param_float("temp") == doctest::Approx(23.75f));
    CHECK(cmd.param_float("missing") == doctest::Approx(0.0f));
}

TEST_CASE("param_str") {
    BufferWriter<256> w;
    Cmd cmd(w);
    cmd.set_param("name", "hello");
    CHECK(strcmp(cmd.param_str("name"), "hello") == 0);
    CHECK(strcmp(cmd.param_str("missing", "default"), "default") == 0);
}

TEST_CASE("param_bool") {
    BufferWriter<256> w;
    Cmd cmd(w);
    cmd.set_param("a", "true");
    cmd.set_param("b", "false");
    cmd.set_param("c", "1");
    cmd.set_param("d", "0");
    cmd.set_param("e", "yes");
    cmd.set_param("f", "no");
    cmd.set_param("g", "on");
    cmd.set_param("h", "off");
    CHECK(cmd.param_bool("a") == true);
    CHECK(cmd.param_bool("b") == false);
    CHECK(cmd.param_bool("c") == true);
    CHECK(cmd.param_bool("d") == false);
    CHECK(cmd.param_bool("e") == true);
    CHECK(cmd.param_bool("f") == false);
    CHECK(cmd.param_bool("g") == true);
    CHECK(cmd.param_bool("h") == false);
    CHECK(cmd.param_bool("missing") == false);
}

TEST_CASE("reply accumulation") {
    BufferWriter<256> w;
    Cmd cmd(w);
    cmd.reply("ok", true);
    cmd.reply("count", 42);
    CHECK(cmd.has_reply());
    CHECK(cmd.reply_count() == 2);
    CHECK(strcmp(cmd.reply_field(0).key, "ok") == 0);
    CHECK(strcmp(cmd.reply_field(0).value, "true") == 0);
    CHECK(strcmp(cmd.reply_field(1).key, "count") == 0);
    CHECK(strcmp(cmd.reply_field(1).value, "42") == 0);
}

TEST_CASE("out() captures output") {
    BufferWriter<256> w;
    Cmd cmd(w);
    cmd.out().print("hello");
    CHECK(strcmp(w.str(), "hello") == 0);
}

TEST_CASE("set_argv populates params") {
    BufferWriter<256> w;
    Cmd cmd(w);
    char arg0[] = "set";
    char arg1[] = "brightness";
    char arg2[] = "75";
    char* args[] = {arg0, arg1, arg2};
    cmd.set_argv(3, args);
    CHECK(cmd.argc() == 3);
    CHECK(strcmp(cmd.argv()[0], "set") == 0);
    CHECK(cmd.param_int("brightness") == 75);
}

TEST_CASE("reset clears state") {
    BufferWriter<256> w;
    Cmd cmd(w);
    cmd.set_param("x", "1");
    cmd.reply("ok", true);
    cmd.reset();
    CHECK(cmd.param_int("x", 0) == 0);
    CHECK(cmd.has_reply() == false);
}

TEST_CASE("reply buffer overflow returns null value") {
    BufferWriter<256> w;
    Cmd cmd(w);
    // Fill the 512-byte reply buffer
    char big[130];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    cmd.reply("a", big);  // 130 bytes stored
    cmd.reply("b", big);  // 260 total
    cmd.reply("c", big);  // 390 total
    cmd.reply("d", big);  // 520 total — exceeds 512, value = nullptr

    CHECK(cmd.reply_count() == 4);
    CHECK(cmd.reply_field(0).value != nullptr);
    CHECK(cmd.reply_field(1).value != nullptr);
    CHECK(cmd.reply_field(2).value != nullptr);
    CHECK(cmd.reply_field(3).value == nullptr);  // overflowed
}

TEST_CASE("reply int overflow returns null not dangling pointer") {
    BufferWriter<256> w;
    Cmd cmd(w);
    // 4 values of exactly 127 chars fill the 512-byte reply buffer completely
    // (127 chars + NUL = 128 bytes each, 4 × 128 = 512)
    char big[128];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    cmd.reply("a", big);
    cmd.reply("b", big);
    cmd.reply("c", big);
    cmd.reply("d", big);  // buffer now exactly full (512/512)
    // Int reply: "42\0" = 3 bytes, but buffer has 0 bytes left
    cmd.reply("e", 42);   // int → stack buffer, _store_reply returns nullptr

    CHECK(cmd.reply_count() == 5);
    CHECK(cmd.reply_field(3).value != nullptr);  // last one that fit
    CHECK(cmd.reply_field(4).value == nullptr);   // must be null, not dangling
}

}  // TEST_SUITE
