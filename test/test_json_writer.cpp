#include <doctest/doctest.h>
#include <embedded_menu/detail/json_writer.h>
#include <string.h>

using namespace emenu;
using namespace emenu::detail;

TEST_SUITE("JsonWriter") {

TEST_CASE("flat object") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin().field("cmd", "ping").field("ok", true).end();

    CHECK(strcmp(out.str(), "{\"cmd\":\"ping\",\"ok\":true}\n") == 0);
}

TEST_CASE("int and float fields") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin().field("count", 42).field("temp", 23.5f, 1).end();

    CHECK(strstr(out.str(), "\"count\":42") != nullptr);
    CHECK(strstr(out.str(), "\"temp\":23.5") != nullptr);
}

TEST_CASE("null string field") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin().field("value", static_cast<const char*>(nullptr)).end();

    CHECK(strstr(out.str(), "\"value\":null") != nullptr);
}

TEST_CASE("bool fields") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin().field("a", true).field("b", false).end();

    CHECK(strstr(out.str(), "\"a\":true") != nullptr);
    CHECK(strstr(out.str(), "\"b\":false") != nullptr);
}

TEST_CASE("nested object") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin()
        .field("cmd", "status")
        .key("info").begin_object()
            .field("version", "1.0")
            .field("uptime", 3600)
        .end_object()
    .end();

    CHECK(strstr(out.str(), "\"info\":{\"version\":\"1.0\",\"uptime\":3600}") != nullptr);
}

TEST_CASE("array of strings") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin()
        .key("names").begin_array()
            .value("alice")
            .value("bob")
        .end_array()
    .end();

    CHECK(strstr(out.str(), "\"names\":[\"alice\",\"bob\"]") != nullptr);
}

TEST_CASE("empty array") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin()
        .key("items").begin_array()
        .end_array()
    .end();

    CHECK(strstr(out.str(), "\"items\":[]") != nullptr);
}

TEST_CASE("array of objects") {
    BufferWriter<512> out;
    JsonWriter jw(out);
    jw.begin()
        .field("cmd", "help")
        .key("commands").begin_array()
            .begin_object().field("name", "ping").end_object()
            .begin_object().field("name", "echo").end_object()
        .end_array()
    .end();

    CHECK(strstr(out.str(), "\"commands\":[{\"name\":\"ping\"},{\"name\":\"echo\"}]") != nullptr);
}

TEST_CASE("field after array") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin()
        .key("tags").begin_array()
            .value("a")
        .end_array()
        .field("count", 1)
    .end();

    CHECK(strstr(out.str(), "\"tags\":[\"a\"],\"count\":1") != nullptr);
}

TEST_CASE("field after nested object") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin()
        .key("inner").begin_object()
            .field("x", 1)
        .end_object()
        .field("ok", true)
    .end();

    CHECK(strstr(out.str(), "\"inner\":{\"x\":1},\"ok\":true") != nullptr);
}

TEST_CASE("string escaping") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin().field("msg", "hello \"world\"").end();

    CHECK(strstr(out.str(), "hello \\\"world\\\"") != nullptr);
}

TEST_CASE("control char escaping") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin().field("msg", "a\x01" "b").end();

    CHECK(strstr(out.str(), "a\\u0001b") != nullptr);
}

TEST_CASE("null value standalone") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin()
        .key("x").null_value()
    .end();

    CHECK(strstr(out.str(), "\"x\":null") != nullptr);
}

TEST_CASE("null in array") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin()
        .key("arr").begin_array()
            .value("ok")
            .value(static_cast<const char*>(nullptr))
        .end_array()
    .end();

    CHECK(strstr(out.str(), "[\"ok\",null]") != nullptr);
}

TEST_CASE("deeply nested") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin()
        .key("a").begin_object()
            .key("b").begin_object()
                .field("c", 1)
            .end_object()
        .end_object()
    .end();

    CHECK(strstr(out.str(), "\"a\":{\"b\":{\"c\":1}}") != nullptr);
}

TEST_CASE("end emits newline and end_frame") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin().field("ok", true).end();

    const char* s = out.str();
    size_t len = strlen(s);
    CHECK(s[len - 1] == '\n');
    CHECK(s[len - 2] == '}');
}

TEST_CASE("single field object") {
    BufferWriter<256> out;
    JsonWriter jw(out);
    jw.begin().field("x", 1).end();

    CHECK(strcmp(out.str(), "{\"x\":1}\n") == 0);
}

TEST_CASE("multiple arrays in same object") {
    BufferWriter<512> out;
    JsonWriter jw(out);
    jw.begin()
        .key("a").begin_array().value("1").end_array()
        .key("b").begin_array().value("2").end_array()
    .end();

    CHECK(strstr(out.str(), "\"a\":[\"1\"],\"b\":[\"2\"]") != nullptr);
}

}  // TEST_SUITE
