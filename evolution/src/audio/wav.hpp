#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace evo::audio {

// The one sample rate the demo plays at; the music is authored for it and never resampled.
inline constexpr int kSampleRate = 48000;

// Decoded music: interleaved stereo (L R L R ...), each sample in -1..1.
struct Wav {
    std::vector<float> stereo;

    std::uint64_t frames() const { return stereo.size() / 2; }
};

// Reads a stereo 48 kHz 16-bit PCM WAV file of at most 512 MiB. Chunks other than "fmt " and "data" are
// skipped. Throws std::runtime_error naming the path and the problem for anything else.
Wav readWav(const std::filesystem::path& path);

} // namespace evo::audio
