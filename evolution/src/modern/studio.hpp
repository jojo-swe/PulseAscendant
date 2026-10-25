#pragma once

// The Studio (spec 2.1): the 2026 room that frames the show. A dark modern room at night (polished concrete,
// plaster, an oak desk, a slim black window frame) around a 1993 PC, the only 1993
// object in it; the CRT shows the live retro picture and is the room's key light, a desk lamp pools warm
// light on the desk when the chapter's light rig switches it on (studioRig), and a window in the back wall
// opens onto the night sky over a skyline. Everything is built in code: the room, the desk, the beige
// monitor and desktop case, a 101-key keyboard (one key-cap mesh, 101 instances), a mouse, floppy disks, a
// mug, a chrome and a glass sphere on small stands beside the monitor, the lamp, a chair and the skyline.
//
// World units are metres, +y up, the desk's front edge on z = 0 and the room behind it towards -z; cameras
// look from +z. Static apart from the camera, which follows authored keys per chapter (studioCamera).

#include "modern/environment.hpp"
#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace evo::modern {

// The CRT's picture: the RetroScreen light and the emissive quad it is, 0.28 x 0.21 m (4:3, the 320 x 200
// picture at the 1993 pixel aspect), flat and facing +z. Corners in picture order: top left,
// top right, bottom right, bottom left.
std::array<Vec3, 4> studioScreenCorners();

// The four lenses (36 mm equivalent), as horizontal fields of view in degrees: 24 mm for interiors, 35 mm
// the Studio's default, 50 mm the CRT close-ups, 85 mm compression shots. lensFovY gives a lens's vertical
// field of view on the 16:9 frame (what Camera takes).
inline constexpr float kLens24 = 73.f, kLens35 = 54.f, kLens50 = 40.f, kLens85 = 24.f;
float lensFovY(float horizontalDegrees);

// A camera shot of a Studio chapter: from start to end in seconds into the chapter.
struct StudioShot {
    float start, end;
};
// The shots of a chapter in order; they cover [0, chapterDuration]. A new shot resets the history: PORTAL
// at its two glass crossings (D and the bar line two bars on), RETURN at its cut to the close-up (D); every
// other Studio chapter is one shot.
std::vector<StudioShot> studioShots(const show::TimelineFrame& frame);

// The camera of a Studio chapter's frame, a pure function of the show time. The moves are authored in show
// seconds from the chapters' transition marks, Catmull-Rom through timed keys with eased ends, on the four
// lenses, plus the handheld (studioHandheld) where a chapter has it:
//   FLYBY         the pull back out of the CRT: before D, the approach and the pull-out crt.comp shows on the
//                 full-frame 1993 picture (studioScreenSpan); from D the room, 1.2 m out by A + 1 s, then
//                 one slow arc to the CRT in profile;
//   FINALE        35 mm, 1.5 m from the CRT, handheld; closing in over the last bar, into PORTAL;
//   PORTAL        the palindrome: the dolly into the CRT, through the glass on D into the 1993 starfield,
//                 a turn centred on the accent that sees the room through the back of the glass,
//                 out through the glass two bars after D, and back into the room to STUDIO NIGHT's first
//                 frame;
//   STUDIO NIGHT  the dolly that never stops, on from PORTAL: back to the night wide, round behind the chair
//                 in the moonbeam to the window by D - 4 bars, tilting up to the sky, through the pane on
//                 the accent;
//   RETURN        from outside the window down into the lamp-lit room, then a cut on D to the CRT close-up
//                 (50 mm, pushing in from 65 to 40 cm by the join), which REBOOT holds and dollies in from
//                 D - 4 bars through the glass on D.
// Shots that show the screen focus on its plane with an aperture of at most 4 mm.
Camera studioCamera(const show::TimelineFrame& frame);

// How much of the 16:9 frame's width the screen spans in the camera's view on the 35 mm lens: the picture's
// size as crt.comp shows it before FLYBY's glass, so that the 1993 frame on D - 1/60 s and the Studio's on D
// match (1.02: the picture just fills the frame on D; 0.75: the full-frame pillarbox).
float studioScreenSpan(const show::TimelineFrame& frame);

