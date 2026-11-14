#pragma once

#include <atomic>
#include <cstdint>
#include <ctime>
#include <optional>
#include <span>
#include <string>

#include "error.hpp"
#include "packet.hpp"

namespace ak650 {

inline constexpr std::size_t kChunk = 4096;

struct Interfaces {
    std::string control;  // 0xFF13: 64-byte feature reports, commands
    std::string data;     // 0xFF68: 4096-byte output reports, pixel data
};

// Finds the one connected AK650 by HID name plus the exact report descriptors of
// its two vendor interfaces. hidraw numbers change on every replug.
Interfaces find_interfaces();

// The command that installs the udev rule, for error messages.
const char* udev_hint();

// Set from the SIGINT handler. upload() checks it between chunks so Ctrl-C still
// ends the transfer properly instead of leaving the board mid-write.
extern std::atomic<bool> g_interrupted;

class Keyboard {
public:
    explicit Keyboard(bool verbose = false);
    ~Keyboard();
    Keyboard(const Keyboard&) = delete;
    Keyboard& operator=(const Keyboard&) = delete;

    const Interfaces& paths() const { return paths_; }

    // GET_FEATURE only, sends nothing.
    Packet read_feature();
    // payload is what image.hpp's encode() returns: a multiple of kChunk bytes.
    void upload(std::span<const std::uint8_t> payload, int slot);
    void sync_clock(const std::tm& now);
    // effect is what lighting.hpp's lighting_packet() returns.
    void set_lighting(const Packet& effect);

private:
    Packet exchange(const Packet& packet);
    void command(std::uint8_t cmd, std::uint8_t sub = 0, std::uint16_t value = 0);
    void drain();
    std::optional<Packet> send_chunk(std::span<const std::uint8_t> chunk);
    void write_setting(std::uint8_t cmd, const Packet& data);

    Interfaces paths_;
    int ctrl_ = -1;
    int data_ = -1;
    bool verbose_ = false;
};

}  // namespace ak650
