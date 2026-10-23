#include "app/capture.hpp"

#include "gfx/context.hpp"
#include "gfx/offscreen.hpp"
#include "image/png.hpp"

#include <cstdio>
#include <filesystem>

namespace evo {

namespace {

std::filesystem::path captureFileName(double seconds) {
    char name[64];
    std::snprintf(name, sizeof(name), "frame-%07.3f.png", seconds);
    return name;
}

} // namespace

int runCapture(const Options& options) {
    gfx::ContextOptions contextOptions;
    contextOptions.validation = options.validation;
    gfx::Context ctx = gfx::Context::create(contextOptions);
    gfx::Offscreen offscreen(ctx, options.width, options.height);

    offscreen.render(options.time);
    const gfx::Frame frame = offscreen.readback();

    // writePng creates the missing parent directories and truncates an existing file.
    const std::filesystem::path path = options.captureDir / captureFileName(options.time);
    image::writePng(path, frame.width, frame.height, frame.rgb.data());
    std::printf("%s\n", path.string().c_str());
    return 0;
}

} // namespace evo
