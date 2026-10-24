#include "render/modern_stage.hpp"

#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "post/denoiser.hpp"

#include <optional>
#include <stdexcept>

namespace evo::render {

namespace {

// The placeholder's exposure: fill_hdr.comp inverts the tonemap at 0 EV.
constexpr float kPlaceholderExposureEV = 0.f;

gfx::ImageDesc hdrDesc(std::uint32_t width, std::uint32_t height) {
    return {width, height, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, 1, "modern hdr"};
}

// Transfer source too: tests read the 2026 picture back on its own.
gfx::ImageDesc imageDesc(std::uint32_t width, std::uint32_t height) {
    return {width,
            height,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            1,
            "modern"};
}

// MERGE(step 5 Task 1): gone once the tracer writes GBuffer::volume. The volume layer of a scene without a
// medium, (0, 0, 0, 1) everywhere: no in-scattered light, full transmittance.
gfx::Image noMediumImage(gfx::Context& ctx, std::uint32_t width, std::uint32_t height) {
    gfx::Image image(ctx, {width, height, VK_FORMAT_R16G16B16A16_SFLOAT,
                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 1, "no medium"});
    ctx.immediate([&](VkCommandBuffer cmd) { // setup
        image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const VkClearColorValue clear{{0.f, 0.f, 0.f, 1.f}};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    });
    return image;
}

} // namespace

struct ModernStage::Traced {
    std::unique_ptr<modern::ModernScene> scene;
    // Built on first use. The denoiser and the tracer each wait for the ring when they go.
    std::optional<modern::PathTracer> tracer;
    std::optional<post::Denoiser> denoiser;
    // MERGE(step 5 Task 1): replaced by the tracer's GBuffer::volume. No medium: (0, 0, 0, 1).
    std::optional<gfx::Image> noMedium;
    std::optional<modern::Camera> previousCamera; // the last frame's, while the history continues
};

ModernStage::ModernStage(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height)
    : ctx_(ctx), ring_(ring), width_(width), height_(height), fill_(ctx, gfx::FillTarget::Hdr),
      tonemap_(ctx, ring), hdr_(ctx, hdrDesc(width, height)), image_(ctx, imageDesc(width, height)),
      studio_(std::make_unique<Traced>()) {
    studio_->scene = modern::makeScene(modern::SceneId::Studio);
}

ModernStage::~ModernStage() = default;

ModernStage::Traced& ModernStage::traced(modern::SceneId id) {
    if (id != modern::SceneId::Studio)
        throw std::logic_error("ModernStage: no path-traced scene for this id");
    Traced& t = *studio_;
    if (!t.tracer) {
        // setup: the BLASes, pipeline and images, once per scene (PathTracer waits for its uploads)
        t.tracer.emplace(ctx_, ring_, t.scene->staticScene(), width_, height_);
        t.denoiser.emplace(ctx_, ring_, width_, height_);
        t.noMedium.emplace(noMediumImage(ctx_, width_, height_));
    }
    return t;
}

void ModernStage::prepare() {
    traced(modern::SceneId::Studio);
}

void ModernStage::resize(std::uint32_t width, std::uint32_t height) {
    width_ = width;
    height_ = height;
    hdr_ = gfx::Image(ctx_, hdrDesc(width, height));
    image_ = gfx::Image(ctx_, imageDesc(width, height));
    if (studio_->tracer) {
        studio_->tracer->resize(width, height);
        studio_->denoiser->resize(width, height);
        studio_->noMedium.emplace(noMediumImage(ctx_, width, height));
        studio_->previousCamera.reset();
    }
}

std::uint32_t ModernStage::shotFor(const show::TimelineFrame& frame) const {
    if (modern::sceneFor(frame.effect) == modern::SceneId::Studio)
        return studio_->scene->frame(frame).camera.shot;
    return 0;
}

bool ModernStage::samplesRetro(const show::TimelineFrame& frame) const {
    return modern::sceneFor(frame.effect) == modern::SceneId::Studio && studio_->scene->samplesRetro();
}

modern::PathTracer* ModernStage::studioTracer() {
    return studio_->tracer ? &*studio_->tracer : nullptr;
}

void ModernStage::render(VkCommandBuffer cmd, const show::TimelineFrame& frame, const HistoryState& state,
                         gfx::Image* retroImage) {
    switch (modern::sceneFor(frame.effect)) {
    case modern::SceneId::Placeholder: {
        gfx::FillParams params;
        params.localSeconds = static_cast<float>(frame.localSeconds);
        params.era = 1.0f;     // the 2026 look over the whole image
        params.transition = 0; // Cut: no wipe split, no cross-fade
        params.kick = frame.kick;
        params.beatPhase = frame.beatPhase;
        params.chapter = static_cast<std::uint32_t>(frame.chapter);
        fill_.record(cmd, ring_, hdr_, params);
        tonemap_.record(cmd, hdr_, image_, kPlaceholderExposureEV);
        break;
    }
    case modern::SceneId::Studio:
        renderTraced(cmd, traced(modern::SceneId::Studio), frame, state, retroImage);
        break;
    }
}

void ModernStage::renderTraced(VkCommandBuffer cmd, Traced& t, const show::TimelineFrame& frame,
                               const HistoryState& state, gfx::Image* retroImage) {
    if (retroImage == nullptr)
        throw std::invalid_argument("ModernStage: a path-traced scene needs the retro image");
    const modern::SceneFrame scene = t.scene->frame(frame);
    const bool interactive = state.mode == RenderMode::Interactive;
    const bool continues = interactive && state.reset == HistoryReset::None;

    modern::TraceSettings trace;
    trace.spp = state.spp;
    trace.maxBounces = state.maxBounces;
    trace.frameSeed = state.frameSeed;
    trace.clampIndirect = scene.clampIndirect;
    t.tracer->trace(cmd, scene, continues ? t.previousCamera : std::nullopt, trace, *retroImage);

    if (state.reset != HistoryReset::None)
        t.denoiser->resetHistory();
    modern::GBuffer& g = t.tracer->gbuffer();
    t.denoiser->denoise(cmd, {g.lighting, g.emission, g.albedo, g.normal, g.depth, g.motion, *t.noMedium},
                        denoiseSettingsFor(state), hdr_);
    tonemap_.record(cmd, hdr_, image_, scene.exposureEV);
    t.previousCamera = scene.camera;
}

} // namespace evo::render
