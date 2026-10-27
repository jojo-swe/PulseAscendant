#include "app/bench.hpp"

#include "app/auto_scale.hpp"

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/gpu_timers.hpp"
#include "gfx/paths.hpp"
#include "gfx/readback.hpp"
#include "image/png.hpp"
#include "modern/modern_scene.hpp"
#include "render/renderer.hpp"
#include "show/timeline.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace evo {

namespace {

// The reset frames (4 and 2 spp in the window) before the timed ones; --bench-export's frames stand alone, so
// one frame (first uses: pipelines, BLASes) is enough there.
constexpr int kWarmUpFrames = 2;
constexpr int kExportWarmUpFrames = 1;
// Where in a chapter the timed frames start.
constexpr double kChapterFraction = 0.2;

std::string jsonString(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof escaped, "\\u%04x", static_cast<unsigned>(c));
            out += escaped;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

std::string number(double value, int decimals) {
    if (!std::isfinite(value))
        value = 0.0;
    char text[64];
    std::snprintf(text, sizeof text, "%.*f", decimals, value);
    return text;
}

double chapterEnd(int chapter) {
    return chapter + 1 < show::chapterCount() ? show::chapterStart(chapter + 1) : show::showDuration();
}

} // namespace

const std::vector<std::string>& benchPasses() {
    static const std::vector<std::string> passes = {
        "total",  "retro",   "fill",    "particles", "medium", "tlas",    "photons",
        "trace",  "splats",  "denoise", "upscale",   "bloom",  "tonemap", "compositor"};
    return passes;
}

std::string benchJson(const BenchRecord& r) {
    const auto object = [](const std::vector<std::pair<std::string, double>>& values) {
        std::string out;
        for (const auto& [name, value] : values)
            out += (out.empty() ? "" : ",") + jsonString(name) + ":" + number(value, 4);
        return "{" + out + "}";
    };
    std::string extra = ",\"cpuMs\":" + number(r.cpuMs, 4) + ",\"waitMs\":" + number(r.waitMs, 4) +
                        ",\"fast\":" + (r.fast ? "true" : "false") + (r.exportFrames ? ",\"export\":true" : "") +
                        (r.targetFps > 0 ? ",\"targetFps\":" + std::to_string(r.targetFps) : "");
    if (!r.counters.empty())
        extra += ",\"counters\":" + object(r.counters);
    if (!r.stats.empty())
        extra += ",\"stats\":" + object(r.stats);
    return "{\"chapter\":" + std::to_string(r.chapter) + ",\"title\":" + jsonString(r.title) +
           ",\"scene\":" + jsonString(r.scene) + ",\"frames\":" + std::to_string(r.frames) +
           ",\"spp\":" + std::to_string(r.spp) + ",\"traceScale\":" + number(r.traceScale, 4) +
           ",\"width\":" + std::to_string(r.width) + ",\"height\":" + std::to_string(r.height) +
           ",\"traceWidth\":" + std::to_string(r.traceWidth) +
           ",\"traceHeight\":" + std::to_string(r.traceHeight) + ",\"device\":" + jsonString(r.device) +
           ",\"timestamps\":" + (r.timestamps ? "true" : "false") + ",\"gpuMs\":" + object(r.gpuMs) + extra +
           ",\"fps\":" + number(r.fps, 3) + "}";
}

