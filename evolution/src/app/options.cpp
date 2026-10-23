#include "app/options.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace evo {

namespace {

constexpr int kMinSize = 16; // tiny frames keep the headless tests fast
constexpr int kMaxSize = 8192;
constexpr int kMinFps = 1;
constexpr int kMaxFps = 240;

int parseInt(const std::string& flag, const std::string& text, int min, int max) {
    int value = 0;
    const char* end = text.data() + text.size();
    auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc() || ptr != end) {
        throw std::runtime_error(flag + " expects an integer, got '" + text + "'");
    }
    if (value < min || value > max) {
        throw std::runtime_error(flag + " must be between " + std::to_string(min) + " and " +
                                 std::to_string(max) + ", got " + text);
    }
    return value;
}

double parseSeconds(const std::string& flag, const std::string& text) {
    double value = 0.0;
    const char* end = text.data() + text.size();
    auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc() || ptr != end || !std::isfinite(value)) {
        throw std::runtime_error(flag + " expects a number of seconds, got '" + text + "'");
    }
    if (value < 0.0) {
        throw std::runtime_error(flag + " must not be negative, got " + text);
    }
    return value;
}

} // namespace

Options parseOptions(const std::vector<std::string>& args) {
    Options options;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& flag = args[i];

        auto nextValue = [&]() -> const std::string& {
            if (i + 1 >= args.size()) {
                throw std::runtime_error(flag + " needs a value");
            }
            return args[++i];
        };

        if (flag == "--width") {
            options.width = parseInt(flag, nextValue(), kMinSize, kMaxSize);
        } else if (flag == "--height") {
            options.height = parseInt(flag, nextValue(), kMinSize, kMaxSize);
        } else if (flag == "--fps") {
            options.fps = parseInt(flag, nextValue(), kMinFps, kMaxFps);
        } else if (flag == "--time") {
            options.time = parseSeconds(flag, nextValue());
        } else if (flag == "--duration") {
            options.duration = parseSeconds(flag, nextValue());
        } else if (flag == "--headless") {
            options.headless = true;
        } else if (flag == "--render-stream") {
            options.renderStream = true;
        } else if (flag == "--capture-dir") {
            const std::string& path = nextValue();
            if (path.empty()) {
                throw std::runtime_error(flag + " needs a non-empty path");
            }
            options.captureDir = path;
        } else if (flag == "--validation") {
            options.validation = true;
        } else if (flag == "--device-info") {
            options.deviceInfo = true;
        } else if (flag == "--fullscreen") {
            options.fullscreen = true;
        } else if (flag == "--help" || flag == "-h") {
            options.help = true;
        } else {
            throw std::runtime_error("unknown option " + flag);
        }
    }

    if (options.renderStream && !options.captureDir.empty()) {
        throw std::runtime_error("--render-stream and --capture-dir cannot be combined");
    }
    // Streaming and capturing never open a window.
    if (options.renderStream || !options.captureDir.empty()) {
        options.headless = true;
    }

    return options;
}

std::string helpText() {
    return "Usage: Evolution [options]\n"
           "\n"
           "Options:\n"
           "  --width N            Render width in pixels, 16..8192 (default 1280)\n"
           "  --height N           Render height in pixels, 16..8192 (default 720)\n"
           "  --fps N              Frames per second for headless output, 1..240 (default 60)\n"
           "  --time S             Start time on the timeline in seconds (default 0)\n"
           "  --duration S         Seconds to run; 0 runs until Esc (default 0)\n"
           "  --headless           Render without opening a window\n"
           "  --render-stream      Write raw RGB frames to stdout (implies --headless)\n"
           "  --capture-dir PATH   Write PNG frames to PATH (implies --headless)\n"
           "  --validation         Enable the Vulkan validation layers\n"
           "  --device-info        Print information about the selected GPU and exit\n"
           "  --fullscreen         Start the window in fullscreen\n"
           "  -h, --help           Show this help and exit\n";
}

} // namespace evo
