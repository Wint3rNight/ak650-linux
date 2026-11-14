#pragma once

#include <array>
#include <cstdint>

namespace ak650 {

// Every command and settings packet on the control interface is 64 bytes.
using Packet = std::array<std::uint8_t, 64>;

}  // namespace ak650
