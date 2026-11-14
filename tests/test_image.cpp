// Checks the part that needs no keyboard: decoding, delays and the payload layout.
#include <cstdio>
#include <numeric>

#include "check.hpp"
#include "image.hpp"

using namespace ak650;


static Frame solid(std::uint8_t r, std::uint8_t g, std::uint8_t b, int delay_ms = 100) {
    Frame frame;
    frame.rgb.resize(kScreen * kScreen * 3);
    for (std::size_t i = 0; i < frame.rgb.size(); i += 3) {
        frame.rgb[i] = r;
        frame.rgb[i + 1] = g;
        frame.rgb[i + 2] = b;
    }
    frame.delay_ms = delay_ms;
    return frame;
}

static void test_device_delay() {
    CHECK(device_delay(0) == 1);
    CHECK(device_delay(2) == 1);
    CHECK(device_delay(100) == 50);
    CHECK(device_delay(510) == 255);
    CHECK(device_delay(5000) == 255);
}

static void test_rgb565() {
    // Low byte first.
    auto first_pixel = [](std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        auto payload = encode({solid(r, g, b)});
        return std::pair<int, int>{payload[kHeaderBytes], payload[kHeaderBytes + 1]};
    };
    CHECK(first_pixel(255, 0, 0) == std::pair(0x00, 0xF8));
    CHECK(first_pixel(0, 255, 0) == std::pair(0xE0, 0x07));
    CHECK(first_pixel(0, 0, 255) == std::pair(0x1F, 0x00));
    CHECK(first_pixel(255, 255, 255) == std::pair(0xFF, 0xFF));
    CHECK(first_pixel(0, 0, 0) == std::pair(0x00, 0x00));
}

static void test_header_and_padding() {
    auto still = encode({solid(1, 2, 3)});
    CHECK(still[0] == 1);
    CHECK(still[1] == 0);     // delay 0 marks a still image
    CHECK(still[2] == 0xFF);  // the rest of the header is padding
    // 256 + 32768 = 33024 bytes, padded up to 9 chunks.
    CHECK(still.size() == 9 * 4096);
    CHECK(still.back() == 0xFF);

    auto anim = encode({solid(0, 0, 0, 60), solid(0, 0, 0, 70), solid(0, 0, 0, 1000)});
    CHECK(anim[0] == 3);
    CHECK(anim[1] == 30);
    CHECK(anim[2] == 35);
    CHECK(anim[3] == 255);
    CHECK(anim[4] == 0xFF);
    CHECK(anim.size() % 4096 == 0);
}

static void test_thinning_keeps_loop_length() {
    std::vector<Frame> frames;
    for (int i = 0; i < 27; ++i) frames.push_back(solid(static_cast<std::uint8_t>(i), 0, 0, i % 2 ? 60 : 70));
    const int before = std::accumulate(frames.begin(), frames.end(), 0,
                                       [](int sum, const Frame& f) { return sum + f.delay_ms; });
    auto thinned = thin_frames(frames, 10);
    const int after = std::accumulate(thinned.begin(), thinned.end(), 0,
                                      [](int sum, const Frame& f) { return sum + f.delay_ms; });
    CHECK(thinned.size() == 10);
    CHECK(after == before);
    CHECK(thinned.front().rgb[0] == 0);  // always starts on the first frame
    CHECK(thin_frames(frames, 30).size() == 27);
}

static void test_pattern_gif() {
    // 128x128, 8 frames at 250 ms, yellow 1 px border.
    auto frames = load_frames(TEST_PATTERN, Fit::Crop, 30);
    CHECK(frames.size() == 8);
    for (const Frame& frame : frames) CHECK(frame.delay_ms == 250);
    const auto& rgb = frames[0].rgb;
    CHECK(rgb[0] > 200 && rgb[1] > 200 && rgb[2] < 60);

    auto payload = encode(frames);
    CHECK(payload.size() == 65 * 4096);
    CHECK(payload[0] == 8);
    CHECK(payload[1] == 125);

    CHECK(load_frames(TEST_PATTERN, Fit::Crop, 4).size() == 4);
}

static void test_bad_files() {
    bool threw = false;
    try {
        load_frames("/nonexistent/nothing.gif", Fit::Crop, 30);
    } catch (const Error&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    test_device_delay();
    test_rgb565();
    test_header_and_padding();
    test_thinning_keeps_loop_length();
    test_pattern_gif();
    test_bad_files();
    if (failures) return 1;
    std::printf("all image tests passed\n");
    return 0;
}
