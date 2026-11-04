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

std::uint32_t frameSpp(RenderMode mode, std::uint32_t spp, std::uint32_t framesSinceReset, bool fast) {
    const std::array<std::uint32_t, 2>& reset = fast ? kFastResetSpp : kResetSpp;
    if (mode != RenderMode::Interactive || framesSinceReset >= reset.size())
        return spp;
    return std::max(spp, reset[framesSinceReset]);
}

post::DenoiseSettings denoiseSettingsFor(const HistoryState& state) {
    constexpr std::uint32_t kAtrousIterations = 3;
    post::DenoiseSettings settings;
    const bool interactive = state.mode == RenderMode::Interactive;
    settings.temporal = interactive;
    const bool fine = !interactive && state.spp >= kExportFilterSpp;
    settings.fireflyFilter = fine;
    if (fine) {
        // Every export pixel has a history of one frame, so the reset iterations would apply to all of
        // them: both are set to the light pass, steps 1 and 2.
        settings.atrousIterations = kExportFineAtrousIterations;
        settings.resetAtrousIterations = kExportFineAtrousIterations;
        settings.chromaAtrousIterations = kExportChromaAtrousIterations;
    } else {
        settings.atrousIterations = kAtrousIterations;
    }
    return settings;
}

} // namespace evo::render
