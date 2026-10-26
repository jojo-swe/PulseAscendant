#include "audio/player.hpp"

#include "audio/wav.hpp"
#include "gfx/paths.hpp"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <locale>
#include <sstream>
#include <utility>

namespace evo::audio {

namespace {

constexpr int kChannels = 2;
constexpr int kBytesPerFrame = kChannels * static_cast<int>(sizeof(float));

// Seconds to 10 significant digits, enough to show a one-frame difference: 0.5, 300.5, 300.4999792.
std::string formatSeconds(double seconds) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(10);
    out << seconds;
    return out.str();
}

std::string sdlError(const std::string& what) {
    return what + ": " + SDL_GetError();
}

// Holds the stream's lock for a scope; the audio thread holds the same lock while it runs the callback.
class StreamLock {
public:
    explicit StreamLock(SDL_AudioStream* stream) : stream_(stream) { SDL_LockAudioStream(stream_); }
    ~StreamLock() { SDL_UnlockAudioStream(stream_); }
    StreamLock(const StreamLock&) = delete;
    StreamLock& operator=(const StreamLock&) = delete;

private:
    SDL_AudioStream* stream_;
};

// The device's own buffer, which plays after everything in the stream; 0 if SDL cannot say.
double queryDeviceBufferSeconds(SDL_AudioStream* stream) {
    SDL_AudioSpec spec{};
    int sampleFrames = 0;
    if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &spec, &sampleFrames) || spec.freq <= 0)
        return 0.0;
    return std::max(0.0, static_cast<double>(sampleFrames) / spec.freq);
}

} // namespace

Player::~Player() {
    stop();
}

bool Player::start(const std::filesystem::path& wav, double expectedSeconds) {
    stop();

    // Everything about the file is checked before a device is opened, so a wrong song is refused the same
    // way on a machine without audio.
    std::string failure;
    try {
        auto transport = std::make_unique<Transport>(readWav(wav));
        const double duration = transport->durationSeconds();
        if (std::abs(duration - expectedSeconds) > 0.5 / kSampleRate)
            failure = "Music length " + formatSeconds(duration) + " s differs from the show " +
                      formatSeconds(expectedSeconds) + " s";
        else
            transport_ = std::move(transport);
    } catch (const std::exception& error) {
        failure = error.what();
    }

    if (failure.empty()) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            failure = sdlError("cannot start SDL audio");
        } else {
            audioInitialised_ = true;
            const SDL_AudioSpec spec{SDL_AUDIO_F32, kChannels, kSampleRate};
            // The device starts paused, so the callback cannot run before stream_ is set.
            stream_ =
                SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Player::callback, this);
            if (!stream_)
                failure = sdlError("cannot open the audio device");
            else if (!SDL_ResumeAudioStreamDevice(stream_))
                failure = sdlError("cannot start the audio device");
            else
                deviceBufferSeconds_ = queryDeviceBufferSeconds(stream_);
        }
    }

    if (!failure.empty()) {
        stop();
        status_ = failure;
        return false;
    }
    status_ = gfx::pathToUtf8(wav);
    return true;
}

void Player::stop() {
    // Destroying the stream unbinds it from the device first, so the callback has finished for good before
    // the transport goes.
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    transport_.reset();
    if (audioInitialised_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audioInitialised_ = false;
    }
    failed_ = false;
    deviceBufferSeconds_ = 0.0;
    seekSeconds_ = 0.0;
    framesSinceSeek_ = 0;
    status_ = "stopped";
}

void Player::seek(double seconds) {
    if (!stream_)
        return;
    StreamLock lock(stream_);
    // Drops what was rendered from the old position, so the new one is heard as soon as possible.
    SDL_ClearAudioStream(stream_);
    transport_->seek(seconds);
    seekSeconds_ = transport_->positionSeconds();
    framesSinceSeek_ = 0;
}

void Player::setMuted(bool muted) {
    if (!stream_)
        return;
    StreamLock lock(stream_);
    transport_->setMuted(muted);
}

double Player::positionSeconds() const {
    if (!stream_)
        return 0.0;
    double seconds = 0.0, duration = 1.0;
    {
        StreamLock lock(stream_);
        // Of what was rendered since the seek, the queue and the device buffer are still to be heard.
        const int queuedBytes = std::max(0, SDL_GetAudioStreamQueued(stream_));
        const double unheard =
            static_cast<double>(queuedBytes) / kBytesPerFrame / kSampleRate + deviceBufferSeconds_;
        const double rendered = static_cast<double>(framesSinceSeek_) / kSampleRate;
        seconds = seekSeconds_ + std::max(0.0, rendered - unheard);
        duration = transport_->durationSeconds();
    }
    const double wrapped = std::fmod(seconds, duration);
    return wrapped < 0.0 ? wrapped + duration : wrapped;
}

bool Player::active() const {
    if (!stream_)
        return false;
    StreamLock lock(stream_);
    return !failed_;
}

std::string Player::status() const {
    if (stream_ && !active())
        return "the audio stream failed";
    return status_;
}

// Runs on SDL's audio thread with the stream locked. Renders what the device asked for, in pieces that fit
// a fixed buffer.
void Player::callback(void* userdata, SDL_AudioStream* stream, int additionalBytes, int /*totalBytes*/) {
    Player& player = *static_cast<Player*>(userdata);
    std::array<float, 4096 * kChannels> buffer;
    std::size_t remaining =
        static_cast<std::size_t>(std::max(0, additionalBytes) + kBytesPerFrame - 1) / kBytesPerFrame;
    while (remaining > 0 && !player.failed_) {
        const std::size_t frames = std::min(remaining, buffer.size() / kChannels);
        player.transport_->render(buffer.data(), frames);
        player.framesSinceSeek_ += frames;
        if (!SDL_PutAudioStreamData(stream, buffer.data(), static_cast<int>(frames * kBytesPerFrame)))
            player.failed_ = true;
        remaining -= frames;
    }
}

} // namespace evo::audio
