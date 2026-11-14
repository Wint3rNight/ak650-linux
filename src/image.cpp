#include "image.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>

#include "msf_gif/msf_gif.h"
#include "stb/stb_image.h"
#include "stb/stb_image_resize2.h"

namespace ak650 {

namespace {

constexpr int kPixels = kScreen * kScreen;
constexpr std::size_t kChunkBytes = 4096;

struct StbFree {
    void operator()(void* p) const { stbi_image_free(p); }
};

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::string hint;
        // "a\ b" inside quotes keeps the backslash, so the name never matches.
        if (path.find("\\ ") != std::string::npos)
            hint = "\nThe path has backslashes before its spaces. Use quotes or backslashes, not both.";
        throw Error(std::format("Can't open {}{}", path, hint));
    }
    return {std::istreambuf_iterator<char>(in), {}};
}

// RGBA over black.
std::vector<std::uint8_t> flatten(const std::uint8_t* rgba, int w, int h) {
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w) * h * 3);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
        const unsigned alpha = rgba[4 * i + 3];
        for (int c = 0; c < 3; ++c)
            rgb[3 * i + c] = static_cast<std::uint8_t>((rgba[4 * i + c] * alpha + 127) / 255);
    }
    return rgb;
}

void resize(const std::uint8_t* src, int w, int h, int src_stride,
            std::uint8_t* dst, int dst_w, int dst_h, int dst_stride) {
    if (w == dst_w && h == dst_h) {
        for (int y = 0; y < h; ++y)
            std::memcpy(dst + y * dst_stride, src + y * src_stride, static_cast<std::size_t>(w) * 3);
        return;
    }
    if (!stbir_resize_uint8_linear(src, w, h, src_stride, dst, dst_w, dst_h, dst_stride, STBIR_RGB))
        throw Error("Out of memory while resizing the image");
}

std::vector<std::uint8_t> fit_to_screen(const std::vector<std::uint8_t>& rgb, int w, int h, Fit fit) {
    std::vector<std::uint8_t> out(kPixels * 3, 0);
    const int row = kScreen * 3;
    switch (fit) {
    case Fit::Crop: {
        // The centred square.
        const int side = std::min(w, h);
        const int x0 = (w - side) / 2, y0 = (h - side) / 2;
        const std::uint8_t* corner = rgb.data() + (static_cast<std::size_t>(y0) * w + x0) * 3;
        resize(corner, side, side, w * 3, out.data(), kScreen, kScreen, row);
        break;
    }
    case Fit::Pad: {
        const double scale = std::min(kScreen / static_cast<double>(w), kScreen / static_cast<double>(h));
        const int new_w = std::clamp(static_cast<int>(std::lround(w * scale)), 1, kScreen);
        const int new_h = std::clamp(static_cast<int>(std::lround(h * scale)), 1, kScreen);
        const int x0 = (kScreen - new_w) / 2, y0 = (kScreen - new_h) / 2;
        resize(rgb.data(), w, h, w * 3, out.data() + (y0 * kScreen + x0) * 3, new_w, new_h, row);
        break;
    }
    case Fit::Stretch:
        resize(rgb.data(), w, h, w * 3, out.data(), kScreen, kScreen, row);
        break;
    }
    return out;
}

}  // namespace

std::vector<Frame> load_frames(const std::string& path, Fit fit, int max_frames) {
    const std::vector<std::uint8_t> file = read_file(path);
    const int len = static_cast<int>(file.size());
    const bool is_gif = file.size() >= 4 && std::memcmp(file.data(), "GIF8", 4) == 0;

    int w = 0, h = 0, count = 1, channels = 0;
    int* raw_delays = nullptr;
    std::unique_ptr<stbi_uc, StbFree> pixels(
        is_gif ? stbi_load_gif_from_memory(file.data(), len, &raw_delays, &w, &h, &count, &channels, 4)
               : stbi_load_from_memory(file.data(), len, &w, &h, &channels, 4));
    std::unique_ptr<int, StbFree> delays(raw_delays);  // milliseconds
    if (!pixels)
        throw Error(std::format("Can't read {} as an image: {}", path, stbi_failure_reason()));

    std::vector<Frame> frames;
    frames.reserve(count);
    const std::size_t frame_bytes = static_cast<std::size_t>(w) * h * 4;
    for (int i = 0; i < count; ++i) {
        Frame frame;
        frame.rgb = fit_to_screen(flatten(pixels.get() + i * frame_bytes, w, h), w, h, fit);
        // Some GIFs store 0, which players treat as "default speed".
        frame.delay_ms = delays && delays.get()[i] > 0 ? delays.get()[i] : 100;
        frames.push_back(std::move(frame));
    }

    if (static_cast<int>(frames.size()) > max_frames) {
        std::printf("  %d frames is over the limit of %d; kept every ~%.1fth\n", count, max_frames,
                    count / static_cast<double>(max_frames));
        frames = thin_frames(std::move(frames), max_frames);
    }
    return frames;
}

