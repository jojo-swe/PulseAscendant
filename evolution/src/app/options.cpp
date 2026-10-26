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
constexpr int kMaxTime = 86400;                // a day, in seconds: far past any timeline
constexpr int kMaxFirstFrame = kMaxTime * 240; // a day at the highest frame rate
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

// --detail: a fraction in (0, 1].
float parseDetail(const std::string& flag, const std::string& text) {
    double value = 0.0;
    const char* end = text.data() + text.size();
    auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc() || ptr != end || !std::isfinite(value)) {
        throw std::runtime_error(flag + " expects a number, got '" + text + "'");
    }
    if (!(value > 0.0 && value <= 1.0)) {
        throw std::runtime_error(flag + " must be greater than 0 and at most 1, got " + text);
    }
    return static_cast<float>(value);
}

// --trace-scale: a fraction in [kMinTraceScale, 1].
float parseTraceScale(const std::string& flag, const std::string& text) {
    double value = 0.0;
    const char* end = text.data() + text.size();
    auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (error != std::errc() || ptr != end || !std::isfinite(value)) {
        throw std::runtime_error(flag + " expects a number, got '" + text + "'");
    }
    if (!(value >= kMinTraceScale && value <= 1.0)) {
        throw std::runtime_error(flag + " must be between 0.25 and 1, got " + text);
    }
    return static_cast<float>(value);
}

// --chapters: comma-separated non-negative chapter indices (the bench checks them against the show).
std::vector<int> parseChapters(const std::string& flag, const std::string& text) {
    std::vector<int> chapters;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = text.find(',', start);
        const std::string item =
            text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        int value = -1;
        const char* end = item.data() + item.size();
        auto [ptr, error] = std::from_chars(item.data(), end, value);
        if (item.empty() || error != std::errc() || ptr != end || value < 0) {
            throw std::runtime_error(flag + " expects a comma-separated list of chapter indices, got '" +
                                     text + "'");
        }
        chapters.push_back(value);
        if (comma == std::string::npos)
            return chapters;
        start = comma + 1;
    }
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
    bool benchOnly = false; // --chapters or --bench-frames given

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
        } else if (flag == "--spp") {
            options.spp = parseInt(flag, nextValue(), kMinSpp, kMaxSpp);
        } else if (flag == "--detail") {
            options.detail = parseDetail(flag, nextValue());
        } else if (flag == "--trace-scale") {
            const std::string& value = nextValue();
            options.autoTraceScale = value == "auto";
            options.traceScale.reset();
            if (!options.autoTraceScale)
                options.traceScale = parseTraceScale(flag, value);
        } else if (flag == "--no-fast-mode") {
            options.fastMode = false;
        } else if (flag == "--bench") {
            options.bench = true;
        } else if (flag == "--bench-capture") {
            options.benchCapture = nextValue();
            benchOnly = true;
        } else if (flag == "--bench-counters") {
            options.benchCounters = true;
            benchOnly = true;
        } else if (flag == "--bench-frames") {
            options.benchFrames = parseInt(flag, nextValue(), 1, kMaxBenchFrames);
            benchOnly = true;
        } else if (flag == "--chapters") {
            options.chapters = parseChapters(flag, nextValue());
            benchOnly = true;
        } else if (flag == "--time") {
            const std::string& text = nextValue();
            options.time = parseSeconds(flag, text);
            if (options.time > kMaxTime) {
                throw std::runtime_error(flag + " must be at most " + std::to_string(kMaxTime) +
                                         " seconds, got " + text);
            }
        } else if (flag == "--duration") {
            options.duration = parseSeconds(flag, nextValue());
        } else if (flag == "--first-frame") {
            options.firstFrame = parseInt(flag, nextValue(), 0, kMaxFirstFrame);
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
        } else if (flag == "--secret-part") {
            options.secretPart = true;
        } else if (flag.rfind("--secret-part=", 0) == 0) {
            options.secretPart = true;
            options.secretAnswer = flag.substr(std::string("--secret-part=").size());
        } else if (flag == "--help" || flag == "-h") {
            options.help = true;
        } else {
            throw std::runtime_error("unknown option " + flag);
        }
    }

    if (options.renderStream && !options.captureDir.empty()) {
        throw std::runtime_error("--render-stream and --capture-dir cannot be combined");
    }
    const bool exporting = options.renderStream || !options.captureDir.empty();
    if (options.bench && exporting) {
        throw std::runtime_error("--bench cannot be combined with an export");
    }
    if (options.firstFrame != 0 && !options.renderStream) {
        throw std::runtime_error("--first-frame needs --render-stream");
    }
    if (options.secretPart && (options.bench || !options.captureDir.empty())) {
        throw std::runtime_error("--secret-part runs in the window or with --render-stream");
    }
    if (benchOnly && !options.bench) {
        throw std::runtime_error("--chapters and --bench-frames need --bench");
    }
    // Exports trace at the output size, so a window frame after a reset at scale 1
    // and an export frame agree.
    if ((options.traceScale || options.autoTraceScale) && exporting) {
        throw std::runtime_error(
            "--trace-scale is for the window and --bench: the exports trace at the output size");
    }
    // The exports never take the window's shortcuts.
    if (!options.fastMode && exporting) {
        throw std::runtime_error(
            "--no-fast-mode is for the window and --bench: the exports never use fast mode");
    }
    // A stream has no Esc to end it, so "run until Esc" would write nothing.
    if (options.renderStream && options.duration <= 0.0) {
        throw std::runtime_error("--render-stream needs --duration greater than 0");
    }
    // Streaming, capturing and benchmarking never open a window.
    if (exporting || options.bench) {
        options.headless = true;
    }

    return options;
}

