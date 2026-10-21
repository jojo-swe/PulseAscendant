#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace evo {

// Command-line settings for one run of the demo.
struct Options {
    int width = 1280;
    int height = 720;
    int fps = 60;
    double time = 0.0;     // timeline position to start from, in seconds
    double duration = 0.0; // 0 = run until Esc
    bool headless = false;
    bool renderStream = false;
    bool validation = false;
    bool help = false;
    bool deviceInfo = false;
    bool fullscreen = false;
    std::filesystem::path captureDir; // empty = no capture
};

// Parses the arguments after the program name. Throws std::runtime_error on bad input.
Options parseOptions(const std::vector<std::string>& args);

std::string helpText();

} // namespace evo