// The handheld micro-jitter of the Studio's cameras at a show time: three incommensurate sines and a hashed
// wander per channel, 0.4 - 1.1 Hz, rotating at most kHandheldDegrees and moving at most kHandheldMetres
// (offset: right, up, back in the camera's frame); 0 within 0.5 s of every glass time (studioGlassTimes),
// easing back in over the next second. A pure function of the time.
inline constexpr float kHandheldDegrees = 0.12f, kHandheldMetres = 0.002f;
struct StudioHandheld {
    Vec3 offset{0, 0, 0};
    float yaw = 0, pitch = 0, roll = 0;
};
StudioHandheld studioHandheld(double showSeconds);
// The show times at which a Studio camera passes a glass: FLYBY's D, PORTAL's D and two bars on, STUDIO
// NIGHT's window pane (its accent) and REBOOT's D.
std::vector<double> studioGlassTimes();

// The exposure (EV, applied before the tonemap) of a Studio chapter: chosen per chapter by eye, so that the
// room reads as a dark room lit by the CRT (and the lamp, where the rig has it on) and the screen's brightest
// colours stay saturated under PBR Neutral where they matter: the screen's display gain (its emissionScale
// times the beam profile's mean 0.875 times 2^EV) stays at about 2.4 in the colourful chapters (FINALE,
// PORTAL; the decision on Task 5: a pastel CRT is fixed by the screen's level and the exposure,
// not by the tonemap), while the grey DOS text of the others may glow brighter.
float studioExposureEV(show::Effect effect);

// The Studio's sky in every chapter: the NightSky with its glow on Studio::kMoonDirection (the moon itself is
// the emitter outside the window, so the sky has no disc) and a brighter, hazier horizon, which gives the
// room a dim cool fill through the window. Never sampled by NEE (select 0): the window's portal samples it.
EnvironmentDesc studioEnvironment();
// The sky a frame uses: studioEnvironment, but in RETURN (and REBOOT, which runs on from it) dawn: the
// horizon brightening to grey-blue (0.10, 0.14, 0.22) over RETURN, no moon glow (and the frame's moon is off).
EnvironmentDesc studioEnvironmentAt(const show::TimelineFrame& frame);

// A chapter's light rig: the lamp's level (0 off, 1 full) and the screen's
// emissionScale. The frame's material table carries them (SceneFrame::materials), and the lights' emissions
// follow from it.
//   FLYBY           the lamp off, the screen 2.4: the room is revealed lit by phosphor and moon
//   STUDIO NIGHT    the lamp off, the screen 8: the CRT is the key light, the moonbeam the fill (3.5 left the
//   nearly black DOS screen too weak against the moon) FINALE, PORTAL  the lamp at 0.2, the screen 2.4: the
//   megamix lights the room (3.0 needed an
//                   exposure that left the wide shots black once the screen's colours stayed saturated)
//   RETURN, REBOOT  the lamp on (1), the screen 1.5: the lamp coming back on is a story beat
// Any other effect gets RETURN's rig.
struct StudioRig {
    float lamp;
    float screen;
};
StudioRig studioRig(show::Effect effect);

// The rig and the exposure a frame actually uses: its chapter's (studioRig, studioExposureEV), but where a
// take runs on across a change, eased between them so that nothing pops:
//   FLYBY         the exposure opens from the picture's own brightness on the glass (D) to the room's by
//                 A + 0.25 s (and closes down to it over the approach before D, where only crt.comp shows);
//   PORTAL        behind the glass (D to the accent) the rig and the exposure ease to STUDIO NIGHT's, which
//                 the camera lands in on 194.00: the room brightens beyond the glass as the camera turns to
//                 it;
//   STUDIO NIGHT  as the dolly reaches the window and tilts up to the sky (D - 6 bars to D - 3 bars), the
//                 exposure closes from the room's 2 EV to 0.3 EV, so the sky outside stays a night sky;
//   REBOOT        over the dolly into the screen (D - 3 bars to D - 0.5 s) the exposure closes to the 1993
//                 picture's own brightness on the glass (the screen's display gain near 1);
//   RETURN        0.8 EV more open until its cut on D, coming in from the dawn outside; over its last bar the
//                 exposure eases to REBOOT's, across the act's join.
StudioRig studioRigAt(const show::TimelineFrame& frame);
float studioExposureAt(const show::TimelineFrame& frame);

class Studio final : public ModernScene {
public:
    // Builds the meshes, materials, the placed instances and the four lights once (CPU only). The Studio has
    // nothing that scales with the detail: every detail builds the same room. The static material table has
    // RETURN's rig; every frame brings its chapter's table.
    explicit Studio(SceneDetail detail = {});

