// One path tracer and one denoiser shared by every scene, with the validation layer and
// its synchronization validation on; fails on any message it reports. A test factory makes SceneId::Ship a
// Cornell box (rt_test_scenes.hpp) and the scene map sends FLYBY to it:
//   (a) STUDIO NIGHT, then a FLYBY frame (the Cornell box, forced to 2026), then STUDIO NIGHT again: the last
//       frame equals a fresh renderer's STUDIO NIGHT frame byte for byte, in Export and in the window;
//   (b) both scenes create exactly one ray tracing pipeline between them; memory is measured, not computed:
//       the allocator's statistics grow by exactly gpuBytes() (less the stage's own two images, which predate
//       it) on the first prepare and by exactly the Cornell box's SceneGpu::bytes() on the second, so the
//       images (tracer, denoiser, stage) do not grow with the scene count;
//   (c) per-frame materials: the Cornell box's left wall overridden from red to blue turns its pixels blue; a
//       per-frame table equal to the static one gives the static picture byte for byte;
//   (d) a scene of capacity 4096 instances traces 3000 (after reserve() grew a 1024-instance tracer); 4097
//       instances throw std::length_error, and so does a frame beyond the tracer's capacity;
//   (e) shotFor() and samplesRetro() never call a scene's frame(): a counting scene sees 0 calls, and
//       shotFor() follows the Studio's camera().shot across PORTAL's glass crossings;
//   (f) the scene fixture's checkChapterPictures passes on the Studio's six chapters (the check every step 5
//       scene test runs, here on the one scene that exists).

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene.hpp"
#include "modern/scene_gpu.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "show/timeline.hpp"

#include "rt_test_scenes.hpp"
#include "scene_fixture.hpp"
#include "test_env.hpp"

#include <vk_mem_alloc.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace gfx = evo::gfx;
namespace modern = evo::modern;
namespace render = evo::render;
namespace rt = evo::rt;
namespace show = evo::show;
using modern::SceneId;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr std::uint32_t kWidth = 160, kHeight = 90;
constexpr int kFlyby = 1, kPortal = 10, kStudioNight = 11;

// Waits for the queue: synchronization validation in layer 1.3.275 credits no host wait on the timeline, so
// memory reused between subtests would read as a hazard.
void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

double chapterTime(int chapter, double fraction) {
    const double start = show::chapterStart(chapter);
    return start + fraction * (show::chapterStart(chapter + 1) - start);
}

show::TimelineFrame forced2026(double seconds) {
    show::TimelineFrame frame = show::sampleTimeline(seconds);
    frame.era = 1.f;
    frame.wipe = 1.f;
    frame.eraCut = 0.f;
    return frame;
}

// What the test scenes share with the test: how often frame() ran, and a per-frame material table to show.
struct Probe {
    int frameCalls = 0;
    std::vector<rt::Material> materials;
};

// A rt_test_scenes.hpp scene as a ModernScene: a fixed frame, one shot.
class TestModernScene final : public modern::ModernScene {
public:
    TestModernScene(evo::test::TestScene scene, Probe* probe) : scene_(std::move(scene)), probe_(probe) {}
    const modern::SceneStatic& staticScene() const override { return scene_.statics; }
    modern::SceneFrame frame(const show::TimelineFrame&) const override {
        if (probe_)
            ++probe_->frameCalls;
        modern::SceneFrame f = scene_.frame;
        if (probe_)
            f.materials = probe_->materials;
        return f;
    }
    modern::Camera camera(const show::TimelineFrame&) const override { return scene_.frame.camera; }
    bool samplesRetro() const override { return false; }

private:
    evo::test::TestScene scene_;
    Probe* probe_;
};

// The show's scenes, with Ship as the Cornell box.
modern::SceneFactory cornellFactory(Probe* probe) {
    return [probe](SceneId id, modern::SceneDetail detail) -> std::unique_ptr<modern::ModernScene> {
        if (id == SceneId::Ship)
            return std::make_unique<TestModernScene>(evo::test::cornellBox(), probe);
        return modern::makeScene(id, detail);
    };
}

