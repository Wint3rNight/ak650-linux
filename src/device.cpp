#include "device.hpp"

#include <fcntl.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <thread>
#include <vector>

namespace ak650 {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

std::atomic<bool> g_interrupted{false};

namespace {

template <std::size_t N>
consteval std::array<std::uint8_t, (N - 1) / 2> hex(const char (&text)[N]) {
    auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::array<std::uint8_t, (N - 1) / 2> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<std::uint8_t>(nibble(text[2 * i]) << 4 | nibble(text[2 * i + 1]));
    return out;
}

// Report descriptors of the two vendor interfaces. They're byte-identical to the
// AK820 Pro boards this protocol was worked out on, so anything else is refused.
constexpr auto kControlDesc =
    hex("0613ff0901a101150026ff007508954009028102090391020a00ffb102c0");
constexpr auto kDataDesc =
    hex("0668ff0961a1010962150026ff009540750881020963150026ff0096001075089102c0");

enum Cmd : std::uint8_t {
    Start = 0x18,
    ImageCfg = 0x72,
    Clock = 0x28,
    Commit = 0x02,
    Finish = 0xF0,
};

// The vendor driver waits this long before every command; the board drops faster ones.
constexpr auto kCmdDelay = 35ms;
constexpr auto kReplyDelay = 50ms;

std::string hex_dump(std::span<const std::uint8_t> bytes) {
    std::string out;
    for (std::uint8_t b : bytes) out += std::format("{}{:02x}", out.empty() ? "" : " ", b);
    return out;
}

std::optional<std::string> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

bool is_ak650(const std::string& uevent) {
    auto at = uevent.find("HID_NAME=");
    if (at == std::string::npos) return false;
    auto line = uevent.substr(at, uevent.find('\n', at) - at);
    return line.find("AK650") != std::string::npos;
}

template <std::size_t N>
bool same(const std::string& desc, const std::array<std::uint8_t, N>& want) {
    return desc.size() == N && std::equal(want.begin(), want.end(), desc.begin(),
                                          [](std::uint8_t a, char b) { return a == static_cast<std::uint8_t>(b); });
}

template <std::size_t N>
bool same_usage_page(const std::string& desc, const std::array<std::uint8_t, N>& want) {
    return desc.size() >= 3 && std::equal(want.begin(), want.begin() + 3, desc.begin(),
                                          [](std::uint8_t a, char b) { return a == static_cast<std::uint8_t>(b); });
}

const char* const kUdevHint =
    "Install the udev rule once (it's in the repo):\n"
    "  sudo install -m644 70-ajazz-ak650.rules /etc/udev/rules.d/ && "
    "sudo udevadm control --reload && sudo udevadm trigger --subsystem-match=hidraw";

}  // namespace

Interfaces find_interfaces() {
    std::vector<fs::path> nodes;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/sys/class/hidraw", ec))
        nodes.push_back(entry.path());
    std::sort(nodes.begin(), nodes.end());

    std::map<std::string, Interfaces> boards;  // keyed by USB device
    for (const auto& node : nodes) {
        const fs::path dev = node / "device";
        auto uevent = read_file(dev / "uevent");
        auto desc = read_file(dev / "report_descriptor");
        if (!uevent || !desc || !is_ak650(*uevent)) continue;

        // hidraw -> hid device -> usb interface -> usb device
        const fs::path usb = fs::canonical(dev / ".." / "..", ec);
        if (ec) continue;
        const std::string name = "/dev/" + node.filename().string();
        Interfaces& board = boards[usb.string()];
        if (same(*desc, kControlDesc)) {
            board.control = name;
        } else if (same(*desc, kDataDesc)) {
            board.data = name;
        } else if (same_usage_page(*desc, kControlDesc) || same_usage_page(*desc, kDataDesc)) {
            throw Error(name + " is an AK650 vendor interface with an unfamiliar descriptor. "
                               "Not touching it.");
        }
    }

    std::vector<Interfaces> found;
    for (const auto& [usb, board] : boards)
        if (!board.control.empty() && !board.data.empty()) found.push_back(board);
    if (found.empty())
        throw Error("No AK650 found. Plug it in over USB (the 2.4G dongle and Bluetooth "
                    "don't expose the screen interfaces).");
    if (found.size() > 1) throw Error("More than one AK650 connected; unplug the extras.");
    return found.front();
}

const char* udev_hint() { return kUdevHint; }

Keyboard::Keyboard(bool verbose) : paths_(find_interfaces()), verbose_(verbose) {
    ctrl_ = ::open(paths_.control.c_str(), O_RDWR | O_CLOEXEC);
    if (ctrl_ >= 0) data_ = ::open(paths_.data.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (ctrl_ < 0 || data_ < 0) {
        const int err = errno;
        if (ctrl_ >= 0) ::close(ctrl_);
        if (err == EACCES)
            throw Error(std::format("No permission on {} / {}. {}", paths_.control, paths_.data,
                                    kUdevHint));
        throw Error(std::format("Can't open the keyboard: {}", std::strerror(err)));
    }

    // One command at a time. The plug-in clock sync and an upload would otherwise
    // interleave their packets. The lock is on the device node, so it goes away
    // with the process or the keyboard, and there's no lock file to clean up.
    if (::flock(ctrl_, LOCK_EX | LOCK_NB) != 0) {
        std::fprintf(stderr, "Another ak650 command is using the keyboard, waiting...\n");
        while (::flock(ctrl_, LOCK_EX) != 0) {
            if (errno == EINTR) continue;
            const int err = errno;
            ::close(ctrl_);
            ::close(data_);
            throw Error(std::format("Can't lock {}: {}", paths_.control, std::strerror(err)));
        }
    }
}

Keyboard::~Keyboard() {
    ::close(ctrl_);
    ::close(data_);
}

Packet Keyboard::read_feature() {
    std::array<std::uint8_t, 65> buf{};  // buf[0] is the report ID, 0 on this board
    if (::ioctl(ctrl_, HIDIOCGFEATURE(buf.size()), buf.data()) < 0)
        throw Error(std::format("GET_FEATURE failed: {}", std::strerror(errno)));
    Packet reply;
    std::copy(buf.begin() + 1, buf.end(), reply.begin());
    return reply;
}

// SET_FEATURE a 64-byte packet, then GET_FEATURE the board's reply.
Packet Keyboard::exchange(const Packet& packet) {
    std::this_thread::sleep_for(kCmdDelay);
    std::array<std::uint8_t, 65> buf{};
    std::copy(packet.begin(), packet.end(), buf.begin() + 1);
    if (::ioctl(ctrl_, HIDIOCSFEATURE(buf.size()), buf.data()) < 0)
        throw Error(std::format("SET_FEATURE failed: {}", std::strerror(errno)));
    std::this_thread::sleep_for(kReplyDelay);
    const Packet reply = read_feature();
    if (verbose_)
        std::fprintf(stderr, "  > %s  < %s\n", hex_dump(std::span(packet).first(10)).c_str(),
                     hex_dump(std::span(reply).first(10)).c_str());
    return reply;
}

void Keyboard::command(std::uint8_t cmd, std::uint8_t sub, std::uint16_t value) {
    Packet packet{};
    packet[0] = 0x04;
    packet[1] = cmd;
    packet[2] = sub;
    packet[8] = static_cast<std::uint8_t>(value & 0xFF);
    packet[9] = static_cast<std::uint8_t>(value >> 8);

    Packet reply = exchange(packet);
    if (cmd == Start) {
        // A board still busy with a previous write answers all zeroes or status FF.
        auto busy = [](const Packet& r) {
            const bool zeroes = std::all_of(r.begin(), r.end(), [](std::uint8_t b) { return b == 0; });
            return zeroes || (r[0] == 0x04 && r[1] == Start && r[2] == 0x00 && r[3] == 0xFF);
        };
        for (int tries = 1; tries < 10 && busy(reply); ++tries) {
            std::this_thread::sleep_for(300ms);
            reply = exchange(packet);
        }
    }
    if (reply[0] != 0x04 || reply[1] != cmd || reply[3] != 0x01)
        throw Error(std::format("Keyboard refused command {:02x} (reply {})", cmd,
                                hex_dump(std::span(reply).first(9))));
}

void Keyboard::drain() {
    pollfd waiting{data_, POLLIN, 0};
    Packet stale;
    while (::poll(&waiting, 1, 0) > 0 && ::read(data_, stale.data(), stale.size()) > 0) {
    }
}

std::optional<Packet> Keyboard::send_chunk(std::span<const std::uint8_t> chunk) {
    // hidraw takes the first byte as the report ID and strips it when it's 0x00,
    // which this board uses. Without the extra byte, any chunk that starts with a
    // black pixel arrives one byte short.
    std::array<std::uint8_t, kChunk + 1> buf;
    buf[0] = 0x00;
    std::copy(chunk.begin(), chunk.end(), buf.begin() + 1);
    if (::write(data_, buf.data(), buf.size()) != static_cast<ssize_t>(buf.size()))
        throw Error("Short write to the screen interface");

    pollfd ack_ready{data_, POLLIN, 0};
    int ready;
    do {
        ready = ::poll(&ack_ready, 1, 1000);
    } while (ready < 0 && errno == EINTR);
    Packet ack{};
    if (ready <= 0 || ::read(data_, ack.data(), ack.size()) <= 0) return std::nullopt;
    return ack;
}

void Keyboard::upload(std::span<const std::uint8_t> payload, int slot) {
    const std::size_t chunks = payload.size() / kChunk;
    bool progress_line_open = false;
    drain();
    command(Start);
    try {
        command(ImageCfg, static_cast<std::uint8_t>(slot), static_cast<std::uint16_t>(chunks));
        for (std::size_t i = 0; i < chunks; ++i) {
            if (g_interrupted) throw Error("Interrupted; stopped without saving");
            auto ack = send_chunk(payload.subspan(i * kChunk, kChunk));
            if (!ack)
                throw Error(std::format("No ack for chunk {}/{}; stopped without saving", i + 1, chunks));
            if (i == 0 && verbose_)
                std::fprintf(stderr, "  first ack: %s\n", hex_dump(std::span(*ack).first(8)).c_str());
            std::printf("\r  sending %zu/%zu", i + 1, chunks);
            std::fflush(stdout);
            progress_line_open = true;
        }
        std::printf("\n");
        progress_line_open = false;
        // No FINISH after a good upload: the vendor driver doesn't send one, and on
        // the AKS075 it restarts the built-in animation.
        command(Commit);
    } catch (...) {
        if (progress_line_open) std::printf("\n");
        try {
            command(Finish, 0, 1);
        } catch (const Error&) {
        }
        throw;
    }
}

void Keyboard::sync_clock(const std::tm& now) {
    Packet data{};
    data[1] = 0x01;
    data[2] = 0x5A;
    data[3] = static_cast<std::uint8_t>((now.tm_year + 1900) % 100);
    data[4] = static_cast<std::uint8_t>(now.tm_mon + 1);
    data[5] = static_cast<std::uint8_t>(now.tm_mday);
    data[6] = static_cast<std::uint8_t>(now.tm_hour);
    data[7] = static_cast<std::uint8_t>(now.tm_min);
    data[8] = static_cast<std::uint8_t>(now.tm_sec);
    data[10] = 0x04;
    data[62] = 0xAA;
    data[63] = 0x55;

    command(Start, 0, 1);
    try {
        command(Clock, 0, 1);
        exchange(data);
        command(Commit);
    } catch (...) {
        try {
            command(Finish, 0, 1);
        } catch (const Error&) {
        }
        throw;
    }
    command(Finish, 0, 1);
}

}  // namespace ak650
