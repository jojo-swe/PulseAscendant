#pragma once

#include "app/options.hpp"

#include <string>
#include <utility>
#include <vector>

namespace evo {

// --bench (Task 4 of step 5): the window's rendering, headless and timed on the GPU, so it can be measured
// on RTX hardware. Interactive mode at --width x --height, --spp (1), --trace-scale (0.67) and
// --detail, with the window's CRT and HUD settings (title included); every path-traced scene of the chosen
// chapters is prepared first. Per chapter (--chapters, default all, in the order given): two warm-up frames
// (the history reset's 4 and 2 spp), then --bench-frames frames 1/--fps apart from 20 % into the chapter, two
// in flight as in the window. Then one JSON line on stdout:
//   {"chapter", "title", "scene", "frames", "spp", "traceScale", "width", "height", "traceWidth",
//    "traceHeight", "device", "timestamps", "gpuMs": {"total", "retro", "fill", "tlas", "trace", "denoise",
//    "upscale", "tonemap", "compositor"}, "fps"}
// gpuMs are means over the timed frames (a pass a frame did not run counts 0); timestamps is false (and every
// time 0) on a queue without timestamps; fps counts the timed frames over their wall-clock time, waits for
// the GPU included. Progress goes to stderr. Throws std::runtime_error for a chapter the show does not have.
int runBench(const Options& options);

// One bench result as its JSON line (without the newline).
struct BenchRecord {
    int chapter = 0;
    std::string title, scene, device;
    int frames = 0;
    unsigned spp = 0;
    float traceScale = 1.f;
    unsigned width = 0, height = 0, traceWidth = 0, traceHeight = 0;
    bool timestamps = false;
    std::vector<std::pair<std::string, double>> gpuMs; // in the order of benchPasses()
    double fps = 0.0;
};
std::string benchJson(const BenchRecord& record);
// The passes every line reports, in order.
const std::vector<std::string>& benchPasses();

} // namespace evo
