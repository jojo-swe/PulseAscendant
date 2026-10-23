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

        const evo::Options stream = evo::parseOptions({"--render-stream"});
        CHECK(stream.renderStream);
        CHECK(stream.headless);

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

        CHECK(evo::helpText().find("--render-stream") != std::string::npos);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CHECK failed: %s\n", error.what());
        return 1;
    }
    std::printf("%d checks passed\n", checksPassed);
    return 0;
}