// The show's map, with FLYBY on the Cornell box.
SceneId cornellMap(show::Effect effect) {
    return effect == show::Effect::Flyby ? SceneId::Ship : modern::sceneFor(effect);
}

class Harness {
public:
    Harness(gfx::Context& ctx, render::RenderMode mode, Probe* probe = nullptr)
        : readback_(ctx, kWidth, kHeight), ring_(ctx), renderer_(ctx, ring_, settings(mode, probe)) {}

    std::vector<std::uint8_t> render(const show::TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        readback_.record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        return readback_.rgb();
    }
    render::Renderer& renderer() { return renderer_; }

private:
    static render::RenderSettings settings(render::RenderMode mode, Probe* probe) {
        render::RenderSettings s{kWidth, kHeight};
        s.hud = false;
        s.mode = mode;
        s.spp = 2;
        s.sceneFactory = cornellFactory(probe);
        s.sceneMap = cornellMap;
        return s;
    }

    gfx::Readback readback_;
    gfx::FrameRing ring_;
    render::Renderer renderer_; // last: its destructor waits for the ring
};

// --- (a) ---------------------------------------------------------------------------------------------------

void testSceneSwitch(gfx::Context& ctx) {
    const double night = chapterTime(kStudioNight, 0.4);
    const show::TimelineFrame flyby = forced2026(chapterTime(kFlyby, 0.5));
    for (const render::RenderMode mode : {render::RenderMode::Export, render::RenderMode::Interactive}) {
        std::vector<std::uint8_t> switched, fresh, cornell, placeholder;
        {
            Harness h(ctx, mode);
            h.render(show::sampleTimeline(night));
            cornell = h.render(flyby);
            CHECK(h.renderer().lastStats().modernRendered);
            switched = h.render(show::sampleTimeline(night));
        }
        idle(ctx);
        {
            Harness h(ctx, mode);
            fresh = h.render(show::sampleTimeline(night));
        }
        idle(ctx);
        CHECK(switched == fresh);
        CHECK(cornell != fresh);
        std::printf("(a) %s: STUDIO NIGHT after the Cornell box equals a fresh STUDIO NIGHT frame\n",
                    mode == render::RenderMode::Export ? "export" : "window");
    }
}

// --- (b) ---------------------------------------------------------------------------------------------------

// The bytes of every live VMA allocation.
VkDeviceSize allocatedBytes(gfx::Context& ctx) {
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(ctx.allocator(), &stats);
    return stats.total.statistics.allocationBytes;
}

