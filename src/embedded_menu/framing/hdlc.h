#pragma once

// HDLC framing now lives in embedded-bridge.
// Re-export into the emenu namespace for backward compatibility.
#include <embedded_menu/writer.h>
#include <embedded_bridge/framing/hdlc.h>

namespace emenu {

using ebridge::FrameCallback;

namespace hdlc {
using ebridge::hdlc::FLAG;
using ebridge::hdlc::ESC;
using ebridge::hdlc::ESC_XOR;
using ebridge::hdlc::XON;
using ebridge::hdlc::XOFF;
}  // namespace hdlc

using ebridge::HdlcFramer;
using ebridge::HdlcFramingWriter;

}  // namespace emenu
