#include "show/music_curves.hpp"

#include "show/generated/music_data.hpp"
#include "show/generated/reactivity_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace evo::show {

static_assert(generated::bands == 24, "TimelineFrame::spectrum holds 24 bands");

namespace {

constexpr double kSampleRate = 48000.0;

int clampTrack(int track) {
    return std::clamp(track, 0, generated::trackCount - 1);
}

// A position in a table of count samples at rate Hz: the sample at or before t, the one after it and the
// blend between them.
struct Lerp {
    int i;
    int j;
    double f;
};

Lerp locate(double seconds, int rate, int count) {
    const double at = std::clamp(seconds * rate, 0.0, double(count - 1));
    const int i = int(at);
    return {i, std::min(i + 1, count - 1), at - i};
}

float blendBytes(std::uint8_t a, std::uint8_t b, double f) {
    return float((a * (1 - f) + b * f) / 255.0);
}

int envelopeCount(int track) {
    return generated::envelopeOffsets[track + 1] - generated::envelopeOffsets[track];
}

int reactivityCount(int track) {
    return generated::reactivityOffsets[track + 1] - generated::reactivityOffsets[track];
}

struct CameraSeries {
    std::vector<float> drive;
    std::vector<double> flow;
};

CameraSeries buildCameraSeries(int track) {
    const int rate = generated::reactivityRate;
    const int count = reactivityCount(track);
    std::vector<double> drive(count), speed(count);
    for (int i = 0; i < count; ++i) {
        const double at = double(i) / rate;
        const double energy = envelope(track, at, EnvelopeChannel::Energy);
        const double low = reactivity(track, at, ReactivityChannel::Low);
        const double mid = reactivity(track, at, ReactivityChannel::Mid);
        drive[i] = .2 * energy + .2 * low + .1 * mid;
        speed[i] = .5 * energy + .8 * low + .35 * mid;
    }

    const double k = 1 - std::exp(-1.0 / (.35 * rate));
    for (std::vector<double>* series : {&drive, &speed}) {
        std::vector<double>& v = *series;
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 1; i < count; ++i)
                v[i] = v[i - 1] + k * (v[i] - v[i - 1]);
            for (int i = count - 2; i >= 0; --i)
                v[i] = v[i + 1] + k * (v[i] - v[i + 1]);
        }
    }

    CameraSeries result;
    result.drive.assign(drive.begin(), drive.end());
    result.flow.assign(count, 0.0);
    for (int i = 1; i < count; ++i)
        result.flow[i] = result.flow[i - 1] + (speed[i] + speed[i - 1]) * .5 / rate;
    return result;
}

const CameraSeries& cameraSeries(int track) {
    static const std::vector<CameraSeries> series = [] {
        std::vector<CameraSeries> all;
        for (int track = 0; track < generated::trackCount; ++track)
            all.push_back(buildCameraSeries(track));
        return all;
    }();
    return series[clampTrack(track)];
}

template <class T> double sampleSeries(const std::vector<T>& values, double trackSeconds) {
    const Lerp at = locate(trackSeconds, generated::reactivityRate, int(values.size()));
    return values[at.i] + (values[at.j] - values[at.i]) * at.f;
}

} // namespace

int trackCount() {
    return generated::trackCount;
}

std::uint64_t trackFrames(int track) {
    return generated::trackFrames[clampTrack(track)];
}

double trackStart(int track) {
    std::uint64_t frames = 0;
    for (int i = 0; i < clampTrack(track); ++i)
        frames += generated::trackFrames[i];
    return double(frames) / kSampleRate;
}

double trackDuration(int track) {
    return double(generated::trackFrames[clampTrack(track)]) / kSampleRate;
}

int trackAt(double showSeconds) {
    int track = 0;
    while (track + 1 < generated::trackCount && showSeconds >= trackStart(track + 1))
        ++track;
    return track;
}

float envelope(int track, double trackSeconds, EnvelopeChannel channel) {
    track = clampTrack(track);
    const Lerp at = locate(trackSeconds, generated::envelopeRate, envelopeCount(track));
    const int base = generated::envelopeOffsets[track];
    const int c = int(channel);
    return blendBytes(generated::envelope[(base + at.i) * 3 + c], generated::envelope[(base + at.j) * 3 + c],
                      at.f);
}

std::array<float, 24> spectrum(int track, double trackSeconds) {
    track = clampTrack(track);
    const Lerp at = locate(trackSeconds, generated::envelopeRate, envelopeCount(track));
    const int base = generated::envelopeOffsets[track];
    std::array<float, 24> out{};
    for (int band = 0; band < 24; ++band)
        out[band] = blendBytes(generated::spectrum[(base + at.i) * 24 + band],
                               generated::spectrum[(base + at.j) * 24 + band], at.f);
    return out;
}

float reactivity(int track, double trackSeconds, ReactivityChannel channel) {
    track = clampTrack(track);
    const Lerp at = locate(trackSeconds, generated::reactivityRate, reactivityCount(track));
    const int base = generated::reactivityOffsets[track];
    const int c = int(channel);
    return blendBytes(generated::reactivity[(base + at.i) * 4 + c],
                      generated::reactivity[(base + at.j) * 4 + c], at.f);
}

float smoothDrive(int track, double trackSeconds) {
    return float(sampleSeries(cameraSeries(track).drive, trackSeconds));
}

double smoothFlow(int track, double trackSeconds) {
    return sampleSeries(cameraSeries(track).flow, trackSeconds);
}

} // namespace evo::show
