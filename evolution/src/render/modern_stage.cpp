#include "render/modern_stage.hpp"

#include "gfx/gpu_timers.hpp"
#include "modern/caustics.hpp"
#include "modern/medium_volume.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene_gpu.hpp"
#include "modern/shutter.hpp"
#include "modern/splats.hpp"
#include "post/denoiser.hpp"
#include "post/look.hpp"
#include "post/upscale.hpp"
#include "rt/particles.hpp"
#include "rt/random.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace evo::render {

namespace {

// The placeholder's exposure: fill_hdr.comp inverts the tonemap at 0 EV.
constexpr float kPlaceholderExposureEV = 0.f;
// The smallest trace size per axis (unless the output is smaller).
constexpr std::uint32_t kMinTraceSize = 16;
// The window's fast mode (modern::TracerOptions::fast): a frame's caustic photons are its scene's / this,
// at least kFastMinPhotons.
constexpr std::uint32_t kFastPhotonDivisor = 4;
constexpr std::uint32_t kFastMinPhotons = 1024;
// The fast mode's scene pipelines: the features of a scene's frames, sampled this many seconds apart over its
// chapters.
constexpr double kFeatureProbeSeconds = 1.0;
// The upscaler's history: how much of it survives a frame (UpscaleSettings::feedback) and its clip box.
constexpr float kUpscaleFeedback = 0.9f;
constexpr float kUpscaleClipGamma = 1.f;

// Transfer source too: tests read the HDR picture back (splat_test).
gfx::ImageDesc hdrDesc(std::uint32_t width, std::uint32_t height) {
    return {width,
            height,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            1,
            "modern hdr"};
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
                         float traceScale, const modern::TracerOptions& tracerOptions)
    : ctx_(ctx), ring_(ring), width_(width), height_(height), traceScale_(checkedScale(traceScale)),
      traceWidth_(traceSize(width, height, traceScale).width),
      traceHeight_(traceSize(width, height, traceScale).height), detail_(detail),
      tracerOptions_(tracerOptions),
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
    // The one particle field, made for the first scene with a particle shader (grown below for a later one).
    const bool hasParticles = !statics.particleShader.empty();
    const std::uint32_t particleCapacity = std::max(statics.capacity.particles, 1u);
    std::unique_ptr<rt::ParticleField> particles;
    if (hasParticles && !particles_)
        particles =
            std::make_unique<rt::ParticleField>(ctx_, ring_, particleCapacity, statics.particleShader);
    // The splat pass with the first scene that has a splat shader, which it loads now.
    std::unique_ptr<modern::SplatPass> splats;
    if (!statics.splatShader.empty()) {
        if (!splats_)
            splats = std::make_unique<modern::SplatPass>(ctx_, ring_, traceWidth_, traceHeight_);
        (splats ? *splats : *splats_).prepare(statics.splatShader);
    }
    // The one CausticMaps, made for the first scene with caustics (grown below for a later one).
    const std::uint32_t causticResolution = statics.capacity.causticResolution;
    std::unique_ptr<modern::CausticMaps> caustics;
    if (causticResolution > 0 && !caustics_)
        caustics = std::make_unique<modern::CausticMaps>(ctx_, ring_, causticResolution);
    // The one medium volume, made for the first scene with a medium shader, grown for a later one.
    const bool hasMedium = !statics.mediumShader.empty();
    std::array<std::uint32_t, 3> mediumCapacity = statics.capacity.mediumGrid;
    for (std::uint32_t& n : mediumCapacity)
        n = std::max(n, 1u);
    std::unique_ptr<modern::MediumVolume> medium;
    if (hasMedium && !medium_)
        medium = std::make_unique<modern::MediumVolume>(ctx_, ring_, mediumCapacity, statics.mediumShader);
    if (!tracer_) {
        auto tracer = std::make_unique<modern::PathTracer>(ctx_, ring_, traceWidth_, traceHeight_, capacity,
                                                           tracerOptions_);
        tracer->setTimers(timers_);
        auto denoiser = std::make_unique<post::Denoiser>(ctx_, ring_, traceWidth_, traceHeight_);
        auto upscaler =
            std::make_unique<post::Upscaler>(ctx_, ring_, traceWidth_, traceHeight_, width_, height_);
        gfx::Image traceHdr =
            upscales() ? gfx::Image(ctx_, hdrDesc(traceWidth_, traceHeight_)) : gfx::Image();
        tracer_ = std::move(tracer);
        denoiser_ = std::move(denoiser);
        upscaler_ = std::move(upscaler);
        traceHdr_ = std::move(traceHdr);
    } else {
        tracer_->reserve(capacity); // keeps the old capacity if it throws
    }
    if (hasParticles && particles_) {
        particles_->setShader(statics.particleShader);
        particles_->reserve(particleCapacity); // keeps the old slots if it throws
    }
    if (particles)
        particles_ = std::move(particles);
    if (splats)
        splats_ = std::move(splats);
    if (causticResolution > 0 && caustics_)
        caustics_->reserve(causticResolution); // keeps the old maps if it throws
    if (caustics)
        caustics_ = std::move(caustics);
    if (hasMedium && medium_) {
        medium_->setShader(statics.mediumShader);
        medium_->reserve(mediumCapacity); // keeps the old images if it throws
    }
    if (medium)
        medium_ = std::move(medium);
    gpus_[index] = std::move(gpu);
    // The window's fast mode: the scene's own pipeline, with only the features its frames use (a frame that
    // needs more, which the probe missed, traces with the generic pipeline). EVO_SCENE_PIPELINES=0 keeps the
    // generic one (a diagnostic, to compare the two).
    const char* scenePipelines = std::getenv("EVO_SCENE_PIPELINES");
    if (tracerOptions_.fast && !(scenePipelines && std::string(scenePipelines) == "0"))
        tracer_->preparePipeline(sceneFeatures(id));
}

