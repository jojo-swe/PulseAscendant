#include "render/renderer.hpp"

#include "modern/path_tracer.hpp"
#include "show/director.hpp"
#include "text/font8x8.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>

namespace evo::render {

namespace {

// Checked before any member is built from the size.
const RenderSettings& checked(const RenderSettings& settings) {
    if (settings.width == 0 || settings.height == 0)
        throw std::invalid_argument("the render size must be positive, got " +
                                    std::to_string(settings.width) + "x" + std::to_string(settings.height));
    if (settings.spp == 0)
        throw std::invalid_argument("the samples per pixel must be positive");
    if (!validTraceScale(settings.traceScale))
        throw std::invalid_argument("the trace scale must be in (0, 1], got " +
                                    std::to_string(settings.traceScale));
    if (!(settings.shutterSeconds >= 0.0) || !std::isfinite(settings.shutterSeconds))
        throw std::invalid_argument("the shutter must be a finite number of seconds, 0 or more, got " +
                                    std::to_string(settings.shutterSeconds));
    return settings;
}

constexpr std::uint32_t kTitleColor = 0xFFFFFFFFu;
constexpr std::uint32_t kShadowColor = 0xFF000000u;
// GPU timer scopes per frame: total, retro, fill or tlas and trace, denoise, upscale, tonemap, compositor.
constexpr std::uint32_t kMaxGpuScopes = 16;

} // namespace

int hudUnit(std::uint32_t height) {
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(height) / 360.0)));
}

std::vector<text::Item> hudItems(const show::TimelineFrame& frame, std::uint32_t width, std::uint32_t height,
                                 bool title) {
    static_cast<void>(width);
    std::vector<text::Item> items;
    if (!title || frame.title == nullptr || *frame.title == '\0')
        return items;
    const int unit = hudUnit(height);
    const auto margin = static_cast<float>(kHudMarginUnits * unit);
    const auto shadow = static_cast<float>(unit);
    items.push_back({margin + shadow, margin + shadow, shadow, kShadowColor, frame.title});
    items.push_back({margin, margin, shadow, kTitleColor, frame.title});
    return items;
}

Renderer::Renderer(gfx::Context& ctx, gfx::FrameRing& ring, const RenderSettings& settings)
    : ring_(ring), timers_(ctx, ring, kMaxGpuScopes), crt_(checked(settings).crt), hud_(settings.hud),
      hudTitle_(settings.hudTitle), mode_(settings.mode), spp_(settings.spp),
      maxBounces_(resolveMaxBounces(settings.mode, settings.maxBounces)),
      shutterSeconds_(settings.mode == RenderMode::Export ? settings.shutterSeconds : 0.0),
      submitPerSlice_(settings.mode == RenderMode::Export && settings.submitPerSlice), text_(ctx, ring),
      retro_(ctx, ring, text_), modern_(ctx, ring, settings.width, settings.height, settings.detail,
                                        settings.sceneFactory, settings.sceneMap, settings.traceScale),
      compositor_(ctx, ring, text_, settings.width, settings.height),
      cards_(ctx, ring, [this](show::Effect effect) { return modern_.nowCard(effect); }) {
    modern_.setTimers(&timers_);
}

Renderer::~Renderer() {
    // The frames in flight still use the images and pipelines. Best effort, as in ~FrameRing: a wait that
    // fails (a lost device) has nothing left to protect.
    try {
        ring_.waitAll(); // shutdown
    } catch (const std::exception&) {
    }
}

void Renderer::resize(std::uint32_t width, std::uint32_t height) {
    checked({width, height});
    const VkExtent2D current = output().extent();
    if (width == current.width && height == current.height)
        return;
    ring_.waitAll(); // resize: the frames in flight may still read the old images
    modern_.resize(width, height);
    compositor_.resize(width, height);
    history_.markResize(); // reported even if the size is back to the last frame's when the stage runs
}

