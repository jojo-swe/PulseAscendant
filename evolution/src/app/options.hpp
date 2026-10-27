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
    // --first-frame N (--render-stream): the stream's frame i shows time + (N + i) / fps, so a stream cut
    // into pieces renders exactly the frames of one long stream.
    long firstFrame = 0;
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
    // The 2026 scenes' detail (--detail, 0 < F <= 1): particle, splat, voxel and photon counts scale with it.
    float detail = 1.f;
    // The trace resolution as a fraction of the output (--trace-scale, kMinTraceScale..1); unset = the
    // mode's (traceScaleFor). The exports refuse it: they always trace at the output size.
    std::optional<float> traceScale;
    // --trace-scale auto: the adaptive scale (app/auto_scale.hpp); the window's default (adaptiveTraceScale).
    bool autoTraceScale = false;
    // The frame rate the window's adaptive trace scale aims for (--target-fps 30|60, app/auto_scale.hpp): its
    // table and its controller's GPU budget. The window and --bench; 60 by default.
    int targetFps = 60;
    // The window's fast mode (render::RenderSettings::fast) for the window and --bench; --no-fast-mode turns
    // it off. The exports never use it.
    bool fastMode = true;
    // --bench: Interactive and headless, every chapter of `chapters` (empty: all) for benchFrames frames,
    // one JSON line each on stdout (app/bench.hpp).
    bool bench = false;
    int benchFrames = 120;
    bool benchCounters = false; // --bench-counters: the work counters and scene statistics per chapter
    std::filesystem::path benchCapture; // --bench-capture DIR: each chapter's last timed frame as a PNG
    // --bench-export: --bench times export frames instead (--render-stream's: Export mode, --spp, default
    // kExportSpp, the 180 degree shutter at --fps, the output size traced, one submit per time slice, each
    // frame read back before the next), so an export's cost per pass and its work counters can be measured.
    bool benchExport = false;
    std::vector<int> chapters; // --chapters LIST: chapter indices, as --show-info numbers them
    // --secret-part[=WORD]: the window and --render-stream run the part of retro/secret_part.hpp instead of
    // the show (--time and --duration then count its seconds); WORD is checked as the patient's answer.
    bool secretPart = false;
    std::string secretAnswer;
};

inline constexpr float kMinTraceScale = 0.25f;
inline constexpr float kWindowTraceScale = 0.67f; // the window and --bench
inline constexpr int kMaxBenchFrames = 100000;

// The trace scale a run renders with: --trace-scale, or 1 for the exports (--capture-dir, --render-stream),
// which trace at the output size, and kWindowTraceScale for the window and --bench.
float traceScaleFor(const Options& options);
// Whether the trace scale adapts per chapter (app/auto_scale.hpp): in the window and --bench unless
// --trace-scale F (--bench takes the table's scale per chapter, no controller), never in an export.
bool adaptiveTraceScale(const Options& options);

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
