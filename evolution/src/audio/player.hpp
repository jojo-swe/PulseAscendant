#pragma once

#include "audio/transport.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

struct SDL_AudioStream;

namespace evo::audio {

// Plays the show's music on the default output device through SDL3 and reports where playback is, so the
// music can be the clock the visuals follow.
//
// Threading: SDL calls the stream callback on its own audio thread and holds the stream's lock while it
// runs. Every member that touches the transport from the main thread (seek, setMuted, positionSeconds)
// takes the same lock with SDL_LockAudioStream, so the transport is only ever used by one thread at a time.
class Player {
public:
    Player() = default;
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Loads the WAV, checks that it is expectedSeconds long (within half a frame) and starts playing it from
    // the start. Returns false on any failure and leaves the reason in status(): a WAV of another length
    // gives "Music length <x> s differs from the show <y> s". The length is checked before any audio device
    // is opened.
    bool start(const std::filesystem::path& wav, double expectedSeconds);
    void stop();

    // Both are ignored while not playing.
    void seek(double seconds);
    void setMuted(bool muted);

    // The position of the sample being heard: the transport's position minus what is still queued in the
    // stream and minus the device's buffer, wrapped into the music. Never before the last seek (or the
    // start): until the first new sample can have reached the speaker it reads as the seek target. 0 while
    // not playing.
    double positionSeconds() const;
    // The device's buffer in seconds, as SDL reports it once the device runs; 0 while not playing.
    double deviceBufferSeconds() const { return deviceBufferSeconds_; }

    // Playing, and the stream has not failed since start().
    bool active() const;
    // The WAV's path while playing, else the reason it is not playing.
    std::string status() const;

private:
    static void callback(void* userdata, SDL_AudioStream* stream, int additionalBytes, int totalBytes);

    std::unique_ptr<Transport> transport_;
    SDL_AudioStream* stream_ = nullptr;
    bool audioInitialised_ = false;
    bool failed_ = false; // set on the audio thread under the stream lock
    double deviceBufferSeconds_ = 0.0;
    double seekSeconds_ = 0.0;          // transport position at the last seek or the start
    std::uint64_t framesSinceSeek_ = 0; // rendered since then; advanced on the audio thread under the lock
    std::string status_ = "not started";
};

} // namespace evo::audio