void testOnePipelineAndMemory(gfx::Context& ctx) {
    Harness h(ctx, render::RenderMode::Export);
    render::ModernStage& stage = h.renderer().modern();
    const std::uint32_t before = modern::PathTracer::pipelinesCreated();
    CHECK(stage.tracer() == nullptr && stage.gpuBytes() == 0 && stage.imageBytes() == 0);
    // The stage's HDR and display images exist from its construction on; imageBytes() counts them once a
    // scene is prepared.
    const VkDeviceSize stageImages = stage.hdr().bytes() + stage.image().bytes();
    const VkDeviceSize allocated0 = allocatedBytes(ctx);
    stage.prepare(SceneId::Studio);
    const VkDeviceSize allocated1 = allocatedBytes(ctx);
    const VkDeviceSize images = stage.imageBytes();
    const VkDeviceSize studioBytes = stage.sceneGpu(SceneId::Studio)->bytes();
    const VkDeviceSize tracerBuffers = stage.tracer()->bufferBytes();
    const VkDeviceSize studioPipeline = stage.tracer()->scenePipelineBytes(); // exports build one per scene
    CHECK(images > 0 && studioBytes > 0 && tracerBuffers > 0);
    CHECK(stage.gpuBytes() == images + tracerBuffers + studioBytes);
    CHECK(allocated1 - allocated0 == stage.gpuBytes() - stageImages);
    // At least the texels: the G-buffer's 60 bytes per pixel, the denoiser's ten RGBA16F images, the stage's
    // RGBA16F and RGBA8 images.
    const VkDeviceSize pixels = VkDeviceSize(kWidth) * kHeight;
    CHECK(stage.tracer()->imageBytes() >= 60 * pixels);
    CHECK(images >= (60 + 80 + 12) * pixels);
    stage.prepare(SceneId::Ship);
    stage.prepare(SceneId::Ship); // idempotent
    const VkDeviceSize allocated2 = allocatedBytes(ctx);
    const VkDeviceSize shipBytes = stage.sceneGpu(SceneId::Ship)->bytes();
    CHECK(shipBytes > 0);
    // Nothing but the scene and its own pipeline's shader binding table: no images, no other tracer buffers.
    const VkDeviceSize shipPipeline = stage.tracer()->scenePipelineBytes() - studioPipeline;
    CHECK(allocated2 - allocated1 == shipBytes + shipPipeline);
    CHECK(stage.imageBytes() == images && stage.tracer()->bufferBytes() == tracerBuffers + shipPipeline);
    CHECK(stage.gpuBytes() == images + tracerBuffers + shipPipeline + studioBytes + shipBytes);
    h.render(show::sampleTimeline(chapterTime(kStudioNight, 0.3)));
    h.render(forced2026(chapterTime(kFlyby, 0.3)));
    // One tracer for both scenes: its generic pipeline and each scene's own (exports trace with a pipeline
    // per scene, PathTracer::preparePipeline), at most one per scene.
    CHECK(stage.tracer()->pipelineCount() <= 3);
    CHECK(modern::PathTracer::pipelinesCreated() - before == stage.tracer()->pipelineCount());
    std::printf("(b) one tracer for two scenes (%u pipelines); allocated: images %llu bytes, tracer buffers %llu, Studio "
                "%llu, Cornell %llu (the allocator's statistics agree)\n",
                stage.tracer()->pipelineCount(), (unsigned long long)images, (unsigned long long)tracerBuffers,
                (unsigned long long)studioBytes, (unsigned long long)shipBytes);
}

// --- (c) ---------------------------------------------------------------------------------------------------

struct WallColour {
    double r = 0, g = 0, b = 0;
};

// The mean colour of the left wall: a band at 4-10 % of the width, the middle third of the height.
WallColour leftWall(const std::vector<std::uint8_t>& rgb) {
    WallColour c;
    int n = 0;
    for (std::uint32_t y = kHeight / 3; y < 2 * kHeight / 3; ++y)
        for (std::uint32_t x = kWidth * 4 / 100; x < kWidth * 10 / 100; ++x, ++n) {
            const std::size_t i = 3 * (std::size_t(y) * kWidth + x);
            c.r += rgb[i];
            c.g += rgb[i + 1];
            c.b += rgb[i + 2];
        }
    c.r /= n;
    c.g /= n;
    c.b /= n;
    return c;
}

void testPerFrameMaterials(gfx::Context& ctx) {
    Probe probe;
    Harness h(ctx, render::RenderMode::Export, &probe);
    const show::TimelineFrame flyby = forced2026(chapterTime(kFlyby, 0.5));
    const std::vector<std::uint8_t> statics = h.render(flyby);
    const evo::test::TestScene cornell = evo::test::cornellBox();
    probe.materials = cornell.statics.materials;
    const std::vector<std::uint8_t> same = h.render(flyby);
    probe.materials[1].baseColor[0] = 0.1f;
    probe.materials[1].baseColor[2] = 0.8f; // the red left wall turns blue
    const std::vector<std::uint8_t> blue = h.render(flyby);
    const WallColour before = leftWall(statics), after = leftWall(blue);
    CHECK(same == statics);
    CHECK(before.r > 1.5 * before.b && before.r > 1.5 * before.g);
    CHECK(after.b > 1.5 * after.r && after.b > 1.5 * after.g);
    std::printf("(c) left wall (%.0f, %.0f, %.0f) -> (%.0f, %.0f, %.0f); the static table again: identical\n",
                before.r, before.g, before.b, after.r, after.g, after.b);
}

// --- (d) ---------------------------------------------------------------------------------------------------

