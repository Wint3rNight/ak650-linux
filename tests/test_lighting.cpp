// Lighting packets. The layout follows the AK820 tool's packet fixtures.
#include "check.hpp"
#include "lighting.hpp"

using namespace ak650;

template <typename F>
static bool throws(F f) {
    try {
        f();
    } catch (const Error&) {
        return true;
    }
    return false;
}

static void test_static_packet() {
    // Mode 01 at speed 0: steady on the AK650, tested on the board.
    Lighting l;
    l.effect = "static";
    l.color = Rgb{0x12, 0x34, 0x56};
    l.brightness = 3;
    l.speed = 5;
    const Packet p = lighting_packet(l);
    CHECK(p[0] == 0x01 && p[1] == 0x12 && p[2] == 0x34 && p[3] == 0x56);
    const std::uint8_t tail[] = {0, 3, 0, 0, 0, 0, 0x55, 0xAA};
    for (int i = 0; i < 8; ++i) CHECK(p[8 + i] == tail[i]);
    for (int i = 16; i < 64; ++i) CHECK(p[i] == 0);
}

static void test_off_and_static_defaults() {
    Lighting off;
    off.effect = "off";
    const Packet p = lighting_packet(off);
    CHECK(p[0] == 0x02 && p[9] == 0 && p[10] == 0);

    Lighting white;
    white.effect = "static";
    const Packet w = lighting_packet(white);
    CHECK(w[1] == 255 && w[2] == 255 && w[3] == 255 && w[8] == 0);
}

static void test_rainbow_and_direction() {
    Lighting l;
    l.effect = "rolling";
    l.direction = Direction::Right;
    l.speed = 4;
    const Packet p = lighting_packet(l);
    CHECK(p[0] == 0x0B && p[8] == 1 && p[10] == 4 && p[11] == 3);

    Lighting scroll;
    scroll.effect = "scrolling";
    CHECK(lighting_packet(scroll)[11] == static_cast<int>(Direction::Up));
}

static void test_rejects_bad_input() {
    Lighting l;
    l.effect = "disco";
    CHECK(throws([&] { lighting_packet(l); }));
    l.effect = "scrolling";
    l.direction = Direction::Left;
    CHECK(throws([&] { lighting_packet(l); }));
    l.effect = "breath";
    CHECK(throws([&] { lighting_packet(l); }));
    l.direction.reset();
    l.brightness = 6;
    CHECK(throws([&] { lighting_packet(l); }));

    CHECK(parse_color("ff8800") == (Rgb{0xFF, 0x88, 0x00}));
    CHECK(parse_color("#00FF7f") == (Rgb{0x00, 0xFF, 0x7F}));
    CHECK(throws([] { parse_color("ff88"); }));
    CHECK(throws([] { parse_color("gg0000"); }));
    CHECK(throws([] { parse_direction("sideways"); }));
}

int main() {
    test_static_packet();
    test_off_and_static_defaults();
    test_rainbow_and_direction();
    test_rejects_bad_input();
    if (failures) return 1;
    std::printf("all lighting tests passed\n");
    return 0;
}