int runBench(const Options& options) {
    std::vector<int> chapters = options.chapters;
    if (chapters.empty())
        for (int c = 0; c < show::chapterCount(); ++c)
            chapters.push_back(c);
    for (const int chapter : chapters)
        if (chapter < 0 || chapter >= show::chapterCount())
            throw std::runtime_error("--chapters: the show has chapters 0.." +
                                     std::to_string(show::chapterCount() - 1) + ", got " +
                                     std::to_string(chapter));

    gfx::ContextOptions contextOptions;
    contextOptions.validation = options.validation;
    contextOptions.pipelineCacheFile = gfx::pipelineCachePath(); // no recompiles on the next run
    gfx::Context ctx = gfx::Context::create(contextOptions);
    gfx::FrameRing ring(ctx);
    render::RenderSettings settings;
    settings.width = static_cast<std::uint32_t>(options.width);
    settings.height = static_cast<std::uint32_t>(options.height);
    settings.crt = options.crt;
    settings.hud = options.hud;
    settings.hudTitle = !options.benchExport; // as the window draws (an export draws no title)
    settings.mode = options.benchExport ? render::RenderMode::Export : render::RenderMode::Interactive;
    settings.spp = static_cast<std::uint32_t>(samplesPerPixel(options));
    settings.detail = modern::SceneDetail{options.detail};
    settings.traceScale = traceScaleFor(options);
    settings.counters = options.benchCounters;
    settings.fast = options.fastMode; // as the window renders (an export never uses it)
    if (options.benchExport) {
        // As --render-stream renders (app.cpp's runRenderStream).
        settings.shutterSeconds = 0.5 / options.fps;
        settings.submitPerSlice = true;
        settings.shutterSlices = static_cast<std::uint32_t>(options.shutterSlices);
    }
    render::Renderer renderer(ctx, ring, settings);

    const double step = 1.0 / options.fps;
    const int warmUp = options.benchExport ? kExportWarmUpFrames : kWarmUpFrames;
    const auto firstTimed = [&](int chapter) {
        const double start = show::chapterStart(chapter);
        return start + kChapterFraction * (chapterEnd(chapter) - start);
    };
    std::vector<double> times;
    for (const int chapter : chapters)
        for (int i = -warmUp; i < options.benchFrames; ++i)
            times.push_back(firstTimed(chapter) + i * step);
    renderer.prepare(times); // setup: every scene's GPU side before the first timed frame

    gfx::GpuTimers& timers = renderer.timers();
    std::optional<gfx::Readback> readback;
    if (!options.benchCapture.empty() || options.benchExport)
        readback.emplace(ctx, settings.width, settings.height);
    const bool adaptive = adaptiveTraceScale(options);
    for (const int chapter : chapters) {
        const double t0 = firstTimed(chapter);
        // The adaptive scale (unless --trace-scale F): the chapter's scale from the table, no controller, so
        // the numbers are reproducible.
        if (adaptive)
            renderer.setTraceScale(autoTraceScale(chapter, settings.width, settings.height, options.targetFps));
        const VkExtent2D trace = renderer.modern().traceExtent();
        std::fprintf(stderr, "bench: chapter %d (%s), %d frames\n", chapter, show::sampleTimeline(t0).title,
                     options.benchFrames);
        for (int i = -warmUp; i < 0; ++i) {
            VkCommandBuffer cmd = ring.begin();
            renderer.render(cmd, show::sampleTimeline(t0 + i * step));
            ring.submit();
        }
        ring.waitAll();
        timers.collect();
        timers.takeCompleted();               // the warm-up frames' times
        renderer.modern().takeCounters(); // and their work

        using Clock = std::chrono::steady_clock;
        std::vector<std::uint8_t> exportFrame; // --bench-export: the last frame read back
        const auto start = Clock::now();
        double cpuSeconds = 0.0, waitSeconds = 0.0;
        for (int i = 0; i < options.benchFrames; ++i) {
            const auto before = Clock::now();
            VkCommandBuffer cmd = ring.begin();
            const auto begun = Clock::now();
            renderer.render(cmd, show::sampleTimeline(t0 + i * step));
            cpuSeconds += std::chrono::duration<double>(Clock::now() - begun).count();
            waitSeconds += std::chrono::duration<double>(begun - before).count();
            if (options.benchExport) {
                // As an export: every frame read back and converted before the next is recorded (cpuMs then
                // includes the slices' waits, FrameRing::split).
                readback->record(cmd, renderer.output());
                ring.wait(ring.submit());
                exportFrame = readback->rgb();
                continue;
            }
            if (readback && i + 1 == options.benchFrames)
                readback->record(cmd, renderer.output()); // --bench-capture: the last timed frame
            ring.submit();
        }
        ring.waitAll();
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        timers.collect();
        const std::vector<gfx::GpuTimers::Scopes> frames = timers.takeCompleted();

        std::map<std::string, double> sums;
        for (const auto& scopes : frames)
            for (const auto& [name, ms] : scopes)
                sums[name] += ms;
        BenchRecord record;
        const show::TimelineFrame frame = show::sampleTimeline(t0);
        record.chapter = chapter;
        record.title = frame.title;
        record.scene = modern::effectSceneName(frame.effect);
        record.device = ctx.deviceName();
        record.frames = options.benchFrames;
        record.spp = settings.spp;
        record.traceScale = renderer.traceScale();
        record.width = settings.width;
        record.height = settings.height;
        record.traceWidth = trace.width;
        record.traceHeight = trace.height;
        record.timestamps = timers.enabled();
        for (const std::string& pass : benchPasses())
            record.gpuMs.emplace_back(pass,
                                      frames.empty() ? 0.0 : sums[pass] / static_cast<double>(frames.size()));
        record.fps = seconds > 0.0 ? options.benchFrames / seconds : 0.0;
        record.fast = settings.fast && !options.benchExport;
        record.exportFrames = options.benchExport;
        record.targetFps = adaptive ? options.targetFps : 0;
        record.cpuMs = 1000.0 * cpuSeconds / options.benchFrames;
        record.waitMs = 1000.0 * waitSeconds / options.benchFrames;
        if (options.benchCounters) {
            const modern::Counters counters = renderer.modern().takeCounters();
            const render::ModernStage::FrameStats& s = renderer.modern().lastFrameStats();
            const double pixels = double(options.benchFrames) * double(trace.width) * double(trace.height);
            for (std::size_t k = 0; k < counters.size(); ++k)
                record.counters.emplace_back(modern::kCounterNames[k], double(counters[k]) / pixels);
            record.stats = {{"instances", s.instances},
                            {"rectLights", s.rectLights},
                            {"portals", s.portals},
                            {"treeLights", s.treeLights},
                            {"thinPanes", s.thinPanes},
                            {"fog", s.fog},
                            {"lensing", s.lensing},
                            {"gridVoxels", s.gridVoxels},
                            {"photons", s.photons},
                            {"causticTexels", s.causticTexels},
                            {"particles", s.particles},
                            {"splats", s.splats},
                            {"spp", s.spp},
                            {"maxBounces", s.maxBounces},
                            {"slices", s.slices},
                            {"raygenStackBytes", double(s.raygenStack)},
                            {"hitStackBytes", double(s.hitStack)},
                            {"pipelineFeatures", double(s.pipelineFeatures)}};
        }
        if (!options.benchCapture.empty()) {
            // The ring is idle (waitAll above).
            const std::vector<std::uint8_t> rgb = options.benchExport ? exportFrame : readback->rgb();
            const std::filesystem::path path =
                options.benchCapture / ("bench_" + std::to_string(chapter) + ".png");
            image::writePng(path, options.width, options.height, rgb.data());
            std::fprintf(stderr, "bench: %s\n", gfx::pathToUtf8(path).c_str());
        }
        std::printf("%s\n", benchJson(record).c_str());
        std::fflush(stdout);
    }
    return 0;
}

} // namespace evo
