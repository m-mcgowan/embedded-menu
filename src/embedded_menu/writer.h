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

}  // namespace emenu
