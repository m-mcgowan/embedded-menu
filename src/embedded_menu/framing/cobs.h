#pragma once

// COBS framing now lives in embedded-bridge.
// Re-export into the emenu namespace for backward compatibility.
#include <embedded_menu/writer.h>
#include <embedded_bridge/framing/cobs.h>

namespace emenu {

using ebridge::FrameCallback;
using ebridge::CobsFramer;
using ebridge::CobsFramingWriter;

}  // namespace emenu
