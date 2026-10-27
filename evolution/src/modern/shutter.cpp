#include "modern/shutter.hpp"

#include "show/director.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace evo::modern {

namespace {
constexpr std::uint32_t kSamplesPerSlice = 4; // kSppPerDispatch (path_tracer.hpp)
constexpr double kChapterEndMargin = 1e-6;
} // namespace

std::uint32_t shutterSlices(std::uint32_t spp, double shutterSeconds) {
    if (!(shutterSeconds > 0.0))
        return 1;
    const std::uint32_t dispatches = (std::max(spp, 1u) + kSamplesPerSlice - 1) / kSamplesPerSlice;
    return std::min(dispatches, kMaxShutterSlices);
}

std::uint32_t shutterSlices(std::uint32_t spp, double shutterSeconds, std::uint32_t requested) {
    if (requested == 0)
        return shutterSlices(spp, shutterSeconds);
    if (!(shutterSeconds > 0.0))
        return 1;
    return std::min({requested, std::max(spp, 1u), kMaxShutterSlices});
}

std::uint32_t samplesPerDispatch(std::uint32_t spp, std::uint32_t slices) {
    spp = std::max(spp, 1u);
    slices = std::max(slices, 1u);
    if ((spp + kSamplesPerSlice - 1) / kSamplesPerSlice >= slices)
        return kSamplesPerSlice; // the default split: dispatches of 4, as many as the slices need or more
    return (spp + slices - 1) / slices;
}

double sliceOffset(std::uint32_t k, std::uint32_t slices, double shutterSeconds) {
    if (slices == 0 || k >= slices)
        throw std::invalid_argument("sliceOffset: slice " + std::to_string(k) + " of " +
                                    std::to_string(slices));
    return ((static_cast<double>(k) + 0.5) / static_cast<double>(slices) - 0.5) * shutterSeconds;
}

std::uint32_t sliceOfDispatch(std::uint32_t dispatch, std::uint32_t dispatches, std::uint32_t slices) {
    if (dispatches == 0 || slices == 0 || dispatch >= dispatches)
        throw std::invalid_argument("sliceOfDispatch: dispatch " + std::to_string(dispatch) + " of " +
                                    std::to_string(dispatches) + " in " + std::to_string(slices) + " slices");
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(dispatch) * slices / dispatches);
}

double sliceSeconds(double localSeconds, double chapterDuration, std::uint32_t k, std::uint32_t slices,
                    double shutterSeconds) {
    const double seconds = localSeconds + sliceOffset(k, slices, shutterSeconds);
    const double last = std::max(chapterDuration - kChapterEndMargin, 0.0);
    return std::clamp(seconds, std::min(localSeconds, 0.0), last);
}

show::TimelineFrame sliceFrame(const show::TimelineFrame& frame, double localSeconds) {
    const double showSeconds = frame.showSeconds + (localSeconds - frame.localSeconds);
    // A scene shown before its chapter starts (REBOOT's look ahead at GALAXY): the slice is moved into the
    // frame's chapter as the director moves the frame itself.
    if (localSeconds < 0.0)
        return show::sceneFrame(frame, show::SceneCut{frame.chapter, showSeconds, false});
    show::TimelineFrame slice = show::sampleTimeline(showSeconds);
    if (slice.chapter != frame.chapter)
        throw std::logic_error("sliceFrame: local time " + std::to_string(localSeconds) +
                               " is outside chapter " + std::to_string(frame.chapter));
    slice.era = frame.era;
    slice.wipe = frame.wipe;
    slice.eraCut = frame.eraCut;
    slice.swapCount = frame.swapCount;
    return slice;
}

} // namespace evo::modern
