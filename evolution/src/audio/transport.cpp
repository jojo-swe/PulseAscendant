#include "audio/transport.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace evo::audio {

Transport::Transport(Wav wav) : wav_(std::move(wav)), frameCount_(wav_.frames()) {
    if (frameCount_ == 0)
        throw std::runtime_error("audio transport needs at least one frame of music");
}

void Transport::seek(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0)
        seconds = 0.0;
    const double wrapped = std::fmod(seconds, durationSeconds());
    // The nudge can round the last fraction of a frame up to frameCount_, which is frame 0 again.
    frame_ = static_cast<std::uint64_t>(std::floor(wrapped * kSampleRate + 1e-7)) % frameCount_;
}

void Transport::setMuted(bool muted) {
    muted_ = muted;
}

double Transport::positionSeconds() const {
    return static_cast<double>(frame_) / kSampleRate;
}

double Transport::durationSeconds() const {
    return static_cast<double>(frameCount_) / kSampleRate;
}

void Transport::render(float* out, std::size_t frames) {
    for (std::size_t i = 0; i < frames; ++i) {
        out[2 * i] = muted_ ? 0.0f : wav_.stereo[2 * frame_];
        out[2 * i + 1] = muted_ ? 0.0f : wav_.stereo[2 * frame_ + 1];
        frame_ = frame_ + 1 == frameCount_ ? 0 : frame_ + 1;
    }
}

} // namespace evo::audio
