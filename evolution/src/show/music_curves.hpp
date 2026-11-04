#pragma once

#include <array>
#include <cstdint>

// Sampling of the generated music tables (tools/analyze_music.py, tools/analyze_reactivity.py). Every curve
// is per track and takes track seconds: show seconds minus trackStart(track). Times outside the track clamp
// to its first or last sample. Values are linear interpolations of the byte tables, scaled to [0, 1].
namespace evo::show {

int trackCount();
std::uint64_t trackFrames(int track); // at 48 kHz
double trackStart(int track);         // show seconds
double trackDuration(int track);      // seconds
// The track playing at a show time (times outside the show clamp to the first or last track).
int trackAt(double showSeconds);

// The 20 Hz envelope.
enum class EnvelopeChannel { Energy = 0, Bass = 1, Hit = 2 };
float envelope(int track, double trackSeconds, EnvelopeChannel channel);
std::array<float, 24> spectrum(int track, double trackSeconds);

// The 100 Hz reactivity series (the parent's pulse()).
enum class ReactivityChannel { Low = 0, Mid = 1, High = 2, LowOnsetPhase = 3 };
float reactivity(int track, double trackSeconds, ReactivityChannel channel);

// Camera-safe series at the reactivity rate, as the parent's cameraSeries(): the transient drive and the
// motion speed are smoothed with two forward and two backward exponential passes (zero phase, about 0.7 s
// wide), so a camera follows phrases rather than single kicks. smoothFlow integrates the smoothed speed
// from the track start, so it never decreases. Built once per process; seeking gives identical values.
float smoothDrive(int track, double trackSeconds);
double smoothFlow(int track, double trackSeconds);

} // namespace evo::show
