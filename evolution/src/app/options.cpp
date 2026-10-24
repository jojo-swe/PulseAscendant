#include "app/options.hpp"

#include "gfx/paths.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace evo {

namespace {

constexpr int kMinSize = 16; // tiny frames keep the headless tests fast
constexpr int kMaxSize = 8192;
constexpr int kMinFps = 1;
constexpr int kMaxFps = 240;
constexpr int kMaxTime = 86400; // a day, in seconds: far past any timeline
constexpr double kMaxAudioOffsetMs = 500.0;

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

// --audio-offset: a signed number of milliseconds within +-kMaxAudioOffsetMs.
double parseAudioOffset(const std::string& flag, const std::string& text) {
    double value = 0.0;
    const char* end = text.data() + text.size();
    auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc() || ptr != end || !std::isfinite(value)) {
        throw std::runtime_error(flag + " expects a number of milliseconds, got '" + text + "'");
    }
    if (std::abs(value) > kMaxAudioOffsetMs) {
        throw std::runtime_error(flag + " must be between -500 and 500 milliseconds, got " + text);
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
            const std::string& text = nextValue();
            options.time = parseSeconds(flag, text);
            if (options.time > kMaxTime) {
                throw std::runtime_error(flag + " must be at most " + std::to_string(kMaxTime) +
                                         " seconds, got " + text);
            }
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
            options.captureDir = gfx::pathFromUtf8(path);
        } else if (flag == "--music") {
            const std::string& path = nextValue();
            if (path.empty()) {
                throw std::runtime_error(flag + " needs a non-empty path");
            }
            options.music = gfx::pathFromUtf8(path);
        } else if (flag == "--no-audio") {
            options.noAudio = true;
        } else if (flag == "--mute") {
            options.mute = true;
        } else if (flag == "--audio-offset") {
            options.audioOffsetMs = parseAudioOffset(flag, nextValue());
        } else if (flag == "--no-crt") {
            options.crt = false;
        } else if (flag == "--no-hud") {
            options.hud = false;
        } else if (flag == "--show-info") {
            options.showInfo = true;
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
    // A stream has no Esc to end it, so "run until Esc" would write nothing.
    if (options.renderStream && options.duration <= 0.0) {
        throw std::runtime_error("--render-stream needs --duration greater than 0");
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
           "  --time S             Start time on the timeline in seconds, 0..86400 (default 0)\n"
           "  --duration S         Seconds to run; 0 runs until Esc (default 0). --render-stream\n"
           "                       needs a duration greater than 0\n"
           "  --headless           Render without opening a window\n"
           "  --render-stream      Write raw RGB frames to stdout (implies --headless)\n"
           "  --capture-dir PATH   Write one PNG frame, at --time, to PATH (implies --headless)\n"
           "  --music PATH         Music WAV to play (default: assets/music/show.wav beside the\n"
           "                       executable); a WAV of another length than the show is refused\n"
           "  --no-audio           Run without audio, on the system clock (SILENT MODE)\n"
           "  --mute               Start muted (M toggles); the music still keeps the time\n"
           "  --audio-offset MS    Calibrate A/V sync: milliseconds added to the music time the\n"
           "                       picture follows, -500..500 (default 0); raise it if the picture\n"
           "                       lags the sound\n"
           "  --no-crt             Show the 1993 picture without the CRT look (C toggles)\n"
           "  --no-hud             Hide the chapter title and the epoch cards (H toggles)\n"
           "  --show-info          Print the show's chapters and music as JSON and exit\n"
           "  --validation         Enable the Vulkan validation layers\n"
           "  --device-info        Print information about the selected GPU and exit\n"
           "  --fullscreen         Start the window in fullscreen\n"
           "  -h, --help           Show this help and exit\n"
           "\n"
           "Keys (window): Left/Right previous/next chapter, keys 1-9 and 0 jump to chapters 1-10,\n"
           "C CRT look, H HUD, M mute, F11 fullscreen, Esc quit\n"
           "\n"
           "Environment (diagnostic): EVO_FORCE_ERA=0 or 1 renders a --capture-dir frame wholly in\n"
           "1993 or 2026 (no wipe split, no cut flash); other values are ignored\n";
}

} // namespace evo
