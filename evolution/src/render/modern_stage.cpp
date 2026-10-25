#include "render/modern_stage.hpp"

#include "gfx/gpu_timers.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene_gpu.hpp"
#include "post/denoiser.hpp"
#include "post/upscale.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace evo::render {

namespace {

// The placeholder's exposure: fill_hdr.comp inverts the tonemap at 0 EV.
constexpr float kPlaceholderExposureEV = 0.f;
// post::Denoiser's images per pixel: ten RGBA16F images (the colour, moments, guides, a-trous and volume
// ping-pong pairs, denoiser.cpp's createImages). The denoiser does not report its own size; keep this in
// step.
constexpr VkDeviceSize kDenoiserBytesPerPixel = 10 * 8;
// hdr_ (RGBA16F) and image_ (RGBA8).
constexpr VkDeviceSize kStageBytesPerPixel = 8 + 4;
// traceHdr_ (RGBA16F), below scale 1.
constexpr VkDeviceSize kTraceHdrBytesPerPixel = 8;
// The smallest trace size per axis (unless the output is smaller).
constexpr std::uint32_t kMinTraceSize = 16;
// The upscaler's history: how much of it survives a frame (UpscaleSettings::feedback) and its clip box.
constexpr float kUpscaleFeedback = 0.9f;
constexpr float kUpscaleClipGamma = 1.f;

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

float checkedScale(float scale) {
    if (!validTraceScale(scale))
        throw std::invalid_argument("ModernStage: the trace scale must be in (0, 1], got " +
                                    std::to_string(scale));
    return scale;
}

std::size_t indexOf(modern::SceneId id) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= modern::kSceneCount)
        throw std::out_of_range("ModernStage: no scene " + std::to_string(index));
    return index;
}

} // namespace

bool validTraceScale(float scale) {
    return std::isfinite(scale) && scale > 0.f && scale <= 1.f;
}

VkExtent2D traceSize(std::uint32_t width, std::uint32_t height, float scale) {
    const auto axis = [&](std::uint32_t size) {
        const auto scaled = static_cast<std::uint32_t>(std::lround(static_cast<double>(size) * scale));
        return std::min(size, std::max(kMinTraceSize, scaled));
    };
    return {axis(width), axis(height)};
}

ModernStage::ModernStage(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height,
                         modern::SceneDetail detail, modern::SceneFactory factory, modern::SceneMap map,
                         float traceScale)
    : ctx_(ctx), ring_(ring), width_(width), height_(height), traceScale_(checkedScale(traceScale)),
      traceWidth_(traceSize(width, height, traceScale).width),
      traceHeight_(traceSize(width, height, traceScale).height), detail_(detail),
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
    gpus_[index] = std::make_unique<modern::SceneGpu>(ctx_, statics);
    const modern::TracerCapacity capacity = modern::tracerCapacity(statics.capacity);
    if (!tracer_) {
        tracer_ = std::make_unique<modern::PathTracer>(ctx_, ring_, traceWidth_, traceHeight_, capacity);
        tracer_->setTimers(timers_);
        denoiser_ = std::make_unique<post::Denoiser>(ctx_, ring_, traceWidth_, traceHeight_);
        upscaler_ = std::make_unique<post::Upscaler>(ctx_, ring_, traceWidth_, traceHeight_, width_, height_);
        if (upscales())
            traceHdr_ = gfx::Image(ctx_, hdrDesc(traceWidth_, traceHeight_));
    } else {
        tracer_->reserve(capacity);
    }
}

void ModernStage::prepareAll() {
    for (std::size_t i = 0; i < show::kEffectCount; ++i)
        prepare(sceneOf(static_cast<show::Effect>(i)));
}