    // The mesh every key cap instances, and how many key caps there are (a 101-key keyboard).
    static constexpr std::uint32_t kKeyCapMesh = 0;
    static constexpr std::uint32_t kKeyCount = 101;
    // The lights of every frame, in this order: the screen, the lamp, the window (a portal onto the sky), the
    // moon outside it and the town's glow. Light selection weighs them per shading point by their power (lights.glsl), so
    // their select weights are all 1; a light the rig switches off has no power and is never chosen.
    enum Light : std::uint32_t {
        kScreenLight = 0,
        kLampLight = 1,
        kWindowLight = 2,
        kMoonLight = 3,
        kTownLight = 4, // the town's sodium glow outside the window, which lifts the right wall
        kLightCount
    };
    static constexpr float kScreenWeight = 1.f, kLampWeight = 1.f, kWindowWeight = 1.f, kMoonWeight = 1.f,
                           kTownWeight = 1.f;
    // Towards the moon (unit length): high (44 degrees) behind the window and a little to its left, so that
    // the moonbeam falls past the desk's left end onto the floor in front of it, where the wide shots see the
    // window's cross in it. The Studio's sky (studioEnvironment) centres its glow on it, and the
    // moon's emitter stands 4 m out from the window along it.
    static constexpr Vec3 kMoonDirection{-0.2104f, 0.6913f, -0.6913f};
    // The lamp's emitter: a warm 8 cm square diffuser recessed 3.5 cm into the shade, colour times intensity
    // times the rig's lamp level.
    static constexpr Vec3 kLampColor{1.f, 0.75f, 0.5f};
    static constexpr float kLampIntensity = 50.f;
    // The moon's emitter: colour times intensity.
    static constexpr Vec3 kMoonColor{0.72f, 0.84f, 1.f};
    static constexpr float kMoonIntensity = 700.f;
    static constexpr float kChromeRadius = 0.04f, kGlassRadius = 0.045f;
    // Where the chrome and the glass sphere rest (the bottom of each sphere's stand, on the desk): the chrome
    // one in front of the monitor's right, where it reflects the screen, the glass one between the lamp and
    // the screen, where it refracts the picture and throws the lamp's caustic.
    static constexpr Vec3 kChromeAt{0.235f, 0.75f, -0.11f}, kGlassAt{-0.33f, 0.75f, -0.27f};
    // The indices of the rigged and the camera-only materials in the material tables.
    static constexpr std::uint32_t kScreenMaterial = 7, kLampMaterial = 22, kLedMaterial = 12;
    // The moon's emitter, which RETURN's dawn (and REBOOT) switch off.
    static constexpr std::uint32_t kMoonMaterial = 26;
    // The screen seen from behind: thin glass with the picture at kScreenBackLevel of the rig's screen level
    // (the phosphor seen through its backing, so the room shows through it).
    static constexpr std::uint32_t kScreenBackMaterial = 34;
    static constexpr float kScreenBackLevel = 0.15f;

    const SceneStatic& staticScene() const override { return static_; }
    // The fixed room with the chapter's camera, exposure, light rig (materials and lights) and sky.
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    // studioCamera(frame), without building the frame.
    Camera camera(const show::TimelineFrame& frame) const override { return studioCamera(frame); }
    bool samplesRetro() const override { return true; }
    std::string nowCard(show::Effect effect) const override;

    // How many of FLYBY's 1993 stars hang behind the screen (kFlybyStars).
    static constexpr std::uint32_t kStarCount = 512;

private:
    // The room as a camera behind the screen sees it (studioBehindScreen): the monitor's body gone, the
    // screen thin glass seen from behind, a dark box round the camera open only through the screen, and the
    // stars in it.
    void buildBehindScreen();

    SceneStatic static_;
    std::vector<SceneInstance> instances_;
    std::vector<SceneInstance> behindInstances_;
    std::vector<std::uint32_t> behindHidden_; // the monitor's foot, body and housing
    std::uint32_t screenInstance_ = 0;
    std::vector<rt::RectLight> lights_;
};

// Whether a camera is behind the screen's glass (PORTAL's and REBOOT's crossings): its eye behind the
// screen's plane, within the monitor's width and height. The Studio then shows the inside of the picture.
bool studioBehindScreen(const Camera& camera);

} // namespace evo::modern
