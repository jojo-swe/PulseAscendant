#pragma once

#include "audio/wav.hpp"

#include <cstddef>
#include <cstdint>

namespace evo::audio {

// Plays decoded music in a loop. The position is a whole frame at 48 kHz, so the timeline and the audio
// agree to the sample; it wraps back to frame 0 after the last frame.
class Transport {
public:
    // Throws std::runtime_error if the music has no frames.
    explicit Transport(Wav wav);

    // Moves to the frame at seconds, wrapped into the song's length. The 1e-7 nudge keeps a time written
    // in decimal (0.3 s) on its exact frame despite binary rounding. Negative or non-finite seconds go to
    // the start.
    void seek(double seconds);
    void setMuted(bool muted);

    double positionSeconds() const;
    double durationSeconds() const;
    std::uint64_t frame() const { return frame_; }

    // Writes frames interleaved stereo frames to out and advances by as many, wrapping at the end. Muted
    // output is silence, but the position still advances so the music stays in sync with the visuals.
    void render(float* out, std::size_t frames);

private:
    Wav wav_;
    std::uint64_t frameCount_;
    std::uint64_t frame_ = 0;
    bool muted_ = false;
};

} // namespace evo::audio
