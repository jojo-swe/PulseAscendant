#include "app/capture.hpp"

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/paths.hpp"
#include "gfx/readback.hpp"
#include "image/png.hpp"
#include "render/renderer.hpp"
#include "show/timeline.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

void applyForcedEra(show::TimelineFrame& frame) {
    const char* value = std::getenv("EVO_FORCE_ERA");
    if (value == nullptr || (std::strcmp(value, "0") != 0 && std::strcmp(value, "1") != 0))
        return;
    const float era = value[0] == '1' ? 1.f : 0.f;
    frame.era = era;
    frame.wipe = era;
    frame.eraCut = 0.f;
}

int runCapture(const Options& options) {
    gfx::ContextOptions contextOptions;
    contextOptions.validation = options.validation;
    gfx::Context ctx = gfx::Context::create(contextOptions);

    const auto width = static_cast<std::uint32_t>(options.width);
    const auto height = static_cast<std::uint32_t>(options.height);
    gfx::Readback readback(ctx, width, height);
    gfx::FrameRing ring(ctx);
    render::Renderer renderer(ctx, ring, {width, height, options.crt, options.hud}); // no title: cards only
    show::TimelineFrame frame = show::sampleTimeline(options.time);
    applyForcedEra(frame);
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
