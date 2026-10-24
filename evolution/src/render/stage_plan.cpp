#include "render/stage_plan.hpp"

namespace evo::render {

StagePlan planStages(const show::TimelineFrame& frame, RenderMode mode, bool modernSamplesRetro) {
    StagePlan plan{};
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
        if (key.width != last.width || key.height != last.height)
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
    framesSinceReset_ = reset == HistoryReset::None ? framesSinceReset_ + 1 : 0;
    return reset;
}

} // namespace evo::render
