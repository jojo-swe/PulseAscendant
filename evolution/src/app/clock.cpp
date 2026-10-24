#include "app/clock.hpp"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>

namespace evo {

ShowClock::ShowClock(bool audioWanted, const std::filesystem::path& wav, double showSeconds, double startAt,
                     double audioOffsetSeconds)
    : showSeconds_(showSeconds), audioOffset_(audioOffsetSeconds) {
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
    seekAudio(timerStart_);
    status_ = "AUDIO: " + player_.status();
}

double ShowClock::now() {
    if (audio_) {
        if (player_.active()) {
            double seconds = player_.positionSeconds() + audioOffset_;
            if (seconds < 0.0) // a negative offset just after the music looped: the end of the show
                seconds += showSeconds_;
            lastAudioSeconds_ = wrap(seconds);
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
    if (audio_)
        seekAudio(timerStart_);
}

void ShowClock::setMuted(bool muted) {
    player_.setMuted(muted); // ignored while silent
}

double ShowClock::wrap(double seconds) const {
    if (!std::isfinite(seconds) || seconds < 0.0)
        return 0.0;
    return std::fmod(seconds, showSeconds_);
}

// Moves the music so that the show time reads showTime. A positive offset near the start of the show would
// need music from before its start; the music starts at 0 then, and the show time a little later.
void ShowClock::seekAudio(double showTime) {
    const double music = std::max(0.0, showTime - audioOffset_);
    player_.seek(music);
    lastAudioSeconds_ = wrap(music + audioOffset_);
}

void ShowClock::restartTimer(double seconds) {
    timerStart_ = wrap(seconds);
    timerStartNs_ = SDL_GetTicksNS();
}

} // namespace evo