int samplesPerPixel(const Options& options) {
    const bool exporting = options.renderStream || !options.captureDir.empty();
    return options.spp.value_or(exporting ? kExportSpp : kWindowSpp);
}

float traceScaleFor(const Options& options) {
    const bool exporting = options.renderStream || !options.captureDir.empty();
    return exporting ? 1.f : options.traceScale.value_or(kWindowTraceScale);
}

bool adaptiveTraceScale(const Options& options) {
    const bool exporting = options.renderStream || !options.captureDir.empty();
    if (exporting || options.traceScale)
        return false;
    return true; // the window and --bench adapt unless given a fixed scale
}

std::string helpText() {
    return "Usage: Evolution [options]\n"
           "\n"
           "Options:\n"
           "  --width N            Render width in pixels, 16..8192 (default 1280)\n"
           "  --height N           Render height in pixels, 16..8192 (default 720)\n"
           "  --fps N              Frames per second for headless output, 1..240 (default 60)\n"
           "  --time S             Start time on the timeline in seconds, 0..86400 (default 0)\n"
           "  --spp N              Path-traced samples per pixel per frame, 1..4096 (default 1 in\n"
           "                       the window, which accumulates them over frames; 64 for\n"
           "                       --capture-dir and --render-stream, where every frame stands alone)\n"
           "  --detail F           Detail of the 2026 scenes, 0 < F <= 1 (default 1): particle,\n"
           "                       splat, voxel and photon counts scale with it (for quick tests)\n"
           "  --trace-scale F      Path trace at F x the window size, 0.25..1, and upscale in time;\n"
           "                       the window and --bench only (the exports always trace at the\n"
           "                       output size). 'auto' (the default) traces each chapter at its\n"
           "                       scale from a table, 0.3..0.67, to hold 30 fps; the window also\n"
           "                       adapts it to the GPU's frame time (docs/PERFORMANCE.md)\n"
           "  --no-fast-mode       Trace the window and --bench as the exports do: four in-scatter\n"
           "                       points in media, photons seeded per frame, the generic pipeline,\n"
           "                       full BLAS builds, every shadow ray traced and walked through\n"
           "                       panes, roulette from bounce 3 only (by default the window\n"
           "                       traces in fast mode; docs/PERFORMANCE.md)\n"
           "  --duration S         Seconds to run; 0 runs until Esc (default 0). --render-stream\n"
           "                       needs a duration greater than 0\n"
           "  --headless           Render without opening a window\n"
           "  --render-stream      Write raw RGB frames to stdout (implies --headless)\n"
           "  --first-frame N      With --render-stream: start at frame N, at --time + N / fps\n"
           "  --capture-dir PATH   Write one PNG frame, at --time, to PATH (implies --headless)\n"
           "  --bench              Benchmark the window's rendering headless: per chapter, render\n"
           "                       --bench-frames frames from 20 % into it (after 2 warm-up frames)\n"
           "                       and print one JSON line with the GPU time of every pass\n"
           "  --bench-frames N     Frames per chapter for --bench, 1..100000 (default 120)\n"
           "  --bench-capture DIR  With --bench: also write each chapter's last timed frame to\n"
           "                       DIR/bench_<chapter>.png (to compare the look of two runs)\n"
           "  --bench-counters     With --bench: trace with the counting path tracer and add its work\n"
           "                       per pixel per frame (rays by kind, NEE, medium steps) and the\n"
           "                       frame's scene statistics to each line (slower; the same picture)\n"
           "  --chapters LIST      Chapters for --bench, comma-separated indices as --show-info\n"
           "                       numbers them, e.g. 11,14 (default: all)\n"
           "  --music PATH         Music WAV to play (default: assets/music/show.wav beside the\n"
           "                       executable); a WAV of another length than the show is refused\n"
           "  --no-audio           Run without audio, on the system clock (SILENT MODE)\n"
           "  --mute               Start muted (M toggles); the music still keeps the time\n"
           "  --audio-offset MS    Calibrate A/V sync: milliseconds added to the music time the\n"
           "                       picture follows, -500..500 (default 0); raise it if the picture\n"
           "                       lags the sound\n"
           "  --no-crt             Show the 1993 picture without the CRT look (C toggles)\n"
           "  --no-hud             Hide the epoch cards and the window's chapter title (H toggles)\n"
           "  --show-info          Print the show's chapters and music as JSON and exit\n"
           "  --validation         Enable the Vulkan validation layers\n"
           "  --device-info        Print information about the selected GPU and exit\n"
           "  --fullscreen         Start the window in fullscreen\n"
           "  -h, --help           Show this help and exit\n"
           "\n"
           "Keys (window): Left/Right previous/next chapter, keys 1-9 and 0 jump to chapters 1-10,\n"
           "C CRT look, H HUD, M mute, F11 fullscreen, Esc quit\n"
           "\n"
           "Environment (diagnostic): EVO_FORCE_ERA=0 or 1 renders --capture-dir and --render-stream\n"
           "frames wholly in 1993 or 2026 (no wipe split, no cut flash); other values are ignored\n";
}

} // namespace evo
