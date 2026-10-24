#include "app/clock.hpp"

#include <SDL3/SDL_timer.h>

#include <cmath>

namespace evo {

ShowClock::ShowClock(bool audioWanted, const std::filesystem::path& wav, double showSeconds, double startAt)
    : showSeconds_(showSeconds) {
    restartTimer(startAt);
    if (!audioWanted) {
        status_ = "SILENT MODE: audio off (--no-audio)";
        return;
    }
    if (!player_.start(wav, showSeconds)) {
        status_ = "SILENT MODE: " + player_.status();
        return;
    }
    audio_ = true;
    player_.seek(timerStart_);
    lastAudioSeconds_ = timerStart_;
    status_ = "AUDIO: " + player_.status();
}

double ShowClock::now() {
    if (audio_) {
        if (player_.active()) {
            lastAudioSeconds_ = wrap(player_.positionSeconds());
            return lastAudioSeconds_;
        }
        // The stream failed mid-show: keep going from where the music was.
        status_ = "SILENT MODE: " + player_.status();
        player_.stop();
        audio_ = false;
        restartTimer(lastAudioSeconds_);
    }
    const double elapsed = static_cast<double>(SDL_GetTicksNS() - timerStartNs_) / 1e9;
    return wrap(timerStart_ + elapsed);
}

void ShowClock::seek(double seconds) {
    restartTimer(seconds);
    if (audio_) {
        player_.seek(timerStart_);
        lastAudioSeconds_ = timerStart_;
    }
}

void ShowClock::setMuted(bool muted) {
    player_.setMuted(muted); // ignored while silent
}

double ShowClock::wrap(double seconds) const {
    if (!std::isfinite(seconds) || seconds < 0.0)
        return 0.0;
    return std::fmod(seconds, showSeconds_);
}

void ShowClock::restartTimer(double seconds) {
    timerStart_ = wrap(seconds);
    timerStartNs_ = SDL_GetTicksNS();
}

} // namespace evo
