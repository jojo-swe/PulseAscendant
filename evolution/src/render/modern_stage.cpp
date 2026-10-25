#include "render/modern_stage.hpp"

#include "modern/path_tracer.hpp"
#include "modern/scene_gpu.hpp"
#include "post/denoiser.hpp"

#include <stdexcept>
#include <string>
#include <utility>

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

std::size_t indexOf(modern::SceneId id) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= modern::kSceneCount)
        throw std::out_of_range("ModernStage: no scene " + std::to_string(index));
    return index;
}

} // namespace

ModernStage::ModernStage(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height,
                         modern::SceneDetail detail, modern::SceneFactory factory, modern::SceneMap map)
    : ctx_(ctx), ring_(ring), width_(width), height_(height), detail_(detail),
      factory_(factory ? std::move(factory) : modern::SceneFactory(&modern::makeScene)),
      map_(map ? std::move(map) : modern::SceneMap(&modern::sceneFor)), fill_(ctx, gfx::FillTarget::Hdr),
      tonemap_(ctx, ring), hdr_(ctx, hdrDesc(width, height)), image_(ctx, imageDesc(width, height)) {}

ModernStage::~ModernStage() = default;

const modern::ModernScene& ModernStage::scene(modern::SceneId id) const {
    std::unique_ptr<modern::ModernScene>& slot = scenes_[indexOf(id)];
    if (!slot) {
        if (id == modern::SceneId::Placeholder)
            throw std::logic_error("ModernStage: the placeholder is no path-traced scene");
        slot = factory_(id, detail_);
        if (!slot)
            throw std::invalid_argument(std::string("ModernStage: no scene object for ") +
                                        modern::sceneName(id));
    }
    return *slot;
}

void ModernStage::prepare(modern::SceneId id) {
    const std::size_t index = indexOf(id);
    if (id == modern::SceneId::Placeholder || gpus_[index])
        return;
    // setup: the scene's geometry, BLASes and materials (SceneGpu waits for its uploads), then the shared
    // tracer and denoiser on the first scene, or the tracer's buffers grown to this scene's capacity.
    const modern::SceneStatic& statics = scene(id).staticScene();
    // Everything is made before anything is stored, so a constructor that throws (a missing shader, out of
    // memory) leaves the stage as it was: never a prepared scene without a tracer, or a denoiser without one.
    auto gpu = std::make_unique<modern::SceneGpu>(ctx_, statics);
    const modern::TracerCapacity capacity = modern::tracerCapacity(statics.capacity);
    if (!tracer_) {
        auto tracer = std::make_unique<modern::PathTracer>(ctx_, ring_, width_, height_, capacity);
        auto denoiser = std::make_unique<post::Denoiser>(ctx_, ring_, width_, height_);
        tracer_ = std::move(tracer);
        denoiser_ = std::move(denoiser);
    } else {
        tracer_->reserve(capacity); // keeps the old capacity if it throws
    }
    gpus_[index] = std::move(gpu);
}

void ModernStage::prepareAll() {
    for (std::size_t i = 0; i < show::kEffectCount; ++i)
        prepare(sceneOf(static_cast<show::Effect>(i)));
}

VkDeviceSize ModernStage::imageBytes() const {
    if (!tracer_)
        return 0;
    return tracer_->imageBytes() + denoiser_->imageBytes() + hdr_.bytes() + image_.bytes();
}

VkDeviceSize ModernStage::gpuBytes() const {
    if (!tracer_)
        return 0;
    VkDeviceSize bytes = imageBytes() + tracer_->bufferBytes();
    for (const auto& gpu : gpus_)
        if (gpu)
            bytes += gpu->bytes();
    return bytes;
}

void ModernStage::resize(std::uint32_t width, std::uint32_t height) {
    width_ = width;
    height_ = height;
    hdr_ = gfx::Image(ctx_, hdrDesc(width, height));
    image_ = gfx::Image(ctx_, imageDesc(width, height));
    if (tracer_) {
        tracer_->resize(width, height);
        denoiser_->resize(width, height);
        previousCamera_.reset();
    }
}

modern::SceneId ModernStage::sceneOf(show::Effect effect) const {
    return map_(effect);
}

std::uint32_t ModernStage::shotFor(const show::TimelineFrame& frame) const {
    const modern::SceneId id = sceneOf(frame.effect);
    return id == modern::SceneId::Placeholder ? 0u : scene(id).camera(frame).shot;
}

bool ModernStage::samplesRetro(const show::TimelineFrame& frame) const {
    const modern::SceneId id = sceneOf(frame.effect);
    return id != modern::SceneId::Placeholder && scene(id).samplesRetro();
}

modern::PathTracer* ModernStage::tracer() {
    return tracer_.get();
}

const modern::SceneGpu* ModernStage::sceneGpu(modern::SceneId id) const {
    return gpus_[indexOf(id)].get();
}

void ModernStage::render(VkCommandBuffer cmd, const show::TimelineFrame& frame, const HistoryState& state,
                         gfx::Image* retroImage) {
    const modern::SceneId id = sceneOf(frame.effect);
    if (id != modern::SceneId::Placeholder) {
        renderTraced(cmd, id, frame, state, retroImage);
        return;
    }
    gfx::FillParams params;
    params.localSeconds = static_cast<float>(frame.localSeconds);
    params.era = 1.0f;     // the 2026 look over the whole image
    params.transition = 0; // Cut: no wipe split, no cross-fade
    params.kick = frame.kick;
    params.beatPhase = frame.beatPhase;
    params.chapter = static_cast<std::uint32_t>(frame.chapter);
    fill_.record(cmd, ring_, hdr_, params);
    tonemap_.record(cmd, hdr_, image_, kPlaceholderExposureEV);
}

void ModernStage::renderTraced(VkCommandBuffer cmd, modern::SceneId id, const show::TimelineFrame& frame,
                               const HistoryState& state, gfx::Image* retroImage) {
    const modern::ModernScene& object = scene(id);
    if (object.samplesRetro() && retroImage == nullptr)
        throw std::invalid_argument("ModernStage: a scene that shows the retro screen needs the retro image");
    prepare(id); // setup on the first frame of an unprepared scene (see the class comment)
    const modern::SceneFrame scene = object.frame(frame);
    const bool interactive = state.mode == RenderMode::Interactive;
    const bool continues = interactive && state.reset == HistoryReset::None;

    modern::TraceSettings trace;
    trace.spp = state.spp;
    trace.maxBounces = state.maxBounces;
    trace.frameSeed = state.frameSeed;
    trace.clampIndirect = scene.clampIndirect;
    trace.seconds = static_cast<float>(frame.localSeconds);
    tracer_->trace(cmd, *gpus_[indexOf(id)], scene, continues ? previousCamera_ : std::nullopt, trace,
                   retroImage);

    if (state.reset != HistoryReset::None)
        denoiser_->resetHistory();
    modern::GBuffer& g = tracer_->gbuffer();
    denoiser_->denoise(cmd, {g.lighting, g.emission, g.albedo, g.normal, g.depth, g.motion, g.volume},
                       denoiseSettingsFor(state), hdr_);
    tonemap_.record(cmd, hdr_, image_, scene.exposureEV);
    previousCamera_ = scene.camera;
}

} // namespace evo::render
