#include "lighting.hpp"

#include <algorithm>
#include <charconv>
#include <format>

namespace ak650 {

namespace {

constexpr Direction kUpDown[] = {Direction::Up, Direction::Down};
constexpr Direction kLeftRight[] = {Direction::Left, Direction::Right};

// Mode numbers from the AK820 driver, the same on this board family. off isn't
// a mode of its own; see lighting_packet().
constexpr Effect kEffects[] = {
    {"off", 0x02, {}},
    {"static", 0x01, {}},
    {"single-on", 0x02, {}},
    {"single-off", 0x03, {}},
    {"glittering", 0x04, {}},
    {"falling", 0x05, {}},
    {"colourful", 0x06, {}},
    {"breath", 0x07, {}},
    {"spectrum", 0x08, {}},
    {"outward", 0x09, {}},
    {"scrolling", 0x0A, kUpDown},
    {"rolling", 0x0B, kLeftRight},
    {"rotating", 0x0C, {}},
    {"explode", 0x0D, {}},
    {"launch", 0x0E, {}},
    {"ripples", 0x0F, {}},
    {"flowing", 0x10, kLeftRight},
    {"pulsating", 0x11, {}},
    {"tilt", 0x12, kLeftRight},
    {"shuttle", 0x13, {}},
};

}  // namespace

std::span<const Effect> effects() { return kEffects; }

const Effect* find_effect(std::string_view name) {
    for (const Effect& effect : kEffects)
        if (effect.name == name) return &effect;
    return nullptr;
}

Packet lighting_packet(const Lighting& lighting) {
    const Effect* effect = find_effect(lighting.effect);
    if (!effect) throw Error(std::format("Unknown effect {}. `ak650 light list` shows them all.", lighting.effect));
    if (lighting.brightness < 0 || lighting.brightness > 5) throw Error("Brightness must be 0..5");
    if (lighting.speed < 0 || lighting.speed > 5) throw Error("Speed must be 0..5");

    Direction direction = Direction::Left;
    if (lighting.direction) {
        if (std::find(effect->directions.begin(), effect->directions.end(), *lighting.direction) ==
            effect->directions.end()) {
            std::string allowed;
            for (Direction d : effect->directions)
                allowed += std::format("{}{}", allowed.empty() ? "" : " or ", direction_name(d));
            throw Error(allowed.empty()
                            ? std::format("{} has no direction", effect->name)
                            : std::format("{} goes {}", effect->name, allowed));
        }
        direction = *lighting.direction;
    } else if (!effect->directions.empty()) {
        direction = effect->directions.front();
    }

    int brightness = lighting.brightness;
    int speed = lighting.speed;
    std::optional<Rgb> color = lighting.color;
    if (effect->name == "off") {
        brightness = 0;  // the firmware has no off mode, so it's an effect at brightness 0
        speed = 0;
    } else if (effect->name == "static") {
        // The AK820 tools send breath at speed 0 for this, but on the AK650 that
        // still breathes. Mode 01 is steady.
        speed = 0;
        if (!color) color = Rgb{255, 255, 255};
    }

    Packet packet{};
    packet[0] = effect->code;
    if (color) std::copy(color->begin(), color->end(), packet.begin() + 1);
    packet[8] = color ? 0 : 1;  // 1 = rainbow
    packet[9] = static_cast<std::uint8_t>(brightness);
    packet[10] = static_cast<std::uint8_t>(speed);
    packet[11] = static_cast<std::uint8_t>(direction);
    packet[14] = 0x55;
    packet[15] = 0xAA;
    return packet;
}

Rgb parse_color(std::string_view text) {
    if (text.starts_with('#')) text.remove_prefix(1);
    unsigned value = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (text.size() != 6 || ec != std::errc{} || end != text.data() + text.size())
        throw Error(std::format("Colour must be six hex digits like ff8800, got {}", text));
    return {static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 8),
            static_cast<std::uint8_t>(value)};
}

Direction parse_direction(std::string_view text) {
    if (text == "left") return Direction::Left;
    if (text == "right") return Direction::Right;
    if (text == "up") return Direction::Up;
    if (text == "down") return Direction::Down;
    throw Error("Direction must be left, right, up or down");
}

std::string_view direction_name(Direction direction) {
    switch (direction) {
    case Direction::Left: return "left";
    case Direction::Down: return "down";
    case Direction::Up: return "up";
    case Direction::Right: return "right";
    }
    return "?";
}

}  // namespace ak650
