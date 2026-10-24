#pragma once

#include "audio/player.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace evo {

// The single time source for rendering a live run. While the music plays, the time is where the music
// is, so the visuals follow what is heard; without audio (none wanted, the WAV refused, no device) the time
// runs on the monotonic system timer instead and the clock is "silent". Times are show seconds, always in
// [0, showSeconds).
class ShowClock {
public:
    // Starts at startAt (wrapped into the show). With audioWanted, plays wav, which must be showSeconds long
    // to within half a frame; any failure leaves the clock silent with the reason in status().
    ShowClock(bool audioWanted, const std::filesystem::path& wav, double showSeconds, double startAt);

    // If the audio stream fails while running, the clock carries on silently from the last audio time.
    double now();
    void seek(double seconds);
    void setMuted(bool muted);

    bool silent() const { return !audio_; }
    // "SILENT MODE: <reason>" or "AUDIO: <file>".
    std::string status() const { return status_; }

private:
    double wrap(double seconds) const;
    void restartTimer(double seconds);

    audio::Player player_;
    double showSeconds_;
    bool audio_ = false;
    double lastAudioSeconds_ = 0.0;
    double timerStart_ = 0.0;        // show time when the timer was last (re)started
    std::uint64_t timerStartNs_ = 0; // SDL_GetTicksNS() at that moment
    std::string status_;
};

} // namespace evo