std::vector<Frame> thin_frames(std::vector<Frame> frames, int max_frames) {
    const std::size_t n = frames.size(), keep = static_cast<std::size_t>(max_frames);
    if (n <= keep) return frames;
    std::vector<Frame> out;
    out.reserve(keep);
    for (std::size_t j = 0; j < keep; ++j) {
        const std::size_t first = j * n / keep, next = (j + 1) * n / keep;
        int delay = 0;
        for (std::size_t k = first; k < next; ++k) delay += frames[k].delay_ms;
        out.push_back({std::move(frames[first].rgb), delay});
    }
    return out;
}

std::uint8_t device_delay(int ms) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(ms / 2.0), 1L, 255L));
}

std::vector<std::uint8_t> encode(const std::vector<Frame>& frames) {
    if (frames.empty() || frames.size() > 255) throw Error("The screen takes 1 to 255 frames");

    std::vector<std::uint8_t> out(kHeaderBytes, 0xFF);
    out.reserve(kHeaderBytes + frames.size() * kPixels * 2 + kChunkBytes);
    out[0] = static_cast<std::uint8_t>(frames.size());
    if (frames.size() == 1) {
        out[1] = 0;  // a still image
    } else {
        for (std::size_t i = 0; i < frames.size(); ++i) out[1 + i] = device_delay(frames[i].delay_ms);
    }

    // RGB565, little-endian, row by row: what the GC9107 panel takes.
    for (const Frame& frame : frames) {
        for (int i = 0; i < kPixels; ++i) {
            const std::uint8_t r = frame.rgb[3 * i], g = frame.rgb[3 * i + 1], b = frame.rgb[3 * i + 2];
            out.push_back(static_cast<std::uint8_t>(((g << 3) & 0xE0) | (b >> 3)));
            out.push_back(static_cast<std::uint8_t>((r & 0xF8) | (g >> 5)));
        }
    }
    out.resize((out.size() + kChunkBytes - 1) / kChunkBytes * kChunkBytes, 0xFF);
    return out;
}

void save_preview(const std::vector<Frame>& frames, const std::string& path) {
    MsfGifState gif{};
    msf_gif_begin(&gif, kScreen, kScreen);
    std::vector<std::uint8_t> rgba(kPixels * 4);
    for (const Frame& frame : frames) {
        for (int i = 0; i < kPixels; ++i) {
            rgba[4 * i] = frame.rgb[3 * i] & 0xF8;
            rgba[4 * i + 1] = frame.rgb[3 * i + 1] & 0xFC;
            rgba[4 * i + 2] = frame.rgb[3 * i + 2] & 0xF8;
            rgba[4 * i + 3] = 0xFF;
        }
        // GIF stores delays in 10 ms steps, the board in 2 ms steps.
        const int shown_ms = device_delay(frame.delay_ms) * 2;
        const int centis = frames.size() == 1 ? 10 : std::max(1, (shown_ms + 5) / 10);
        msf_gif_frame(&gif, rgba.data(), centis, 16, kScreen * 4);
    }
    MsfGifResult result = msf_gif_end(&gif);
    if (!result.data) throw Error("Couldn't build the preview GIF");

    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(result.data), static_cast<std::streamsize>(result.dataSize));
    const bool ok = out.good();
    msf_gif_free(result);
    if (!ok) throw Error("Can't write " + path);
}

}  // namespace ak650
