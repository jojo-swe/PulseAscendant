#pragma once

// The secret part: a 1993 intro of its own, outside the show's 31 chapters, that runs for kSecretSeconds
// from the moment it is called up (four keys in the window, or --secret-part). Its clock is the chiptune
// that opens the show: second t of the part is second t of track 0, so everything lands on that track's
// tracked beats. Three screens:
//   1. a double box, "EVOLUTION · SECRET PART", and four lines typed in;
//   2. the box clears line by line; on the left, one line per beat pair, the show's own numbers (the same
//      accessors as the epoch cards), and the glenz from GLENZ bouncing in the right third;
//   3. the BOOT block typed again, the last device still waiting; a dim row of seven numbers for the
//      patient. L answers the device (window only), and the part ends in a CRT power-off.
// Like every retro effect it renders palette indices at 320x200 and 35 retro frames a second, so the
// picture is a pure function of the retro frame and of the viewer's input (SecretInput), which the frame
// handed to the retro stage carries in its chapter number (secretFrame).

#include "retro/effects.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace evo::retro {

inline constexpr double kSecretSeconds = 28.0;
inline constexpr std::uint32_t kSecretRetroRate = 35;
// The chiptune's track: the part's second t is this track's second t.
inline constexpr int kSecretTrack = 0;

// What the viewer did, in seconds of the part; negative for never. Headless runs never press L, and
// --secret-part=W sets solvedAt to the puzzle's time when W is the answer.
struct SecretInput {
    double applauseAt = -1; // L pressed (counts only from screen 3's last device on)
    double solvedAt = -1;   // the answer typed (shown no earlier than the puzzle itself)
};

// The part's landmarks in its own seconds, all on track 0's tracked beats except the power-off.
struct SecretMarks {
    double screen2 = 0, screen3 = 0;
    std::array<double, 8> list{}; // screen 2's lines: the heading, four numbers, three closing lines
    double puzzle = 0;            // the row for the patient
    double lastDevice = 0;        // screen 3's third device line is typed out
};
const SecretMarks& secretMarks();

// How long the part runs for this input: kSecretSeconds, longer only when a late answer needs time to read.
double secretDuration(const SecretInput& input);
// The power-off collapse: the last kSecretPowerOffSeconds of the part.
inline constexpr double kSecretPowerOffSeconds = 0.6;

// The input as the part sees it at the retro frame grid (an applause before the last device is ignored, an
// answer shows from the puzzle on), so that equal frames come from equal keys.
SecretInput settledInput(const SecretInput& input);

// The numbers screen 2 lists, from the scenes' full-detail configuration (the epoch cards' accessors) and
// the show's length.
struct SecretStats {
    std::uint64_t photons = 0;   // caustic photons in an export frame of GLASS CATHEDRAL
    std::uint64_t stars = 0;     // the stars of the galaxy (GALAXY and ASCENDANT)
    std::uint64_t particles = 0; // DOTS' traced particles
    std::uint64_t frames = 0;    // the video at 60 frames a second: ceil(60 x showDuration())
};
SecretStats secretStats();

// "4,194,304": digits grouped by three with commas.
std::string groupDigits(std::uint64_t value);

// The seven numbers on screen 3's last row.
std::array<std::uint32_t, 7> patientNumbers();
// Whether a typed word (any case) is the one the seven numbers spell; compared by its SHA-256 only.
bool isPatientAnswer(std::string_view word);
// The lowercase hex SHA-256 of a byte string (FIPS 180-4).
std::string sha256Hex(std::string_view bytes);

// Every line of text on the screen at second t, top to bottom, as typed so far (no cursor). Box lines
// are not text and are left out.
std::vector<std::string> secretScreenText(double t, const SecretInput& input);
// Which screen t is in: 1, 2 or 3; 0 from the power-off on.
int secretScreen(double t, const SecretInput& input);
// How far the glenz is lifted off its floor at t, in units of its normal bounce (0 on a beat, 1 at the top of
// a normal bounce; higher for the bounce after an applause). 0 outside screens 2 and 3.
double secretGlenzLift(double t, const SecretInput& input);

// The frame the retro stage renders the part from: chapter carries the settled input, localSeconds is t.
show::TimelineFrame secretFrame(double t, const SecretInput& input);
// The input a secretFrame carries.
SecretInput secretInputOf(const show::TimelineFrame& frame);
// The part's EffectInfo (background, text, scene and palette by the retro frame).
const EffectInfo& secretEffect();

// Recognises a word typed as single keys: the letters in order, from the first to the last within
// `window` seconds. Any other key in between starts over (and may itself begin the word).
class KeyWord {
public:
    KeyWord(std::string_view word, double windowSeconds);
    // An upper-case letter (or any other character); true on the press that completes the word.
    bool press(char key, double seconds);
    // How many letters of the word have been typed in order so far.
    std::size_t progress() const { return progress_; }
    void reset() { progress_ = 0; }

private:
    std::string word_;
    double window_;
    std::size_t progress_ = 0;
    double start_ = 0;
};

// The last letters typed during the part, checked against the patient's answer after every key.
class AnswerKeys {
public:
    // True on the key that completes the answer.
    bool press(char key);

private:
    std::string typed_;
};

} // namespace evo::retro
