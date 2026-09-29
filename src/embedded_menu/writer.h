#pragma once

// Writer and subclasses now live in embedded-bridge.
// Re-export into the emenu namespace for backward compatibility.
#include <embedded_bridge/writer.h>

namespace emenu {

using ebridge::Writer;
using ebridge::BufferWriter;
using ebridge::StdioWriter;
using ebridge::NullWriter;
#if __has_include(<Arduino.h>)
using ebridge::PrintWriter;
#endif

/// Buffers output and forwards each complete line to the next writer in one
/// write(), so a line cannot be split by anything else writing to the same port
/// (a log on another task, say). A line longer than N goes out in N-byte pieces
/// rather than being lost; flush() sends a partial line.
template<size_t N>
class LineWriter : public Writer {
public:
    explicit LineWriter(Writer& next) : _next(next) {}

    size_t write(uint8_t c) override {
        _buf[_len++] = static_cast<char>(c);
        if (c == '\n' || _len == N) flush();
        return 1;
    }

    size_t write(const uint8_t* buf, size_t len) override {
        for (size_t i = 0; i < len; i++) write(buf[i]);
        return len;
    }

    void flush() {
        if (_len == 0) return;
        _next.write(reinterpret_cast<const uint8_t*>(_buf), _len);
        _len = 0;
    }

    /// A message boundary (a framed transport's frame): send what is buffered,
    /// then pass the boundary on.
    void end_frame() override {
        flush();
        _next.end_frame();
    }

private:
    Writer& _next;
    char _buf[N];
    size_t _len = 0;
};

}  // namespace emenu
