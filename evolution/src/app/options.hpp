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
    bool showInfo = false;            // print the show as JSON and exit
    bool noAudio = false;             // run on the monotonic clock without opening an audio device
    bool mute = false;                // start muted (the music still drives the clock)
    double audioOffsetMs = 0.0;       // added to the music time the visuals follow (A/V calibration)
    bool crt = true;                  // the CRT look on the 1993 picture (--no-crt, key C)
    bool hud = true;                  // the chapter title and epoch cards (--no-hud, key H)
    std::filesystem::path captureDir; // empty = no capture
    std::filesystem::path music;      // empty = the show's music beside the executable
};

// Parses the arguments after the program name. Throws std::runtime_error on bad input.
Options parseOptions(const std::vector<std::string>& args);

std::string helpText();

} // namespace evo
