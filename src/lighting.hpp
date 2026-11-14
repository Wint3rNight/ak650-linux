#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "error.hpp"
#include "packet.hpp"

namespace ak650 {

enum class Direction : std::uint8_t { Left = 0, Down = 1, Up = 2, Right = 3 };

using Rgb = std::array<std::uint8_t, 3>;

struct Effect {
    std::string_view name;
    std::uint8_t code;
    std::span<const Direction> directions;  // empty: the effect has no direction
};

// The built-in effects, named after the AK820 driver's list.
std::span<const Effect> effects();
const Effect* find_effect(std::string_view name);

struct Lighting {
    std::string effect = "static";
    std::optional<Rgb> color;  // none: rainbow, or white for static
    int brightness = 5;        // 0..5
    int speed = 3;             // 0..5
    std::optional<Direction> direction;
};

// Checks everything and builds the data packet, so a bad option fails before
// anything is sent.
Packet lighting_packet(const Lighting& lighting);

Rgb parse_color(std::string_view text);  // "ff8800" or "#ff8800"
Direction parse_direction(std::string_view text);
std::string_view direction_name(Direction direction);

}  // namespace ak650
