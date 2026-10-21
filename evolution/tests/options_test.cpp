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
        CHECK(!parseError({"--width", "32"}).empty());
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
