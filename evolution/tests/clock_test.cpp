#include "app/app.hpp"
#include "app/clock.hpp"
#include "audio/player.hpp"
#include "show/timeline.hpp"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_timer.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr double kShowSeconds = 711.16;
constexpr std::uint32_t kHalfSecond = 24000; // frames at 48 kHz

void appendU16(std::vector<char>& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void appendU32(std::vector<char>& out, std::uint32_t value) {
    appendU16(out, value & 0xFFFF);
    appendU16(out, value >> 16);
}

void appendTag(std::vector<char>& out, const char* tag) {
    out.insert(out.end(), tag, tag + 4);
}

// Writes frames of quiet stereo 48 kHz PCM16 (a slow ramp, so a muted render differs from an unmuted one).
void writeWav(const std::filesystem::path& path, std::uint32_t frames) {
    std::vector<char> file;
    appendTag(file, "RIFF");
    appendU32(file, 36 + frames * 4);
    appendTag(file, "WAVE");
    appendTag(file, "fmt ");
    appendU32(file, 16);
    appendU16(file, 1); // PCM
    appendU16(file, 2); // channels
    appendU32(file, 48000);
    appendU32(file, 48000 * 4); // byte rate
    appendU16(file, 4);         // block align
    appendU16(file, 16);        // bits per sample
    appendTag(file, "data");
    appendU32(file, frames * 4);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        appendU16(file, frame % 256);
        appendU16(file, frame % 256);
    }

    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(file.data(), static_cast<std::streamsize>(file.size()));
    if (!out)
        throw std::runtime_error("could not write " + path.string());
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
}

// Without audio the clock runs on the monotonic timer from the start time.
void checkSilentClock() {
    evo::ShowClock clock(false, "unused.wav", kShowSeconds, 0.0);
    CHECK(clock.silent());
    CHECK(startsWith(clock.status(), "SILENT MODE: "));

    const double first = clock.now();
    const double second = clock.now();
    const double third = clock.now();
    CHECK(first >= 0.0);
    CHECK(second >= first);
    CHECK(third >= second);

    clock.seek(100.0);
    const double afterSeek = clock.now();
    CHECK(afterSeek >= 100.0);
    CHECK(afterSeek < 100.5);

    // Muting a silent clock is harmless.
    clock.setMuted(true);
    CHECK(clock.silent());

    // Times wrap into the show, both at the start and on a seek.
    evo::ShowClock late(false, "unused.wav", kShowSeconds, kShowSeconds + 5.0);
    const double wrapped = late.now();
    CHECK(wrapped >= 5.0 && wrapped < 5.5);
    late.seek(-3.0);
    CHECK(late.now() < 0.5);
}

// Any problem with the music leaves the clock silent with the reason in its status (Review Focus 2).
void checkRefusedMusic(const std::filesystem::path& dir) {
    const std::filesystem::path missing = dir / "missing.wav";
    std::filesystem::remove(missing);
    evo::ShowClock noFile(true, missing, kShowSeconds, 0.0);
    CHECK(noFile.silent());
    CHECK(startsWith(noFile.status(), "SILENT MODE: "));
    CHECK(noFile.status().find("missing.wav") != std::string::npos);

    // A WAV that does not match the generated show data is refused, naming both lengths. The check runs
    // before any audio device is opened.
    const std::filesystem::path halfSecond = dir / "half_second.wav";
    writeWav(halfSecond, kHalfSecond);
    evo::ShowClock wrongLength(true, halfSecond, kShowSeconds, 0.0);
    CHECK(wrongLength.silent());
    CHECK(wrongLength.status() == "SILENT MODE: Music length 0.5 s differs from the show 711.16 s");
    const double t = wrongLength.now();
    CHECK(t >= 0.0 && t < 0.5);
}

double secondsSince(std::uint64_t ticksNs) {
    return static_cast<double>(SDL_GetTicksNS() - ticksNs) / 1e9;
}

