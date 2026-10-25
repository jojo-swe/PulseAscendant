#include "render/stage_plan.hpp"

#include "show/director.hpp"

#include <algorithm>

namespace evo::render {

StagePlan planStages(const show::TimelineFrame& frame, RenderMode mode, bool modernSamplesRetro) {
    StagePlan plan{};
    if (show::holdsModern(frame)) { // FINALE: the 1993 picture is on the Studio's CRT, never the frame
        plan.modernVisible = plan.modern = true;
        plan.retro = modernSamplesRetro;
        return plan;
    }
    switch (frame.transition) {
    case show::Transition::Portal:
        plan.modernVisible = frame.era > 0.f;
        plan.retroVisible = frame.era < 1.f;
        break;
    case show::Transition::Wipe:
        plan.modernVisible = frame.wipe > 0.f;
        plan.retroVisible = frame.wipe < 1.f;
        break;
    case show::Transition::Cut:
    case show::Transition::BeatSwap:
        plan.modernVisible = frame.era >= .5f;
        plan.retroVisible = !plan.modernVisible;
        break;
    }
    plan.retro = plan.retroVisible || modernSamplesRetro;
    plan.modern =
        plan.modernVisible || (mode == RenderMode::Interactive && frame.transition != show::Transition::Cut);
    return plan;
}

std::uint32_t resolveMaxBounces(RenderMode mode, std::uint32_t requested) {
    if (requested != 0)
        return requested;
    return mode == RenderMode::Interactive ? kInteractiveMaxBounces : kExportMaxBounces;
}

HistoryReset HistoryTracker::advance(const HistoryKey& key, bool skippedSinceLast) {
    HistoryReset reset = HistoryReset::None;
    if (!last_) {
        reset = HistoryReset::FirstFrame;
    } else {
        const HistoryKey& last = *last_;
        const double dt = key.showSeconds - last.showSeconds;
        if (resized_ || key.width != last.width || key.height != last.height)
            reset = HistoryReset::Resize;
        else if (key.mode != last.mode || key.spp != last.spp)
            reset = HistoryReset::Settings;
        else if (key.chapter != last.chapter)
            reset = HistoryReset::Chapter;
        else if (key.shot != last.shot)
            reset = HistoryReset::Shot;
        else if (dt < 0.0 || dt > kTimeJumpSeconds)
            reset = HistoryReset::TimeJump;
        else if (skippedSinceLast)
            reset = HistoryReset::Skipped;
    }
    last_ = key;
    resized_ = false;
    framesSinceReset_ = reset == HistoryReset::None ? framesSinceReset_ + 1 : 0;
    return reset;
}

std::uint32_t frameSpp(RenderMode mode, std::uint32_t spp, std::uint32_t framesSinceReset) {
    if (mode != RenderMode::Interactive || framesSinceReset >= kResetSpp.size())
        return spp;
    return std::max(spp, kResetSpp[framesSinceReset]);
}

post::DenoiseSettings denoiseSettingsFor(const HistoryState& state) {
    constexpr std::uint32_t kAtrousIterations = 3;
    post::DenoiseSettings settings;
    const bool interactive = state.mode == RenderMode::Interactive;
    settings.temporal = interactive;
    settings.atrousIterations = interactive || state.spp < kExportFilterSpp ? kAtrousIterations : 0u;
    settings.fireflyFilter = !interactive && state.spp >= kExportFilterSpp;
    return settings;
}

} // namespace evo::render