VkDeviceSize ModernStage::imageBytes() const {
    if (!tracer_)
        return 0;
    const VkDeviceSize pixels = VkDeviceSize(width_) * height_;
    const VkDeviceSize tracePixels = VkDeviceSize(traceWidth_) * traceHeight_;
    return tracer_->imageBytes() + kDenoiserBytesPerPixel * tracePixels +
           (upscales() ? kTraceHdrBytesPerPixel * tracePixels : 0) + upscaler_->imageBytes() +
           kStageBytesPerPixel * pixels;
}

VkDeviceSize ModernStage::gpuBytes() const {
    VkDeviceSize bytes = imageBytes();
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
    resizeTraced();
}

void ModernStage::setTraceScale(float scale) {
    checkedScale(scale);
    if (scale == traceScale_)
        return;
    traceScale_ = scale;
    resizeTraced();
}

void ModernStage::resizeTraced() {
    const VkExtent2D trace = traceSize(width_, height_, traceScale_);
    traceWidth_ = trace.width;
    traceHeight_ = trace.height;
    if (!tracer_)
        return;
    tracer_->resize(traceWidth_, traceHeight_);
    denoiser_->resize(traceWidth_, traceHeight_);
    upscaler_->resize(traceWidth_, traceHeight_, width_, height_);
    traceHdr_ = upscales() ? gfx::Image(ctx_, hdrDesc(traceWidth_, traceHeight_)) : gfx::Image();
    previousCamera_.reset();
}

void ModernStage::setTimers(gfx::GpuTimers* timers) {
    timers_ = timers;
    if (tracer_)
        tracer_->setTimers(timers);
}

void ModernStage::beginScope(VkCommandBuffer cmd, const char* name) {
    if (timers_)
        timers_->begin(cmd, name);
}

void ModernStage::endScope(VkCommandBuffer cmd) {
    if (timers_)
        timers_->end(cmd);
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
    lastJitter_ = {0.f, 0.f};
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
    beginScope(cmd, "fill");
    fill_.record(cmd, ring_, hdr_, params);
    endScope(cmd);
    beginScope(cmd, "tonemap");
    tonemap_.record(cmd, hdr_, image_, kPlaceholderExposureEV);
    endScope(cmd);
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

    // The window jitters its samples within each trace pixel below scale 1; a reset frame and exports do not.
    const post::SubpixelWindow window =
        post::subpixelWindow(traceWidth_, traceHeight_, width_, height_, state.framesSinceReset, interactive);
    modern::TraceSettings trace;
    trace.spp = state.spp;
    trace.maxBounces = state.maxBounces;
    trace.frameSeed = state.frameSeed;
    trace.clampIndirect = scene.clampIndirect;
    trace.pixelJitter = window.jitter;
    trace.pixelFootprint = window.footprint;
    lastJitter_ = window.jitter;
    tracer_->trace(cmd, *gpus_[indexOf(id)], scene, continues ? previousCamera_ : std::nullopt, trace,
                   retroImage);

    if (state.reset != HistoryReset::None) {
        denoiser_->resetHistory();
        upscaler_->resetHistory();
    }
    modern::GBuffer& g = tracer_->gbuffer();
    beginScope(cmd, "denoise");
    denoiser_->denoise(cmd, {g.lighting, g.emission, g.albedo, g.normal, g.depth, g.motion, g.volume},
                       denoiseSettingsFor(state), upscales() ? traceHdr_ : hdr_);
    endScope(cmd);
    if (upscales()) {
        post::UpscaleSettings upscale;
        upscale.temporal = interactive;
        upscale.feedback = kUpscaleFeedback;
        upscale.clipGamma = kUpscaleClipGamma;
        upscale.jitter = window.jitter;
        beginScope(cmd, "upscale");
        upscaler_->record(cmd, traceHdr_, g.depth, g.motion, upscale, hdr_, &g.emission);
        endScope(cmd);
    }
    beginScope(cmd, "tonemap");
    tonemap_.record(cmd, hdr_, image_, scene.exposureEV);
    endScope(cmd);
    previousCamera_ = scene.camera;
}

} // namespace evo::render
