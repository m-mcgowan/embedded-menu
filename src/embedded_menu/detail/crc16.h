#pragma once

// CRC-16/HDLC now lives in embedded-bridge.
// Re-export into the emenu::detail namespace for backward compatibility.
#include <embedded_bridge/detail/crc16.h>

namespace emenu {
namespace detail {

using ebridge::detail::CRC16_INIT;
using ebridge::detail::CRC16_GOOD;
using ebridge::detail::crc16_hdlc_update;
using ebridge::detail::crc16_hdlc;

}  // namespace detail
}  // namespace emenu
