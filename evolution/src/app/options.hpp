#pragma once

#include <filesystem>
#include <optional>
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
    bool hud = true;                  // the epoch cards (and the window's chapter title)
    std::filesystem::path captureDir; // empty = no capture
    std::filesystem::path music;      // empty = the show's music beside the executable
    // Samples per pixel per frame (--spp, kMinSpp..kMaxSpp); unset = the mode's default (samplesPerPixel).
    std::optional<int> spp;
};

inline constexpr int kMinSpp = 1;
inline constexpr int kMaxSpp = 4096;
inline constexpr int kWindowSpp = 1;  // the window: its temporal history accumulates more over frames
inline constexpr int kExportSpp = 64; // --capture-dir and --render-stream: every frame stands alone

// The samples per pixel a run renders each frame with: --spp, or kExportSpp for the exports (--capture-dir,
// --render-stream) and kWindowSpp for the window.
int samplesPerPixel(const Options& options);

// Parses the arguments after the program name. Throws std::runtime_error on bad input.
Options parseOptions(const std::vector<std::string>& args);

std::string helpText();

} // namespace evo
