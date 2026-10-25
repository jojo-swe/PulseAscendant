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

// A camera shot of a Studio chapter: from start to end in seconds into the chapter.
struct StudioShot {
    float start, end;
};
// The shots of a chapter in order; they cover [0, chapterDuration]. STUDIO NIGHT cuts at its accent.
std::vector<StudioShot> studioShots(const show::TimelineFrame& frame);

// The camera of a Studio chapter's frame: a pure function of the chapter's effect, localSeconds,
// chapterDuration and accentSeconds. Each shot runs Catmull-Rom through authored keys with smoothstep
// easing over the shot; `shot` is the index of the shot in studioShots (a change resets the history).
//   FINALE, PORTAL  one shot: a slow pull from the screen out to a wide view of the desk (step 6 replaces
//                   PORTAL's with the path out of the screen)
//   STUDIO NIGHT    shot 0 until the accent: a wide dolly across the dark room towards the desk; shot 1 from
//                   the accent: a close orbit around the monitor
//   RETURN          one shot: a slow push back towards the screen
//   REBOOT          one shot: a close-up of the screen, drifting slightly
// Any other effect gets REBOOT's close-up. Every shot but STUDIO NIGHT's dolly is about the screen: it
// focuses on the screen's plane with an aperture of at most 4 mm, so the CRT's picture stays sharp.
Camera studioCamera(const show::TimelineFrame& frame);

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

// A chapter's light rig (Task 14 of step 5): the lamp's level (0 off, 1 full) and the screen's
// emissionScale. The frame's material table carries them (SceneFrame::materials), and the lights' emissions
// follow from it.
//   STUDIO NIGHT    the lamp off, the screen 8: the CRT is the key light, the moonbeam the fill (3.5 left the nearly black DOS screen too weak against the moon)
//   FINALE, PORTAL  the lamp at 0.2, the screen 2.4: the megamix lights the room (3.0 needed an
//                   exposure that left the wide shots black once the screen's colours stayed saturated)
//   RETURN, REBOOT  the lamp on (1), the screen 1.5: the lamp coming back on is a story beat
// Any other effect gets RETURN's rig.
struct StudioRig {
    float lamp;
    float screen;
};
StudioRig studioRig(show::Effect effect);

class Studio final : public ModernScene {
public:
    // Builds the meshes, materials, the placed instances and the four lights once (CPU only). The Studio has
    // nothing that scales with the detail: every detail builds the same room. The static material table has
    // RETURN's rig; every frame brings its chapter's table.
    explicit Studio(SceneDetail detail = {});

    // The mesh every key cap instances, and how many key caps there are (a 101-key keyboard).
    static constexpr std::uint32_t kKeyCapMesh = 0;
    static constexpr std::uint32_t kKeyCount = 101;
    // The lights of every frame, in this order: the screen, the lamp, the window (a portal onto the sky) and
    // the moon outside it. Light selection weighs them per shading point by their power (lights.glsl), so
    // their select weights are all 1; a light the rig switches off has no power and is never chosen.
    enum Light : std::uint32_t {
        kScreenLight = 0,
        kLampLight = 1,
        kWindowLight = 2,
        kMoonLight = 3,
        kLightCount
    };
    static constexpr float kScreenWeight = 1.f, kLampWeight = 1.f, kWindowWeight = 1.f, kMoonWeight = 1.f;
    // Towards the moon (unit length): high (44 degrees) behind the window and a little to its left, so that
    // the moonbeam falls past the desk's left end onto the floor in front of it, where the wide shots see the
    // window's cross in it (Task 14). The Studio's sky (studioEnvironment) centres its glow on it, and the
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

    const SceneStatic& staticScene() const override { return static_; }
    // The fixed room with the chapter's camera, exposure, light rig (materials and lights) and sky.
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    // studioCamera(frame), without building the frame.
    Camera camera(const show::TimelineFrame& frame) const override { return studioCamera(frame); }
    bool samplesRetro() const override { return true; }
    std::string nowCard(show::Effect effect) const override;

private:
    SceneStatic static_;
    std::vector<SceneInstance> instances_;
    std::vector<rt::RectLight> lights_;
};

} // namespace evo::modern
