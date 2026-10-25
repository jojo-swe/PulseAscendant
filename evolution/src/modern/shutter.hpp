#pragma once

// The export shutter (step 5, Task 9): a frame's picture is the average of up to kMaxShutterSlices
// sub-pictures, each traced at its own time within the shutter interval, which gives motion blur. The window
// has no shutter (one slice at the frame's time). Everything here is a pure function of (time, fps, spp), so
// a frame stays a pure function of the show time.

#include "show/timeline.hpp"

#include <cstdint>

namespace evo::modern {

// The most time slices one frame is traced in (a TLAS build, a particle rebuild and one uniform region each).
inline constexpr std::uint32_t kMaxShutterSlices = 8;

// How many slices a frame of `spp` samples is split into: one per dispatch of kSppPerDispatch (4) samples,
// at most kMaxShutterSlices; 1 without a shutter (shutterSeconds <= 0) and for spp <= 4. spp 0 counts as 1.
std::uint32_t shutterSlices(std::uint32_t spp, double shutterSeconds);

// Slice k of `slices` is centred in the k-th of `slices` equal parts of the shutter interval, which is
// centred on the frame's time: ((k + 0.5) / slices - 0.5) * shutterSeconds seconds from it. Throws
// std::invalid_argument for k >= slices.
double sliceOffset(std::uint32_t k, std::uint32_t slices, double shutterSeconds);

// The slice a dispatch belongs to when `dispatches` dispatches are split evenly over `slices` slices
// (contiguous; every slice gets at least one while slices <= dispatches).
std::uint32_t sliceOfDispatch(std::uint32_t dispatch, std::uint32_t dispatches, std::uint32_t slices);

// The chapter-local time of slice k: localSeconds + sliceOffset, clamped into
// [min(localSeconds, 0), chapterDuration - 1e-6] so that a slice never samples another chapter (a chapter's
// first frame does not look back into the chapter before it, nor its last forward into the next). A frame
// shown before its chapter starts (a negative local time, show::sceneFrame) keeps its slices from its own
// time on.
double sliceSeconds(double localSeconds, double chapterDuration, std::uint32_t k, std::uint32_t slices,
                    double shutterSeconds);

// The timeline frame at chapter-local time `localSeconds` of the chapter `frame` is in: sampleTimeline() at
// the matching show time, with the era state of `frame`; a negative local time is moved into the chapter by
// show::sceneFrame, as the director moves a frame shown before its chapter. Throws std::logic_error when a
// later time falls in another chapter (a frame that does not come from sampleTimeline, or a time past the
// chapter).
show::TimelineFrame sliceFrame(const show::TimelineFrame& frame, double localSeconds);

} // namespace evo::modern