std::uint32_t ModernStage::sceneFeatures(modern::SceneId id) const {
    const modern::ModernScene& object = scene(id);
    std::uint32_t features = 0;
    for (int chapter = 0; chapter < show::chapterCount(); ++chapter) {
        const double start = show::chapterStart(chapter);
        const double end =
            chapter + 1 < show::chapterCount() ? show::chapterStart(chapter + 1) : show::showDuration();
        if (sceneOf(show::sampleTimeline(start).effect) != id)
            continue;
        for (double t = start; t < end; t += kFeatureProbeSeconds)
            features |= modern::traceFeatures(object.staticScene(), object.frame(show::sampleTimeline(t)));
    }
    return features;
}

void ModernStage::prepareAll() {
    for (std::size_t i = 0; i < show::kEffectCount; ++i)
        prepare(sceneOf(static_cast<show::Effect>(i)));
}

VkDeviceSize ModernStage::imageBytes() const {
    if (!tracer_)
        return 0;
    return tracer_->imageBytes() + denoiser_->imageBytes() + traceHdr_.bytes() + upscaler_->imageBytes() +
           (splats_ ? splats_->imageBytes() : 0) + hdr_.bytes() + image_.bytes() + tonemap_.bloomBytes();
}

VkDeviceSize ModernStage::gpuBytes() const {
    if (!tracer_)
        return 0;
    VkDeviceSize bytes = imageBytes() + tracer_->bufferBytes() + (particles_ ? particles_->bytes() : 0) +
                         (caustics_ ? caustics_->bytes() : 0) + (medium_ ? medium_->bytes() : 0);
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
    if (splats_)
        splats_->resize(traceWidth_, traceHeight_);
    previousCamera_.reset();
    previousInstances_.clear();
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

std::string ModernStage::nowCard(show::Effect effect) const {
    const modern::SceneId id = sceneOf(effect);
    return id == modern::SceneId::Placeholder ? std::string() : scene(id).nowCard(effect);
}

modern::Counters ModernStage::takeCounters() {
    return tracer_ ? tracer_->takeCounters() : modern::Counters{};
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
    frameStats_ = {};
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
    const bool interactive = state.mode == RenderMode::Interactive;
    const bool continues = interactive && state.reset == HistoryReset::None;

    // The export shutter: the scene's frame at each slice's time, clamped into the frame's chapter. The
    // window (and an export at 4 spp or fewer, or without a shutter) has one slice: the frame itself.
    const std::uint32_t sliceCount =
        modern::shutterSlices(state.spp, interactive ? 0.0 : state.shutterSeconds);
    std::vector<modern::SceneFrame> slices;
    std::vector<double> sliceLocal; // each slice's chapter-local time
    slices.reserve(sliceCount);
    modern::TraceSettings trace;
    for (std::uint32_t k = 0; k < sliceCount; ++k) {
        if (sliceCount == 1) {
            slices.push_back(object.frame(frame));
            sliceLocal.push_back(frame.localSeconds);
            continue;
        }
        const double local = modern::sliceSeconds(frame.localSeconds, frame.chapterDuration, k, sliceCount,
                                                  state.shutterSeconds);
        slices.push_back(object.frame(modern::sliceFrame(frame, local)));
        sliceLocal.push_back(local);
        trace.sliceOffsets[k] = static_cast<float>(local - frame.localSeconds);
    }
    const std::uint32_t guideSlice = sliceCount / 2; // the slice the guides (and the next motion) come from
    const modern::SceneFrame& scene = slices[guideSlice];

    // The window jitters its samples within each trace pixel below scale 1; a reset frame and exports do not.
    const post::SubpixelWindow window =
        post::subpixelWindow(traceWidth_, traceHeight_, width_, height_, state.framesSinceReset, interactive);
    trace.spp = state.spp;
    trace.maxBounces = state.maxBounces;
    trace.frameSeed = state.frameSeed;
    trace.clampIndirect = scene.clampIndirect;
    trace.seconds = static_cast<float>(frame.localSeconds);
    trace.pixelJitter = window.jitter;
    trace.pixelFootprint = window.footprint;
    lastJitter_ = window.jitter;
    modern::TraceExtras extras;
    // The previous frame's instances, while the history continues: the previous transforms of the motion
    // guide.
    if (continues)
        extras.previousInstances = previousInstances_;
    const modern::SceneStatic& statics = object.staticScene();
    const bool particles = std::any_of(slices.begin(), slices.end(),
                                       [](const modern::SceneFrame& s) { return s.particles.has_value(); });
    if (particles) {
        // The particle field is rebuilt per slice, at the slice's time, just before that slice's TLAS build
        // (each slice is checked with the rest of its frame before anything is recorded).
        for (const modern::SceneFrame& s : slices)
            modern::checkSceneFrame(statics, s);
        particles_->setShader(statics.particleShader);
        extras.particleSpheres = &particles_->records();
        extras.particleBlas = particles_->blasAddress();
    }
    const auto recordParticles = [&](VkCommandBuffer commands, std::uint32_t k) {
        if (!slices[k].particles)
            return;
        const modern::ParticleFrame& p = *slices[k].particles;
        if (sliceCount == 1)
            beginScope(commands, "particles");
        particles_->record(commands, p.count, sliceLocal[k], continues ? previousSeconds_ : sliceLocal[k],
                           p.params, p.material);
        if (sliceCount == 1)
            endScope(commands);
    };
    if (slices[0].caustics) {
        // Caustics: this frame's photon maps (the first slice's), a pure function of the show time (the seed
        // does not follow the frames since a reset), with kExportPhotonFactor times the photons in an export
        // of many samples.
        if (!caustics_)
            throw std::invalid_argument("ModernStage: a frame with caustics from a scene whose "
                                        "capacity.causticResolution is 0");
        // Such exports also resolve the maps at kExportMapFactor times the receivers' resolution, where the maps
        // hold it.
        const bool fine = state.mode == RenderMode::Export && state.spp >= modern::kExportPhotonSpp;
        // The window's fast mode: a quarter of the photons, from one seed per chapter (the maps' grain stands
        // still instead of flickering, and maps of unchanged inputs are kept, CausticMaps::prepare).
        const bool fast = tracerOptions_.fast && interactive;
        const std::uint32_t scenePhotons = slices[0].caustics->photons;
        const std::uint32_t photons =
            fast ? std::min(scenePhotons, std::max(kFastMinPhotons, scenePhotons / kFastPhotonDivisor))
                 : scenePhotons * (fine ? modern::kExportPhotonFactor : 1u);
        const std::uint32_t seed =
            fast ? rt::pcgHash(0x43415553u ^ (0x9E3779B9u * static_cast<std::uint32_t>(frame.chapter + 1)))
                 : rt::pcgHash(modern::frameSeedFor(frame.showSeconds, 0) ^ 0x43415553u);
        const modern::SceneFrame* causticFrame = &slices[0];
        modern::SceneFrame fineFrame;
        if (fine) {
            fineFrame = slices[0];
            fineFrame.caustics = modern::exportCaustics(*slices[0].caustics, caustics_->maxResolution());
            causticFrame = &fineFrame;
        }
        caustics_->prepare(object.staticScene(), *causticFrame, photons, seed, fast);
        if (!caustics_->reused()) {
            frameStats_.photons = photons;
            for (const modern::CausticReceiver& r : causticFrame->caustics->receivers)
                frameStats_.causticTexels += r.resolution * r.resolution;
        }
        extras.causticMaps = &caustics_->maps();
        extras.causticReceivers = &caustics_->receiverTable();
        extras.photons = [this](VkCommandBuffer c, const modern::PhotonScene& p) { caustics_->record(c, p); };
    }
    const modern::SceneFrame& middle = slices[sliceCount / 2];
    if (middle.medium && middle.medium->grid) {
        // Media: the grid medium's bake, once per frame at the middle slice's time (a pure function of it:
        // nothing carries over from another frame), checked with the rest of that slice first. Every slice
        // samples it.
        const modern::SceneStatic& statics = object.staticScene();
        modern::checkSceneFrame(statics, middle);
        medium_->setShader(statics.mediumShader);
        beginScope(cmd, "medium");
        medium_->record(cmd, *middle.medium->grid, sliceLocal[sliceCount / 2]);
        frameStats_.gridVoxels = middle.medium->grid->size[0] * middle.medium->grid->size[1] *
                                 middle.medium->grid->size[2];
        endScope(cmd);
        extras.density = &medium_->density();
        extras.majorant = &medium_->majorant();
    }
    // An export may submit what the frame recorded so far before each slice (the tracer binds its pipeline
    // and descriptors again after the callback), so no submit holds more than one slice's trace.
    const bool split = state.submitPerSlice && !interactive;
    const auto beforeSlice = [&](VkCommandBuffer commands, std::uint32_t k) {
        if (split)
            ring_.split(); // the same command buffer records on
        recordParticles(commands, k);
    };
    tracer_->trace(cmd, *gpus_[indexOf(id)], slices, continues ? previousCamera_ : std::nullopt, trace,
                   retroImage, extras,
                   particles || split ? modern::PathTracer::SliceCallback(beforeSlice) : nullptr);

    if (state.reset != HistoryReset::None) {
        denoiser_->resetHistory();
        upscaler_->resetHistory();
    }
    modern::GBuffer& g = tracer_->gbuffer();
    // The splats add into the emission guide, after the trace and before the denoiser (the tracer checked
    // them against the scene's capacity and shader, so the pass exists).
    if (scene.splats && scene.splats->count > 0) {
        beginScope(cmd, "splats");
        // At the guide slice's time, with its camera and depth: with a shutter the splats are not blurred.
        splats_->record(cmd, object.staticScene().splatShader, *scene.splats, scene.camera, g.depth,
                        g.emission, sliceLocal[guideSlice], interactive ? scene.splats->windowFootprint : 5u);
        endScope(cmd);
    }
    beginScope(cmd, "denoise");
    post::DenoiseSettings denoise = denoiseSettingsFor(state);
    denoise.hasMedium = scene.medium.has_value(); // without one the volume layer is (0, 0, 0, 1): skip it
    denoiser_->denoise(cmd, {g.lighting, g.emission, g.albedo, g.normal, g.depth, g.motion, g.volume},
                       denoise, upscales() ? traceHdr_ : hdr_);
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
    // The look (post::Look): the act's grade and grain, the authored exposure, the bloom, the palette grade
    // after a crossing.
    const post::Look look = post::lookFor(frame);
    post::ToneInputs tone;
    tone.exposureEV = scene.exposureEV + look.exposureOffset;
    tone.grade = look.grade;
    tone.grade.vignette = scene.vignette;
    if (tone.grade.palette > 0.f)
        tone.palette = &look.palette;
    if (look.bloom > 0.f) {
        beginScope(cmd, "bloom");
        tone.bloom = &tonemap_.recordBloom(cmd, hdr_, scene.anamorphic);
        tone.bloomWeight = look.bloom;
        endScope(cmd);
    }
    beginScope(cmd, "tonemap");
    tonemap_.record(cmd, hdr_, image_, tone);
    endScope(cmd);
    frameStats_.instances = static_cast<std::uint32_t>(scene.instances.size());
    frameStats_.rectLights = static_cast<std::uint32_t>(scene.lights.size());
    frameStats_.portals = static_cast<std::uint32_t>(
        std::count_if(scene.lights.begin(), scene.lights.end(),
                      [](const rt::RectLight& l) { return l.kind == static_cast<std::uint32_t>(rt::LightKind::Portal); }));
    frameStats_.treeLights = static_cast<std::uint32_t>(scene.treeLights.size());
    for (const modern::SceneInstance& in : scene.instances) {
        const std::vector<rt::Material>& materials =
            scene.materials.empty() ? object.staticScene().materials : scene.materials;
        const rt::Material& m = materials[in.material];
        frameStats_.thinPanes = frameStats_.thinPanes ||
                                ((m.flags & rt::kMaterialThinWalled) != 0 && m.transmission > 0.5f);
    }
    frameStats_.fog = scene.medium && scene.medium->fog.has_value();
    frameStats_.lensing = scene.lensing.has_value();
    for (const modern::SceneFrame& s : slices)
        frameStats_.particles += s.particles ? s.particles->count : 0u;
    frameStats_.splats = scene.splats ? scene.splats->count : 0u;
    frameStats_.slices = sliceCount;
    frameStats_.spp = state.spp;
    frameStats_.maxBounces = state.maxBounces;
    frameStats_.traceWidth = traceWidth_;
    frameStats_.pipelineFeatures = tracer_->lastPipelineFeatures();
    if (tracerOptions_.counters) {
        const gfx::RayTracingPipeline::StackSizes stack = tracer_->stackSizes();
        frameStats_.raygenStack = stack.raygen;
        frameStats_.hitStack = std::max({stack.closestHit, stack.intersection, stack.miss});
    }
    frameStats_.traceHeight = traceHeight_;
    previousCamera_ = scene.camera;
    previousInstances_ = scene.instances;
    previousSeconds_ = sliceLocal[guideSlice];
}

} // namespace evo::render