void Renderer::setTraceScale(float scale) {
    if (!validTraceScale(scale))
        throw std::invalid_argument("the trace scale must be in (0, 1], got " + std::to_string(scale));
    if (scale == modern_.traceScale())
        return;
    ring_.waitAll(); // resize: a trace-scale change, the frames in flight may still read the old images
    modern_.setTraceScale(scale);
    history_.markResize(); // the next frame of the modern stage resets (Resize)
}

void Renderer::prepare(std::span<const double> showSeconds) {
    std::array<bool, modern::kSceneCount> done{};
    for (const double seconds : showSeconds) {
        // The scene the frame shows: a megamix or REBOOT frame's cut, else its own chapter's.
        const show::TimelineFrame frame = show::sampleTimeline(seconds);
        const std::optional<show::SceneCut> cut = show::sceneCut(frame);
        const show::Effect effect =
            cut ? show::cues[static_cast<std::size_t>(cut->chapter)].effect : frame.effect;
        const modern::SceneId id = modern_.sceneOf(effect);
        bool& prepared = done[static_cast<std::size_t>(id)];
        if (!prepared)
            modern_.prepare(id);
        prepared = true;
    }
}

void Renderer::render(VkCommandBuffer cmd, const show::TimelineFrame& frame) {
    // The 2026 side renders the frame's scene cut, if any (show/director.hpp): its scene, camera shot, scene
    // time and so its history and seed. The 1993 side, the compositor and the HUD follow the frame itself.
    const show::TimelineFrame scene = show::sceneFrame(frame);
    const StagePlan plan = planStages(frame, mode_, modern_.samplesRetro(scene));
    const VkExtent2D size = output().extent();
    stats_.gpuMs = timers_.lastCompleted(); // read by the ring's begin() for this frame's slot
    timers_.begin(cmd, "total");

    if (plan.retro)
        timers_.begin(cmd, "retro");
    stats_.retroRendered = plan.retro && retro_.render(cmd, frame);
    if (plan.retro)
        timers_.end(cmd);
    stats_.retroFrame = plan.retro ? retro_.retroFrame() : 0u;

    stats_.modernRendered = plan.modern;
    stats_.historyReset = HistoryReset::None;
    stats_.frameSeed = 0;
    stats_.spp = 0;
    stats_.traceJitter = {0.f, 0.f};
    if (plan.modern) {
        // A cut changes the chapter (or the scene time jumps), so it resets the history like any chapter
        // change: the window shows the reset frame's samples, an export the frame at the cut's scene time.
        const HistoryKey key{
            scene.chapter, modern_.shotFor(scene), scene.showSeconds, size.width, size.height, mode_, spp_};
        stats_.historyReset = history_.advance(key, !modernRanLastFrame_);
        // Export frames stand alone: the seed depends on the scene time only, not on the frames before.
        const std::uint32_t seedFrames = mode_ == RenderMode::Export ? 0u : history_.framesSinceReset();
        const HistoryState state{stats_.historyReset,
                                 history_.framesSinceReset(),
                                 mode_,
                                 frameSpp(mode_, spp_, history_.framesSinceReset()),
                                 maxBounces_,
                                 modern::frameSeedFor(scene.showSeconds, seedFrames),
                                 shutterSeconds_,
                                 submitPerSlice_};
        modern_.render(cmd, scene, state, plan.retro ? &retro_.image() : nullptr);
        stats_.frameSeed = state.frameSeed;
        stats_.spp = state.spp;
        stats_.traceJitter = modern_.lastTraceJitter();
    }
    stats_.framesSinceReset = plan.modern ? history_.framesSinceReset() : 0u;
    modernRanLastFrame_ = plan.modern;

    const std::uint32_t visible =
        (plan.retroVisible ? kRetroVisible : 0u) | (plan.modernVisible ? kModernVisible : 0u);
    const std::vector<text::Item> hud =
        hud_ ? hudItems(frame, size.width, size.height, hudTitle_) : std::vector<text::Item>{};
    timers_.begin(cmd, "compositor");
    compositor_.render(cmd, retro_.image(), modern_.image(), frame, crt_, visible, hud);
    if (hud_)
        cards_.record(cmd, compositor_.output(), frame, crt_);
    timers_.end(cmd);
    timers_.end(cmd); // total
}

} // namespace evo::render
