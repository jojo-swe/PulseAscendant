#pragma once

// Stills to look at, for the scene tests' `--look` modes: each named show time rendered alone (export mode,
// the 60 fps shutter, the full post) in 2026, written as a PNG.
// EVO_LOOK_ONLY keeps the stills whose name contains it, EVO_LOOK_AT renders one show time instead,
// EVO_LOOK_DETAIL sets the scene detail and EVO_LOOK_HUD=1 draws the epoch cards. One contact sheet per
// group of stills (their names up to the first '_').

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "image/png.hpp"
#include "render/renderer.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace evo::test {

struct LookStill {
    double seconds; // show time
    std::string name;
};

inline void renderLookStills(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp,
                             std::uint32_t w, std::uint32_t h, std::vector<LookStill> stills,
                             std::uint32_t columns = 4) {
    if (const char* only = std::getenv("EVO_LOOK_ONLY")) {
        std::vector<LookStill> kept;
        for (const LookStill& s : stills)
            if (s.name.find(only) != std::string::npos)
                kept.push_back(s);
        stills = kept;
    }
    if (const char* at = std::getenv("EVO_LOOK_AT"))
        stills = {{std::stod(at), "at"}};
    if (stills.empty())
        return;
    gfx::FrameRing ring(ctx);
    render::RenderSettings s{w, h};
    const char* hud = std::getenv("EVO_LOOK_HUD");
    s.hud = hud != nullptr && std::string(hud) == "1";
    s.mode = render::RenderMode::Export;
    s.spp = spp;
    s.detail = {1.f};
    if (const char* d = std::getenv("EVO_LOOK_DETAIL"))
        s.detail = {std::stof(d)};
    s.shutterSeconds = 0.5 / 60;
    render::Renderer renderer(ctx, ring, s);
    gfx::Readback readback(ctx, w, h);
    std::vector<double> times;
    for (const LookStill& still : stills)
        times.push_back(still.seconds);
    renderer.prepare(times);
    std::filesystem::create_directories(dir);
    // One contact sheet per group: the stills' names up to their first '_' (the chapter).
    std::map<std::string, std::vector<std::vector<std::uint8_t>>> groups;
    for (std::size_t i = 0; i < stills.size(); ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        show::TimelineFrame frame = show::sampleTimeline(stills[i].seconds);
        frame.era = 1.f;
        frame.wipe = 1.f;
        frame.eraCut = 0.f;
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, frame);
        readback.record(cmd, renderer.output());
        ring.wait(ring.submit());
        std::vector<std::uint8_t> rgb = readback.rgb();
        double mean = 0;
        for (std::size_t p = 0; p < rgb.size(); p += 3)
            mean += (0.2126 * rgb[p] + 0.7152 * rgb[p + 1] + 0.0722 * rgb[p + 2]) / 255.0;
        mean /= double(rgb.size() / 3);
        const std::filesystem::path path = dir / (stills[i].name + ".png");
        image::writePng(path, int(w), int(h), rgb.data());
        std::printf("look: %s (%.3f s, local %.2f, mean %.3f) in %.1f s\n", path.string().c_str(),
                    stills[i].seconds, frame.localSeconds, mean,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        groups[stills[i].name.substr(0, stills[i].name.find('_'))].push_back(std::move(rgb));
    }
    for (const auto& [group, pictures] : groups) {
        const auto n = static_cast<std::uint32_t>(pictures.size());
        const std::uint32_t cols = std::min(columns, n), rows = (n + cols - 1) / cols;
        std::vector<std::uint8_t> sheet(std::size_t(w) * cols * h * rows * 3, 0);
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::size_t cx = i % cols, cy = i / cols;
            for (std::uint32_t y = 0; y < h; ++y)
                std::copy_n(pictures[i].begin() + std::ptrdiff_t(std::size_t(y) * w * 3), std::size_t(w) * 3,
                            sheet.begin() + std::ptrdiff_t(((cy * h + y) * std::size_t(w) * cols + cx * w) * 3));
        }
        image::writePng(dir / ("sheet_" + group + ".png"), int(w * cols), int(h * rows), sheet.data());
    }
    vkQueueWaitIdle(ctx.queue());
}

} // namespace evo::test
