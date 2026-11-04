#pragma once

#include "app/options.hpp"

#include "retro/secret_part.hpp"

#include <SDL3/SDL_keycode.h>

#include <optional>
#include <string>
#include <string_view>

namespace evo::gfx {
struct MissingRequirements;
}
namespace evo::show {
struct TimelineFrame;
}

namespace evo {

// Each run function returns the process exit code and lets gfx::MissingRequirements escape; main() turns
// that into exit code 2 with missingRequirementsMessage() on stderr.

// Opens an SDL3 window (Vulkan, resizable) and presents the show until Esc, the window closes, or
// --duration elapses. The time comes from a ShowClock (the music, or SILENT MODE on the system timer); its
// status goes to stderr at the start and again whenever it switches between audio and SILENT MODE. Keys:
// Left/Right previous/next chapter, keys 1-9 and 0 jump to chapters 1-10, C CRT look, H HUD (the epoch
// cards and, only here, the chapter title), M mute, F11 fullscreen. The window title shows the chapter and
// the show time. Returns the process exit code.
int runWindowed(const Options& options);

// Headless: renders frames 0..ceil(duration*fps)-1 at t = time + i/fps and writes each as raw top-down RGB
// to stdout. Progress goes to stderr. Each frame goes through applyForcedEra (app/capture.hpp); when it
// forces the era, one stderr line says so ("render-stream: EVO_FORCE_ERA=1 (2026 only)").
int runRenderStream(const Options& options);

// Prints Context::deviceInfoJson() and a newline.
int runDeviceInfo(const Options& options);

// The chapter index a key in the window jumps to at show time now, or -1 for other keys. Left/Right step to
// the previous/next chapter and wrap around the show; keys 1-9 and 0 jump to chapters 1-10 (indices 0-9). A
// time just before a chapter's start counts as in it, so a key pressed right after a jump steps from there.
int chapterForKey(SDL_Keycode key, double now);

// The window title for a sampled frame: "EVOLUTION  <chapter title>  <mm:ss>".
std::string windowTitle(const show::TimelineFrame& frame);

// The four keys that call up the secret part in the window, as a matcher (two seconds, in order).
retro::KeyWord secretKeyWord();

// The window's secret part, apart from the window: the show hands it its keys and its clock, and it says when
// the clock must jump. In the show it listens for the four keys; once they are typed it waits for the next
// beat of the track playing, then the part runs on the music from its start (second t of the part is second t
// of the show), and at its end or on Esc the show resumes at the time it left.
class SecretSession {
public:
    enum class State { Show, Pending, Part };

    // `answer`: the word of --secret-part=WORD, which solves the puzzle up front when it is the answer.
    explicit SecretSession(std::string_view answer = {});

    // Starts the part now (--secret-part), returning to show time `resumeAt` afterwards.
    void start(double resumeAt);
    // A key while the show plays (an upper-case letter, or 0 for any other key) at show time `now`. True on
    // the key that completes the four: the part then starts on the next beat (cutAt()).
    bool showKey(char key, double now);
    // A key during the part at its second t: L answers the applause device once the last device is on screen;
    // letters are checked against the patient's answer.
    void partKey(char key, double t);
    // Esc during the part: the show time to seek to.
    double escape();
    // Every frame, with the clock's time (show seconds; the part's seconds while it runs). Returns the time
    // the clock must seek to when the part starts (0) or ends (the show time it left).
    std::optional<double> update(double now);

    State state() const { return state_; }
    double cutAt() const { return cutAt_; }
    double resumeAt() const { return resumeAt_; }
    const retro::SecretInput& input() const { return input_; }

private:
    State state_ = State::Show;
    retro::KeyWord keys_;
    retro::AnswerKeys answer_;
    retro::SecretInput input_;
    bool solvedUpFront_ = false;
    double cutAt_ = 0;
    double resumeAt_ = 0;
};

// "EVOLUTION needs a Vulkan 1.3 GPU with ray tracing. Missing: a, b"
std::string missingRequirementsMessage(const gfx::MissingRequirements& missing);

} // namespace evo
