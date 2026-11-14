#include <unistd.h>

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "device.hpp"
#include "image.hpp"

using namespace ak650;

namespace {

const char* const kUsage =
    "ak650: Linux tool for the Ajazz AK650 keyboard\n"
    "\n"
    "usage:\n"
    "  ak650 probe                  find the keyboard and check access, sends nothing\n"
    "  ak650 screen upload FILE     send an image or GIF to the screen\n"
    "  ak650 screen preview FILE    write a GIF of what upload would send, no keyboard needed\n"
    "  ak650 time                   set the keyboard clock to the system time\n"
    "\n"
    "options:\n"
    "  -v, --verbose           print every packet exchange\n"
    "  --fit crop|pad|stretch  crop to a square (default), pad with black, or stretch\n"
    "  --max-frames N          drop frames evenly above N (default 30, max 255;\n"
    "                          capacity past 30 is untested on this board)\n"
    "  --slot 1|2|3            image slot for upload (default 1, what the vendor driver uses)\n"
    "  -o, --output PATH       where preview writes its GIF (default <name>-ak650.gif)\n"
    "  --raw PATH              preview also writes the exact bytes upload would send\n"
    "\n"
    "FILE is a GIF, PNG, JPG, BMP or TGA. Wired USB only.\n";

struct UsageError {
    std::string message;
};

struct Options {
    std::vector<std::string> words;  // the command and its file
    bool verbose = false;
    bool help = false;
    Fit fit = Fit::Crop;
    int max_frames = 30;
    int slot = 1;
    std::string output;
    std::string raw;
};

int to_int(const std::string& text, const char* what, int lo, int hi) {
    int value = 0;
    auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || value < lo || value > hi)
        throw UsageError{std::format("{} must be {}..{}", what, lo, hi)};
    return value;
}

Options parse(int argc, char** argv) {
    Options opt;
    bool options_done = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw UsageError{arg + " needs a value"};
            return argv[++i];
        };
        if (options_done || arg.empty() || arg[0] != '-' || arg == "-") {
            opt.words.push_back(arg);
        } else if (arg == "--") {
            options_done = true;
        } else if (arg == "-v" || arg == "--verbose") {
            opt.verbose = true;
        } else if (arg == "-h" || arg == "--help") {
            opt.help = true;
        } else if (arg == "--fit") {
            const std::string fit = value();
            if (fit == "crop") opt.fit = Fit::Crop;
            else if (fit == "pad") opt.fit = Fit::Pad;
            else if (fit == "stretch") opt.fit = Fit::Stretch;
            else throw UsageError{"--fit must be crop, pad or stretch"};
        } else if (arg == "--max-frames") {
            opt.max_frames = to_int(value(), "--max-frames", 1, 255);
        } else if (arg == "--slot") {
            opt.slot = to_int(value(), "--slot", 1, 3);
        } else if (arg == "-o" || arg == "--output") {
            opt.output = value();
        } else if (arg == "--raw") {
            opt.raw = value();
        } else {
            throw UsageError{"unknown option " + arg};
        }
    }
    return opt;
}

std::string hex_dump(const Packet& packet, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) out += std::format("{}{:02x}", i ? " " : "", packet[i]);
    return out;
}

int cmd_probe(const Options& opt) {
    const Interfaces found = find_interfaces();
    std::printf("AK650 found\n  control (0xFF13): %s\n  data    (0xFF68): %s\n", found.control.c_str(),
                found.data.c_str());
    if (::access(found.control.c_str(), R_OK | W_OK) != 0 || ::access(found.data.c_str(), R_OK | W_OK) != 0) {
        std::printf("\nNo read/write access yet. %s\n", udev_hint());
        return 1;
    }
    Keyboard keyboard(opt.verbose);
    std::printf("  feature read ok: %s\n", hex_dump(keyboard.read_feature(), 10).c_str());
    std::printf("Ready to upload.\n");
    return 0;
}

int cmd_preview(const Options& opt, const std::string& file) {
    const auto frames = load_frames(file, opt.fit, opt.max_frames);
    const std::string out =
        opt.output.empty() ? std::filesystem::path(file).stem().string() + "-ak650.gif" : opt.output;
    save_preview(frames, out);
    if (frames.size() == 1) {
        std::printf("1 frame, still image -> %s\n", out.c_str());
    } else {
        int loop_ms = 0;
        for (const Frame& frame : frames) loop_ms += device_delay(frame.delay_ms) * 2;
        std::printf("%zu frames, %d ms per loop -> %s\n", frames.size(), loop_ms, out.c_str());
    }
    if (!opt.raw.empty()) {
        const auto payload = encode(frames);
        std::ofstream raw(opt.raw, std::ios::binary);
        raw.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        if (!raw) throw Error("Can't write " + opt.raw);
        std::printf("%zu payload bytes -> %s\n", payload.size(), opt.raw.c_str());
    }
    return 0;
}

int cmd_upload(const Options& opt, const std::string& file) {
    const auto frames = load_frames(file, opt.fit, opt.max_frames);
    const auto payload = encode(frames);
    std::printf("%zu frame(s), %zu chunks of 4 KiB, slot %d\n", frames.size(), payload.size() / kChunk,
                opt.slot);
    Keyboard keyboard(opt.verbose);

    // From here Ctrl-C only sets a flag, so upload() can end the transfer cleanly.
    struct sigaction action{};
    action.sa_handler = [](int) { g_interrupted = true; };
    sigaction(SIGINT, &action, nullptr);

    const auto started = std::chrono::steady_clock::now();
    keyboard.upload(payload, opt.slot);
    const std::chrono::duration<double> took = std::chrono::steady_clock::now() - started;
    std::printf("Done in %.1fs. If the screen still shows the clock or the stock animation, "
                "switch screen pages with the knob.\n", took.count());
    return 0;
}

int cmd_time(const Options& opt) {
    Keyboard keyboard(opt.verbose);
    const std::time_t t = std::time(nullptr);
    std::tm now{};
    localtime_r(&t, &now);
    keyboard.sync_clock(now);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &now);
    std::printf("Clock set to %s\n", text);
    return 0;
}

int run(const Options& opt) {
    const auto& w = opt.words;
    if (w.size() == 1 && w[0] == "probe") return cmd_probe(opt);
    if (w.size() == 1 && w[0] == "time") return cmd_time(opt);
    if (w.size() == 3 && w[0] == "screen" && w[1] == "upload") return cmd_upload(opt, w[2]);
    if (w.size() == 3 && w[0] == "screen" && w[1] == "preview") return cmd_preview(opt, w[2]);
    if (!w.empty() && w[0] == "screen") throw UsageError{"expected: ak650 screen upload|preview FILE"};
    throw UsageError{w.empty() ? "no command given" : "unknown command " + w[0]};
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options opt = parse(argc, argv);
        if (opt.help) {
            std::fputs(kUsage, stdout);
            return 0;
        }
        return run(opt);
    } catch (const UsageError& e) {
        std::fprintf(stderr, "ak650: %s\n\n%s", e.message.c_str(), kUsage);
        return 2;
    } catch (const Error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return g_interrupted ? 130 : 1;
    }
}
