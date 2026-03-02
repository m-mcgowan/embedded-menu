#include <doctest/doctest.h>
#include <embedded_menu/framing/hdlc.h>
#include <embedded_menu/transport/json_serial.h>

#include <cstring>
#include <string>
#include <vector>

using namespace emenu;

namespace {

// Capture raw bytes from downstream writer
class RawCapture : public Writer {
public:
    size_t write(uint8_t c) override {
        bytes.push_back(c);
        return 1;
    }
    std::vector<uint8_t> bytes;
    void clear() { bytes.clear(); }
};

// Decode HDLC frames from captured bytes
struct FrameCapture {
    std::vector<std::string> frames;

    static void callback(void* ctx, const uint8_t* data, size_t len) {
        auto* self = static_cast<FrameCapture*>(ctx);
        self->frames.emplace_back(reinterpret_cast<const char*>(data), len);
    }
};

}  // namespace

TEST_SUITE("Framed JsonSerial") {

TEST_CASE("end-to-end: HDLC-framed JSON command and response") {
    // Wire: raw_output <- HdlcFramingWriter <- JsonSerial <- HdlcFramer <- raw input
    RawCapture raw_output;
    HdlcFramingWriter<256> framed_output(raw_output);
    Registry<8> registry;
    JsonSerial<8> js(registry, framed_output);

    registry.add("ping", {[](Cmd& cmd) { cmd.reply("ok", true); }});

    // Build an HDLC-framed request
    RawCapture request_wire;
    HdlcFramingWriter<256> request_encoder(request_wire);
    request_encoder.print(R"({"cmd":"ping"})");
    request_encoder.end_frame();

    // Set up framer to feed decoded payloads into JsonSerial
    auto on_frame = [](void* ctx, const uint8_t* data, size_t len) {
        auto* serial = static_cast<JsonSerial<8>*>(ctx);
        // Null-terminate for process_line
        char buf[256];
        if (len < sizeof(buf)) {
            memcpy(buf, data, len);
            buf[len] = '\0';
            serial->process_line(buf);
        }
    };
    HdlcFramer<256> framer(on_frame, &js);

    // Feed the framed request bytes into the framer
    for (auto b : request_wire.bytes) framer.process_byte(b);

    // The response should be an HDLC-framed JSON response
    // Decode the response frames
    FrameCapture response_frames;
    HdlcFramer<256> response_decoder(FrameCapture::callback, &response_frames);
    for (auto b : raw_output.bytes) response_decoder.process_byte(b);

    REQUIRE(response_frames.frames.size() == 1);
    // JsonBuilder::end() appends \n — it's part of the framed payload
    CHECK(response_frames.frames[0] == "{\"cmd\":\"ping\",\"ok\":true}\n");
}

TEST_CASE("end-to-end: unknown command returns framed error") {
    RawCapture raw_output;
    HdlcFramingWriter<256> framed_output(raw_output);
    Registry<8> registry;
    JsonSerial<8> js(registry, framed_output);

    // Send unknown command via process_line (simulating framer delivery)
    js.process_line(R"({"cmd":"nope"})");

    // Decode the framed error response
    FrameCapture response_frames;
    HdlcFramer<256> decoder(FrameCapture::callback, &response_frames);
    for (auto b : raw_output.bytes) decoder.process_byte(b);

    REQUIRE(response_frames.frames.size() == 1);
    CHECK(response_frames.frames[0] == "{\"cmd\":\"nope\",\"error\":\"not found\"}\n");
}

TEST_CASE("end-to-end: parse error returns framed error") {
    RawCapture raw_output;
    HdlcFramingWriter<256> framed_output(raw_output);
    Registry<8> registry;
    JsonSerial<8> js(registry, framed_output);

    js.process_line("not json at all");

    FrameCapture response_frames;
    HdlcFramer<256> decoder(FrameCapture::callback, &response_frames);
    for (auto b : raw_output.bytes) decoder.process_byte(b);

    REQUIRE(response_frames.frames.size() == 1);
    CHECK(response_frames.frames[0] == "{\"error\":\"parse error\"}\n");
}

TEST_CASE("end-to-end: multiple commands produce separate frames") {
    RawCapture raw_output;
    HdlcFramingWriter<256> framed_output(raw_output);
    Registry<8> registry;
    JsonSerial<8> js(registry, framed_output);

    registry.add("echo", {[](Cmd& cmd) {
        const char* val = cmd.param_str("msg", "");
        cmd.reply("msg", val ? val : "");
    }});

    js.process_line(R"({"cmd":"echo","msg":"one"})");
    js.process_line(R"({"cmd":"echo","msg":"two"})");

    FrameCapture response_frames;
    HdlcFramer<256> decoder(FrameCapture::callback, &response_frames);
    for (auto b : raw_output.bytes) decoder.process_byte(b);

    REQUIRE(response_frames.frames.size() == 2);
    CHECK(response_frames.frames[0] == "{\"cmd\":\"echo\",\"msg\":\"one\"}\n");
    CHECK(response_frames.frames[1] == "{\"cmd\":\"echo\",\"msg\":\"two\"}\n");
}

}  // TEST_SUITE
