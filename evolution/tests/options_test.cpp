#include "app/options.hpp"

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

        CHECK(evo::helpText().find("--render-stream") != std::string::npos);
        for (const char* flag : {"--no-audio", "--mute", "--music", "--show-info"})
            CHECK(evo::helpText().find(flag) != std::string::npos);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CHECK failed: %s\n", error.what());
        return 1;
    }
    std::printf("%d checks passed\n", checksPassed);
    return 0;
}
