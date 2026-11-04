#include "app/capture.hpp"

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/paths.hpp"
#include "gfx/readback.hpp"
#include "image/png.hpp"
#include "modern/modern_scene.hpp"
#include "render/renderer.hpp"
#include "show/timeline.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>
#include <vector>

namespace evo {

namespace {

std::filesystem::path captureFileName(double seconds) {
    char name[64];
    // Adding 0.0 turns the -0.0 that "--time -0" parses to into 0.0, which prints without a sign.
    std::snprintf(name, sizeof(name), "frame-%07.3f.png", seconds + 0.0);
    return name;
}

} // namespace

bool applyForcedEra(show::TimelineFrame& frame) {
    const char* value = std::getenv("EVO_FORCE_ERA");
    if (value == nullptr || (std::strcmp(value, "0") != 0 && std::strcmp(value, "1") != 0))
        return false;
    const float era = value[0] == '1' ? 1.f : 0.f;
    frame.era = era;
    frame.wipe = era;
    frame.eraCut = 0.f;
    return true;
}

int runCapture(const Options& options) {
    gfx::ContextOptions contextOptions;
    contextOptions.validation = options.validation;
    contextOptions.pipelineCacheFile = gfx::pipelineCachePath(); // no recompiles on the next run
    gfx::Context ctx = gfx::Context::create(contextOptions);

    const auto width = static_cast<std::uint32_t>(options.width);
    const auto height = static_cast<std::uint32_t>(options.height);
    gfx::Readback readback(ctx, width, height);
    gfx::FrameRing ring(ctx);
    // Export: a pure function of the show time (and --spp, 64 by default); no title, cards only.
    render::RenderSettings settings{width, height, options.crt, options.hud};
    settings.mode = render::RenderMode::Export;
    settings.spp = static_cast<std::uint32_t>(samplesPerPixel(options));
    settings.shutterSeconds = 0.5 / options.fps; // a 180 degree shutter; the window has none
    settings.detail = modern::SceneDetail{options.detail};
    settings.submitPerSlice = true; // one submit per time slice: no frame-long submit to trip a GPU watchdog
    settings.shutterSlices = static_cast<std::uint32_t>(options.shutterSlices); // 0: one per 4 samples
    render::Renderer renderer(ctx, ring, settings);
    show::TimelineFrame frame = show::sampleTimeline(options.time);
    applyForcedEra(frame);
    // The frame's path-traced scene (BLASes, pipeline, denoiser images) as setup, before the frame.
    renderer.prepare(std::span<const double>(&options.time, 1));
    VkCommandBuffer cmd = ring.begin();
    renderer.render(cmd, frame);
    readback.record(cmd, renderer.output());
    ring.wait(ring.submit());
    const std::vector<std::uint8_t> rgb = readback.rgb();

    // writePng creates the missing parent directories and truncates an existing file.
    const std::filesystem::path path = options.captureDir / captureFileName(options.time);
    image::writePng(path, options.width, options.height, rgb.data());
    std::printf("%s\n", gfx::pathToUtf8(path).c_str());
    return 0;
}

} // namespace evo