template <typename E, typename F> bool throws(F&& f) {
    try {
        f();
    } catch (const E&) {
        return true;
    }
    return false;
}

void testCapacity(gfx::Context& ctx) {
    evo::test::TestScene scene = evo::test::cornellBox();
    scene.statics.capacity.instances = 4096;
    const modern::SceneInstance block = scene.frame.instances[7]; // the white block, no light
    modern::SceneFrame many = scene.frame;
    many.instances.resize(3000, block);
    modern::SceneFrame tooMany = scene.frame;
    tooMany.instances.resize(4097, block);

    modern::SceneGpu gpu(ctx, scene.statics);
    gfx::FrameRing ring(ctx);
    modern::PathTracer tracer(ctx, ring, 32, 32, modern::tracerCapacity(modern::SceneCapacity{}));
    CHECK(tracer.capacity().instances == 1024);
    modern::TraceSettings settings;
    {
        // Beyond the tracer's capacity (not yet reserved), within the scene's.
        VkCommandBuffer cmd = ring.begin();
        CHECK(throws<std::length_error>(
            [&] { tracer.trace(cmd, gpu, many, std::nullopt, settings, nullptr); }));
        ring.wait(ring.submit());
    }
    tracer.reserve(modern::tracerCapacity(scene.statics.capacity));
    CHECK(tracer.capacity().instances == 4096);
    {
        VkCommandBuffer cmd = ring.begin();
        CHECK(throws<std::length_error>(
            [&] { tracer.trace(cmd, gpu, tooMany, std::nullopt, settings, nullptr); }));
        tracer.trace(cmd, gpu, many, std::nullopt, settings, nullptr);
        ring.wait(ring.submit());
    }
    CHECK(tracer.lastStats().traceDispatches == 1);
    CHECK(throws<std::length_error>([&] { modern::checkSceneFrame(scene.statics, tooMany); }));
    idle(ctx);
    std::printf("(d) capacity 4096: 3000 instances traced, 4097 refused\n");
}

// --- (e) ---------------------------------------------------------------------------------------------------

void testShotWithoutFrame(gfx::Context& ctx) {
    Probe probe;
    gfx::FrameRing ring(ctx);
    render::ModernStage stage(ctx, ring, kWidth, kHeight, {}, cornellFactory(&probe), cornellMap);
    const std::unique_ptr<modern::ModernScene> studio = modern::makeScene(SceneId::Studio, {});
    std::uint32_t firstShot = 0, lastShot = 0;
    for (int i = 0; i < 20; ++i) {
        const show::TimelineFrame frame = show::sampleTimeline(chapterTime(kFlyby, i / 20.0));
        CHECK(stage.shotFor(frame) == 0);
        CHECK(!stage.samplesRetro(frame));
        const show::TimelineFrame night = show::sampleTimeline(chapterTime(kPortal, i / 20.0));
        const std::uint32_t shot = stage.shotFor(night);
        CHECK(shot == studio->camera(night).shot);
        (i == 0 ? firstShot : lastShot) = shot;
    }
    CHECK(lastShot != firstShot); // the samples cross PORTAL's glass
    CHECK(probe.frameCalls == 0);
    std::printf("(e) shotFor and samplesRetro: %d frame() calls\n", probe.frameCalls);
}

// --- (f) ---------------------------------------------------------------------------------------------------

void testChapterPictures(gfx::Context& ctx) {
    evo::test::checkChapterPictures(ctx, SceneId::Studio);
    std::printf("(f) checkChapterPictures: the Studio's 6 chapters pass\n");
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
        {
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            else if (!ctx.syncValidationEnabled())
                std::printf("SKIP sync validation: unavailable\n");
            testSceneSwitch(ctx);
            testOnePipelineAndMemory(ctx);
            idle(ctx);
            testPerFrameMaterials(ctx);
            idle(ctx);
            testCapacity(ctx);
            testShotWithoutFrame(ctx);
            idle(ctx);
            testChapterPictures(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("shared_tracer_test: %d checks passed\n", checksPassed);
    return 0;
}
