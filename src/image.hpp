#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "error.hpp"

namespace ak650 {

inline constexpr int kScreen = 128;  // the screen is 128x128
inline constexpr std::size_t kHeaderBytes = 256;

struct Frame {
    std::vector<std::uint8_t> rgb;  // kScreen * kScreen * 3
    int delay_ms = 100;
};

enum class Fit { Crop, Pad, Stretch };

// Decodes a GIF or still image, flattens transparency onto black and fits every
// frame to the screen. Thins the frames out if there are more than max_frames.
std::vector<Frame> load_frames(const std::string& path, Fit fit, int max_frames);

// Keeps max_frames evenly spaced frames and folds the dropped frames' time into
// the kept ones, so the loop runs at the same speed, just choppier.
std::vector<Frame> thin_frames(std::vector<Frame> frames, int max_frames);

// The header stores each delay in one byte, in 2 ms units: 2..510 ms.
std::uint8_t device_delay(int ms);

// The bytes the keyboard stores: header, RGB565 frames, 0xFF padding to a 4 KiB multiple.
std::vector<std::uint8_t> encode(const std::vector<Frame>& frames);

// Writes a GIF of what the screen will show: 565 colour depth, the board's delays.
void save_preview(const std::vector<Frame>& frames, const std::string& out);

}  // namespace ak650
