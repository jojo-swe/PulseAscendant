#include "app/options.hpp"
#include "gfx/paths.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

// Returns the exception message, or an empty string when parseOptions did not throw.
std::string parseError(const std::vector<std::string>& args) {
    try {
        evo::parseOptions(args);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return {};
}

} // namespace

int main() {
    try {
        const evo::Options defaults = evo::parseOptions({});
        CHECK(defaults.width == 1280);
        CHECK(defaults.height == 720);
        CHECK(defaults.fps == 60);
        CHECK(!defaults.headless);
        CHECK(defaults.captureDir.empty());

        const evo::Options uhd = evo::parseOptions({"--width", "3840", "--height", "2160"});
        CHECK(uhd.width == 3840);
        CHECK(uhd.height == 2160);

        const evo::Options stream = evo::parseOptions({"--render-stream", "--duration", "1"});
        CHECK(stream.renderStream);
        CHECK(stream.headless);
        // "Until Esc" means nothing for a stream: it would write no frames at all.
        CHECK(parseError({"--render-stream"}) == "--render-stream needs --duration greater than 0");
        CHECK(parseError({"--render-stream", "--duration", "0"}) ==
              "--render-stream needs --duration greater than 0");

        const evo::Options capture = evo::parseOptions({"--capture-dir", "out/x"});
        CHECK(capture.captureDir == "out/x");
        CHECK(capture.headless);

        CHECK(!parseError({"--fps", "0"}).empty());
        // Tiny frames keep the headless tests fast: 16 is the floor, 8192 the ceiling.
        const evo::Options tiny = evo::parseOptions({"--width", "32", "--height", "18"});
        CHECK(tiny.width == 32);
        CHECK(tiny.height == 18);
        CHECK(evo::parseOptions({"--width", "16", "--height", "16"}).width == 16);
        CHECK(!parseError({"--width", "8"}).empty());
        CHECK(!parseError({"--height", "15"}).empty());
        CHECK(!parseError({"--width", "8193"}).empty());
        CHECK(parseError({"--bogus"}).find("--bogus") != std::string::npos);
        CHECK(!parseError({"--render-stream", "--capture-dir", "x"}).empty());

        // --time is capped at a day.
        CHECK(evo::parseOptions({"--time", "86400"}).time == 86400.0);
        CHECK(parseError({"--time", "86400.5"}).find("at most 86400") != std::string::npos);

        // Audio: on by default with the show's own music beside the executable (empty path).
        CHECK(!defaults.noAudio);
        CHECK(!defaults.mute);
        CHECK(!defaults.showInfo);
        CHECK(defaults.music.empty());
        CHECK(evo::parseOptions({"--no-audio"}).noAudio);
        CHECK(evo::parseOptions({"--mute"}).mute);
        CHECK(evo::parseOptions({"--show-info"}).showInfo);
        CHECK(evo::parseOptions({"--music", "x.wav"}).music == "x.wav");
        CHECK(parseError({"--music"}) == "--music needs a value");
        CHECK(parseError({"--music", ""}) == "--music needs a non-empty path");

        // Arguments are UTF-8 on every platform (main.cpp converts the Windows command line), and paths are
        // made from them as UTF-8, so a non-ASCII name survives: "Jörgen/låt.wav".
        const std::string utf8Name = "J\xC3\xB6rgen/l\xC3\xA5t.wav";
        const std::u8string expected = u8"J\u00F6rgen/l\u00E5t.wav";
        const evo::Options nonAscii = evo::parseOptions({"--music", utf8Name, "--capture-dir", utf8Name});
        CHECK(nonAscii.music.u8string() == expected);
        CHECK(nonAscii.captureDir.u8string() == expected);
        CHECK(nonAscii.music == evo::gfx::pathFromUtf8(utf8Name));
        CHECK(evo::gfx::pathToUtf8(nonAscii.music) == utf8Name);

        // --audio-offset: signed milliseconds within +-500, for calibrating A/V sync.
        CHECK(defaults.audioOffsetMs == 0.0);
        CHECK(evo::parseOptions({"--audio-offset", "120"}).audioOffsetMs == 120.0);
        CHECK(evo::parseOptions({"--audio-offset", "-37.5"}).audioOffsetMs == -37.5);
        CHECK(evo::parseOptions({"--audio-offset", "500"}).audioOffsetMs == 500.0);
        CHECK(evo::parseOptions({"--audio-offset", "-500"}).audioOffsetMs == -500.0);
        CHECK(parseError({"--audio-offset", "500.5"}) ==
              "--audio-offset must be between -500 and 500 milliseconds, got 500.5");
        CHECK(!parseError({"--audio-offset", "-501"}).empty());
        CHECK(parseError({"--audio-offset", "soon"}) ==
              "--audio-offset expects a number of milliseconds, got 'soon'");
        CHECK(!parseError({"--audio-offset", "nan"}).empty());
        CHECK(parseError({"--audio-offset"}) == "--audio-offset needs a value");

        // --no-crt: the 1993 picture without the CRT look; on by default.
        CHECK(defaults.crt);
        CHECK(!evo::parseOptions({"--no-crt"}).crt);
        CHECK(!evo::parseOptions({"--capture-dir", "out", "--no-crt"}).crt);

        // --no-hud: no epoch cards (nor the window's chapter title); shown by default.
        CHECK(defaults.hud);
        CHECK(!evo::parseOptions({"--no-hud"}).hud);
        CHECK(!evo::parseOptions({"--render-stream", "--duration", "1", "--no-hud"}).hud);
        CHECK(evo::parseOptions({"--no-crt"}).hud);

        // --spp: samples per pixel per frame, 1..4096. Unset by default, which means 1 in the window (the
        // history accumulates more) and 64 in the exports (each frame stands alone).
        CHECK(!defaults.spp.has_value());
        CHECK(evo::samplesPerPixel(defaults) == 1);
        CHECK(evo::samplesPerPixel(evo::parseOptions({"--capture-dir", "out"})) == 64);
        CHECK(evo::samplesPerPixel(evo::parseOptions({"--render-stream", "--duration", "1"})) == 64);
        const evo::Options spp16 = evo::parseOptions({"--spp", "16"});
        CHECK(spp16.spp == 16);
        CHECK(evo::samplesPerPixel(spp16) == 16);
        CHECK(evo::samplesPerPixel(evo::parseOptions({"--capture-dir", "out", "--spp", "2"})) == 2);
        CHECK(evo::parseOptions({"--spp", "1"}).spp == 1);
        CHECK(evo::parseOptions({"--spp", "4096"}).spp == 4096);
        CHECK(parseError({"--spp", "0"}) == "--spp must be between 1 and 4096, got 0");
        CHECK(parseError({"--spp", "5000"}) == "--spp must be between 1 and 4096, got 5000");
        CHECK(parseError({"--spp", "x"}) == "--spp expects an integer, got 'x'");
        CHECK(!parseError({"--spp", "-1"}).empty());
        CHECK(!parseError({"--spp", "2.5"}).empty());
        CHECK(parseError({"--spp"}) == "--spp needs a value");

        // --detail: the 2026 scenes' detail, 0 < F <= 1, default 1.
        CHECK(defaults.detail == 1.f);
        CHECK(evo::parseOptions({"--detail", "0.004"}).detail == 0.004f);
        CHECK(evo::parseOptions({"--detail", "1"}).detail == 1.f);
        CHECK(parseError({"--detail", "0"}) == "--detail must be greater than 0 and at most 1, got 0");
        CHECK(parseError({"--detail", "1.5"}) == "--detail must be greater than 0 and at most 1, got 1.5");
        CHECK(!parseError({"--detail", "-0.5"}).empty());
        CHECK(parseError({"--detail", "x"}) == "--detail expects a number, got 'x'");
        CHECK(parseError({"--detail"}) == "--detail needs a value");

        // --trace-scale: the window's and the bench's trace resolution, 0.25..1, default 0.67; the exports
        // always trace at the output size (1), and refuse the option.
        CHECK(!defaults.traceScale && evo::traceScaleFor(defaults) == 0.67f);
        CHECK(evo::traceScaleFor(evo::parseOptions({"--trace-scale", "0.5"})) == 0.5f);
        CHECK(evo::traceScaleFor(evo::parseOptions({"--trace-scale", "1"})) == 1.f);
        CHECK(evo::traceScaleFor(evo::parseOptions({"--trace-scale", "0.25"})) == 0.25f);
        CHECK(evo::traceScaleFor(evo::parseOptions({"--capture-dir", "out"})) == 1.f);
        CHECK(evo::traceScaleFor(evo::parseOptions({"--render-stream", "--duration", "1"})) == 1.f);
        CHECK(evo::traceScaleFor(evo::parseOptions({"--bench"})) == 0.67f);
        CHECK(parseError({"--trace-scale", "0.2"}) == "--trace-scale must be between 0.25 and 1, got 0.2");
        CHECK(parseError({"--trace-scale", "1.5"}) == "--trace-scale must be between 0.25 and 1, got 1.5");
        CHECK(parseError({"--trace-scale", "x"}) == "--trace-scale expects a number, got 'x'");
        CHECK(parseError({"--trace-scale", "0.5", "--capture-dir", "out"}) ==
              "--trace-scale is for the window and --bench: the exports trace at the output size");
        CHECK(!parseError({"--trace-scale", "0.5", "--render-stream", "--duration", "1"}).empty());
        CHECK(evo::parseOptions({"--trace-scale", "auto"}).autoTraceScale);
        CHECK(!evo::parseOptions({"--trace-scale", "auto"}).traceScale);
        CHECK(!parseError({"--trace-scale", "auto", "--capture-dir", "out"}).empty());

        // --bench: Interactive and headless, per chapter; --bench-frames N (default 120); --chapters LIST.
        CHECK(!defaults.bench && defaults.benchFrames == 120 && defaults.chapters.empty());
        const evo::Options bench =
            evo::parseOptions({"--bench", "--bench-frames", "3", "--chapters", "11,3,0", "--width", "96"});
        CHECK(bench.bench && bench.headless && bench.benchFrames == 3);
        CHECK((bench.chapters == std::vector<int>{11, 3, 0}));
        CHECK(evo::samplesPerPixel(bench) == 1); // interactive, like the window
        CHECK(parseError({"--bench", "--bench-frames", "0"}) ==
              "--bench-frames must be between 1 and 100000, got 0");
        CHECK(parseError({"--bench", "--chapters", "1,,2"}) ==
              "--chapters expects a comma-separated list of chapter indices, got '1,,2'");
        CHECK(!parseError({"--bench", "--chapters", "-1"}).empty());
        CHECK(!parseError({"--bench", "--chapters", ""}).empty());
        CHECK(parseError({"--chapters", "1"}) == "--chapters and --bench-frames need --bench");
        CHECK(parseError({"--bench-frames", "5"}) == "--chapters and --bench-frames need --bench");
        CHECK(!defaults.benchCounters && !bench.benchCounters);
        CHECK(evo::parseOptions({"--bench", "--bench-counters"}).benchCounters);
        CHECK(!parseError({"--bench-counters"}).empty());
        CHECK(defaults.fastMode && !evo::parseOptions({"--bench", "--no-fast-mode"}).fastMode);
        CHECK(evo::parseOptions({"--bench", "--bench-capture", "out"}).benchCapture == "out");
        CHECK(!parseError({"--bench-capture", "out"}).empty());
        CHECK(!parseError({"--no-fast-mode", "--capture-dir", "out"}).empty());
        CHECK(parseError({"--bench", "--capture-dir", "out"}) == "--bench cannot be combined with an export");
        CHECK(!parseError({"--bench", "--render-stream", "--duration", "1"}).empty());

        CHECK(evo::helpText().find("--render-stream") != std::string::npos);
        for (const char* flag :
             {"--no-audio", "--mute", "--music", "--show-info", "--audio-offset", "--no-crt", "--no-hud",
              "--spp", "--detail", "--trace-scale", "--bench", "--bench-frames", "--bench-counters",
              "--bench-capture", "--chapters", "--no-fast-mode"})
            CHECK(evo::helpText().find(flag) != std::string::npos);
        CHECK(evo::helpText().find("Calibrate A/V sync") != std::string::npos);
        CHECK(evo::helpText().find("keys 1-9 and 0 jump to chapters 1-10") != std::string::npos);
        CHECK(evo::helpText().find("C CRT look") != std::string::npos);
        CHECK(evo::helpText().find("H HUD") != std::string::npos);
        CHECK(evo::helpText().find("EVO_FORCE_ERA") != std::string::npos);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CHECK failed: %s\n", error.what());
        return 1;
    }
    std::printf("%d checks passed\n", checksPassed);
    return 0;
}