// SDL's dummy driver plays into nothing at real speed, so the player and the audio clock run anywhere. The
// hint overrides an SDL_AUDIO_DRIVER in the environment.
void checkPlayerOnDummyDriver(const std::filesystem::path& dir) {
    const std::filesystem::path halfSecond = dir / "half_second.wav";
    CHECK(SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE));

    evo::audio::Player player;
    CHECK(!player.active());

    // The length must match to within half a frame.
    CHECK(!player.start(halfSecond, 0.5 + 0.6 / 48000.0));
    CHECK(!player.active());
    CHECK(startsWith(player.status(), "Music length 0.5 s differs from the show"));
    CHECK(player.start(halfSecond, 0.5 + 0.4 / 48000.0));
    CHECK(player.active());
    CHECK(player.status() == halfSecond.string());

    // The position is what is heard: the transport less the queue and the device's buffer. It never runs
    // ahead of the time since the seek, and lags it by at most the device buffer plus scheduling slack.
    const double buffer = player.deviceBufferSeconds();
    std::printf("dummy device buffer: %.1f ms\n", buffer * 1000.0);
    CHECK(buffer >= 0.0 && buffer < 0.2);
    player.setMuted(true);
    const std::uint64_t beforePlayerSeek = SDL_GetTicksNS();
    player.seek(0.25);
    const std::uint64_t afterPlayerSeek = SDL_GetTicksNS();
    CHECK(player.positionSeconds() == 0.25); // nothing new can have been heard yet
    SDL_Delay(50);
    const double atLeast = secondsSince(afterPlayerSeek);
    const double position = player.positionSeconds();
    const double atMost = secondsSince(beforePlayerSeek);
    std::printf("after seek(0.25) and %.1f ms: position %.4f s\n", atLeast * 1000.0, position);
    CHECK(position <= 0.25 + atMost);
    CHECK(position >= 0.25 + atLeast - (buffer + 0.05));
    player.stop();
    CHECK(!player.active());
    CHECK(player.deviceBufferSeconds() == 0.0);

    // The audio clock starts where asked and moves with the music. Two seconds of music leave room for a
    // slow machine before the position wraps.
    const std::filesystem::path twoSeconds = dir / "two_seconds.wav";
    writeWav(twoSeconds, 4 * kHalfSecond);
    evo::ShowClock clock(true, twoSeconds, 2.0, 0.1);
    CHECK(!clock.silent());
    CHECK(clock.status() == "AUDIO: " + twoSeconds.string());
    clock.setMuted(true);
    clock.seek(0.3);
    const double afterSeek = clock.now();
    CHECK(afterSeek >= 0.3 && afterSeek < 0.4);
    SDL_Delay(200);
    const double later = clock.now();
    CHECK(later > afterSeek && later < 2.0);

    // After a seek the clock reads the music: within one device buffer + 50 ms of the time since the seek,
    // and never ahead of it.
    const std::uint64_t beforeClockSeek = SDL_GetTicksNS();
    clock.seek(0.5);
    const std::uint64_t afterClockSeek = SDL_GetTicksNS();
    for (int i = 0; i < 4; ++i) {
        SDL_Delay(40);
        const double atLeast = secondsSince(afterClockSeek);
        const double t = clock.now();
        const double atMost = secondsSince(beforeClockSeek);
        CHECK(t <= 0.5 + atMost);
        CHECK(t >= 0.5 + atLeast - (buffer + 0.05));
    }
}

// A clock read right after a seek: at the target, or a few milliseconds of music past it.
bool justAfter(double t, double target) {
    return t >= target - 1e-9 && t < target + 0.05;
}

// --audio-offset: the show time is the music's position plus the offset, and a seek still lands where asked.
void checkAudioOffset(const std::filesystem::path& dir) {
    const std::filesystem::path twoSeconds = dir / "two_seconds.wav";
    evo::ShowClock later(true, twoSeconds, 2.0, 0.5, 0.2);
    CHECK(!later.silent());
    later.setMuted(true);
    CHECK(justAfter(later.now(), 0.5)); // the music was moved to 0.3
    later.seek(1.0);
    CHECK(justAfter(later.now(), 1.0));
    // At the start a positive offset would need music from before it: the music starts at 0, the show at
    // the offset.
    later.seek(0.0);
    CHECK(justAfter(later.now(), 0.2));

    // A negative offset reads the end of the show just after the music loops.
    evo::ShowClock earlier(true, twoSeconds, 2.0, 1.9, -0.2);
    earlier.setMuted(true);
    CHECK(justAfter(earlier.now(), 1.9)); // the music was moved to 0.1, past the loop
    earlier.seek(0.0);
    CHECK(justAfter(earlier.now(), 0.0));

    // Without audio there is nothing to offset against.
    evo::ShowClock silent(false, twoSeconds, 2.0, 0.5, 0.2);
    CHECK(justAfter(silent.now(), 0.5));
}

// The window's keys pick where the clock jumps; its title shows where it is.
void checkControls() {
    using evo::chapterForKey;
    const int last = evo::show::chapterCount() - 1;
    CHECK(chapterForKey(SDLK_RIGHT, 0.0) == 1);
    CHECK(chapterForKey(SDLK_LEFT, 0.0) == last); // wraps around the show
    CHECK(chapterForKey(SDLK_RIGHT, kShowSeconds - 0.01) == 0);
    CHECK(chapterForKey(SDLK_LEFT, evo::show::chapterStart(11) + 1.0) == 10);
    // A clock reading a hair before the chapter it just jumped to still counts as in it.
    CHECK(chapterForKey(SDLK_RIGHT, evo::show::chapterStart(11) - 0.01) == 12);
    CHECK(chapterForKey(SDLK_1, 500.0) == 0);
    CHECK(chapterForKey(SDLK_9, 0.0) == 8);
    CHECK(chapterForKey(SDLK_0, 0.0) == 9);
    CHECK(chapterForKey(SDLK_A, 0.0) == -1);

    CHECK(evo::windowTitle(evo::show::sampleTimeline(0.0)) == "EVOLUTION  BOOT  00:00");
    CHECK(evo::windowTitle(evo::show::sampleTimeline(194.5)) == "EVOLUTION  STUDIO NIGHT  03:14");
}

} // namespace

int main() {
    try {
        const std::filesystem::path dir = std::filesystem::path(EVO_TEST_OUT_DIR) / "clock";
        checkSilentClock();
        checkControls();
        checkRefusedMusic(dir);
        checkPlayerOnDummyDriver(dir);
        checkAudioOffset(dir);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CHECK failed: %s\n", error.what());
        return 1;
    }
    std::printf("%d checks passed\n", checksPassed);
    return 0;
}
