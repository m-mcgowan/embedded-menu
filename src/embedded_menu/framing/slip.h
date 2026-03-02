#pragma once

// SLIP framing now lives in embedded-bridge.
// Re-export into the emenu namespace for backward compatibility.
#include <embedded_menu/writer.h>
#include <embedded_bridge/framing/slip.h>

namespace emenu {

using ebridge::FrameCallback;

namespace slip {
using ebridge::slip::END;
using ebridge::slip::ESC;
using ebridge::slip::ESC_END;
using ebridge::slip::ESC_ESC;
}  // namespace slip

using ebridge::SlipFramer;
using ebridge::SlipFramingWriter;

}  // namespace emenu
