#include <doctest/doctest.h>
#include <embedded_menu/detail/json_parser.h>
#include <string.h>

using namespace emenu::detail;

TEST_SUITE("json_parse_flat") {

TEST_CASE("empty object") {
    JsonPair pairs[4];
    CHECK(json_parse_flat("{}", pairs, 4) == 0);
}

TEST_CASE("string field") {
    JsonPair pairs[4];
    int n = json_parse_flat(R"({"cmd":"ping"})", pairs, 4);
    CHECK(n == 1);
    CHECK(strcmp(pairs[0].key, "cmd") == 0);
    CHECK(strcmp(pairs[0].value, "ping") == 0);
    CHECK(pairs[0].type == JsonTokenType::STRING);
}

TEST_CASE("integer field") {
    JsonPair pairs[4];
    int n = json_parse_flat(R"({"value":42})", pairs, 4);
    CHECK(n == 1);
    CHECK(strcmp(pairs[0].key, "value") == 0);
    CHECK(strcmp(pairs[0].value, "42") == 0);
    CHECK(pairs[0].type == JsonTokenType::INTEGER);
}

TEST_CASE("negative integer") {
    JsonPair pairs[4];
    int n = json_parse_flat(R"({"value":-7})", pairs, 4);
    CHECK(n == 1);
    CHECK(strcmp(pairs[0].value, "-7") == 0);
    CHECK(pairs[0].type == JsonTokenType::INTEGER);
}

TEST_CASE("float field") {
    JsonPair pairs[4];
    int n = json_parse_flat(R"({"temp":23.75})", pairs, 4);
    CHECK(n == 1);
    CHECK(strcmp(pairs[0].value, "23.75") == 0);
    CHECK(pairs[0].type == JsonTokenType::FLOAT);
}

TEST_CASE("bool fields") {
    JsonPair pairs[4];
    int n = json_parse_flat(R"({"a":true,"b":false})", pairs, 4);
    CHECK(n == 2);
    CHECK(pairs[0].type == JsonTokenType::BOOL_TRUE);
    CHECK(pairs[1].type == JsonTokenType::BOOL_FALSE);
}

TEST_CASE("null field") {
    JsonPair pairs[4];
    int n = json_parse_flat(R"({"x":null})", pairs, 4);
    CHECK(n == 1);
    CHECK(pairs[0].type == JsonTokenType::NULL_VAL);
    CHECK(strcmp(pairs[0].value, "") == 0);
}

TEST_CASE("multiple fields") {
    JsonPair pairs[8];
    int n = json_parse_flat(R"({"cmd":"set","value":42,"flag":true})", pairs, 8);
    CHECK(n == 3);
    CHECK(strcmp(pairs[0].key, "cmd") == 0);
    CHECK(strcmp(pairs[0].value, "set") == 0);
    CHECK(strcmp(pairs[1].key, "value") == 0);
    CHECK(strcmp(pairs[1].value, "42") == 0);
    CHECK(strcmp(pairs[2].key, "flag") == 0);
}

TEST_CASE("escaped string") {
    JsonPair pairs[4];
    int n = json_parse_flat(R"({"msg":"hello \"world\""})", pairs, 4);
    CHECK(n == 1);
    CHECK(strcmp(pairs[0].value, "hello \"world\"") == 0);
}

TEST_CASE("whitespace handling") {
    JsonPair pairs[4];
    int n = json_parse_flat("{ \"a\" : 1 , \"b\" : 2 }", pairs, 4);
    CHECK(n == 2);
    CHECK(strcmp(pairs[0].value, "1") == 0);
    CHECK(strcmp(pairs[1].value, "2") == 0);
}

TEST_CASE("invalid json") {
    JsonPair pairs[4];
    CHECK(json_parse_flat("not json", pairs, 4) == -1);
    CHECK(json_parse_flat("{bad}", pairs, 4) == -1);
    CHECK(json_parse_flat("", pairs, 4) == -1);
}

}  // TEST_SUITE

TEST_SUITE("JsonBuilder") {

TEST_CASE("builds valid json") {
    char buf[128];
    JsonBuilder jb(buf, sizeof(buf));
    jb.begin()
        .field("cmd", "ping")
        .field("value", 42)
        .field("ok", true)
        .field("temp", 23.5f, 1)
        .end();

    CHECK(strstr(buf, "\"cmd\":\"ping\"") != nullptr);
    CHECK(strstr(buf, "\"value\":42") != nullptr);
    CHECK(strstr(buf, "\"ok\":true") != nullptr);
    CHECK(strstr(buf, "\"temp\":23.5") != nullptr);
    CHECK(buf[0] == '{');
    size_t len = strlen(buf);
    CHECK(buf[len - 1] == '\n');
    CHECK(buf[len - 2] == '}');
}

TEST_CASE("escapes strings") {
    char buf[128];
    JsonBuilder jb(buf, sizeof(buf));
    jb.begin().field("msg", "hello \"world\"").end();
    CHECK(strstr(buf, "hello \\\"world\\\"") != nullptr);
}

}  // TEST_SUITE
