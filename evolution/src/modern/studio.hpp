#pragma once

// The Studio (spec 2.1): the 2026 room that frames the show. A dark room at night with a 1993 PC on a wooden
// desk; the CRT shows the live retro picture and lights the room, a warm desk lamp pools light on the desk,
// and a window in the back wall opens onto the procedural night sky. Everything is built in code: the room,
// the desk, the beige monitor and desktop case, a 101-key keyboard (one key-cap mesh, 101 instances), a
// mouse, floppy disks, a mug, a chrome and a glass sphere on small stands and the lamp.
//
// World units are metres, +y up, the desk's front edge on z = 0 and the room behind it towards -z; cameras
// look from +z. Static apart from the camera, which follows authored keys per chapter (studioCamera).

#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "rt/sky_constants.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace evo::modern {

// The CRT's picture: the RetroScreen light and the emissive quad it is, 0.28 x 0.21 m (4:3, the 320 x 200
// picture at the 1993 pixel aspect), flat (Decision 10) and facing +z. Corners in picture order: top left,
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
// room reads as a dark room lit by the CRT and the lamp.
float studioExposureEV(show::Effect effect);

class Studio final : public ModernScene {
public:
    // Builds the meshes, materials, the placed instances and the four lights once (CPU only). The Studio has
    // nothing that scales with the detail: every detail builds the same room.
    explicit Studio(SceneDetail detail = {});

    // The mesh every key cap instances, and how many key caps there are (a 101-key keyboard).
    static constexpr std::uint32_t kKeyCapMesh = 0;
    static constexpr std::uint32_t kKeyCount = 101;
    // The lights of every frame, in this order, with their select weights: the screen 0.3, the lamp 0.45 (it
    // lights most of what the cameras see, so at 1 spp most samples go to it), the window (a portal onto the
    // sky) 0.1 and the moon outside it 0.15.
    enum Light : std::uint32_t {
        kScreenLight = 0,
        kLampLight = 1,
        kWindowLight = 2,
        kMoonLight = 3,
        kLightCount
    };
    static constexpr float kScreenWeight = 0.3f, kLampWeight = 0.45f, kWindowWeight = 0.1f,
                           kMoonWeight = 0.15f;
    // Towards the moon: rt::kMoonDirection (rt/sky_constants.hpp), the default disc and glow direction of
    // the NightSky environment the Studio's frames keep (EnvironmentDesc{}). The moon's emitter stands 4 m
    // out from the window along it.
    static constexpr Vec3 kMoonDirection{rt::kMoonDirection[0], rt::kMoonDirection[1], rt::kMoonDirection[2]};
    // The screen's emissionScale: the RetroScreen light's radiance is the picture (linear light) times it.
    static constexpr float kScreenEmission = 1.2f;
    // The lamp's emitter: a warm 10 cm square, colour times intensity.
    static constexpr Vec3 kLampColor{1.f, 0.75f, 0.5f};
    static constexpr float kLampIntensity = 35.f;
    static constexpr float kChromeRadius = 0.04f, kGlassRadius = 0.045f;

    const SceneStatic& staticScene() const override { return static_; }
    // The fixed room with the chapter's camera and exposure.
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    // studioCamera(frame), without building the frame.
    Camera camera(const show::TimelineFrame& frame) const override { return studioCamera(frame); }
    bool samplesRetro() const override { return true; }

private:
    SceneStatic static_;
    std::vector<SceneInstance> instances_;
    std::vector<rt::RectLight> lights_;
};

} // namespace evo::modern
