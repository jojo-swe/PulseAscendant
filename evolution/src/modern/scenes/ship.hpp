#pragma once

// The Ship (step 5, Task 16): FLYBY's 2026 side. A sleek procedural starship of about 40,000 triangles, the
// 1993 arrow-head fighter rebuilt at full detail (the same fuselage, swept wings and two engine pods, so the
// portal between the eras shows one ship): a lofted brushed-titanium fuselage over dark ceramic belly panels,
// emissive panel lines that pulse on the kick, chrome intake lips, a double-walled glass canopy over a cockpit
// lit by its dials, navigation lights that strobe on the beat, and two nozzles glowing white-hot, whose plumes
// are a baked density grid of 96^3 voxels each with blackbody emission (Task 11's medium). Around it: the Space
// environment with a warm 0.6 degree sun as the key light, a planet below (an analytic sphere of ocean under a
// clear coat, its atmosphere a glowing limb of splats), and 262,144 stars as splats with four-spike
// diffraction kernels that grow near the sun.
//
// The flight is the 1993 flight (retro/flyby_flight.hpp): the ship's attitude is flybyAttitude, and the first
// shot hands over to flybyView's camera, scaled to metres, before the portal opens, so both eras show the same
// picture while they cross-fade. The ship flies along world +z at kShipSpeed.
//
// The render frame is the ship's own: the ship stands still at the origin in its model space (so its
// world-space surface patterns do not crawl, and its coordinates stay small), and the world moves around it:
// every world point X is rendered at R^T (X - P), P the ship's position and R its attitude. The camera keeps
// the world's up, so the horizon stays level while the ship banks. Metres, model space x to starboard, y up, z
// along the nose.

#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace evo::modern {

// --- What the epoch card can show (the full-detail configuration; the scene test pins it) ------------------

inline constexpr std::uint32_t kShipStars = 262144;       // star splats
inline constexpr std::uint32_t kShipLimbSplats = 131072;  // the planet's glowing limb
inline constexpr std::uint32_t kShipPlumeVoxelsAxis = 96; // per axis of each plume's grid
inline constexpr std::uint32_t kShipPlumes = 2;
inline constexpr std::uint32_t kShipPlumeVoxels =
    kShipPlumes * kShipPlumeVoxelsAxis * kShipPlumeVoxelsAxis * kShipPlumeVoxelsAxis; // 1,769,472
// The card's line, built from the numbers above.
inline constexpr const char* kShipCardLine = "262 144 STARS \xC2\xB7 VOLUMETRIC FIRE \xC2\xB7 PATH TRACED";

struct ShipStats {
    std::uint32_t stars = 0, limbSplats = 0, splats = 0; // splats: stars + limb + the sun's spikes
    std::uint32_t plumeVoxels = 0;                       // over both plumes
    std::uint32_t triangles = 0;                         // of the ship's meshes as instanced
    std::uint32_t meshes = 0, materials = 0;
};
// The counts of a Ship built at `detail` (splats and voxels scale with it; the ship's triangles do not).
ShipStats shipStats(SceneDetail detail = {});

// --- The flight ---------------------------------------------------------------------------------------------

inline constexpr float kShipScale = 4.f;    // metres per 1993 model unit: an 18 m ship
inline constexpr float kShipSpeed = 100.f;  // metres per second along world +z
inline constexpr float kPlanetRadius = 6000.f;
inline constexpr float kPlanetAltitude = 320.f; // of the ship above the ocean at the chapter's middle
// Towards the sun in the world (from the right, above and a little behind, as FLYBY's 1993 light).
inline constexpr Vec3 kShipSunDirection{0.80f, 0.52f, -0.30f};

// The shots of FLYBY: 0 until the accent (a chase camera behind and above that swings round to the 1993
// camera before the portal opens), 1 from the accent (the ship crosses the camera at speed: the shutter's
// motion blur), 2 from kShipCrossingSeconds later to the end (a slow orbit past the canopy). They cover
// [0, chapterDuration].
struct ShipShot {
    float start, end;
};
inline constexpr float kShipCrossingSeconds = 2.6f;
// When, after the accent, the ship passes the camera closest.
inline constexpr float kShipPassSeconds = 0.25f;
std::vector<ShipShot> shipShots(const show::TimelineFrame& frame);

// The ship in the world at `localSeconds` into the chapter: its position (metres, the planet's frame) and
// attitude (model space to world, retro::flybyAttitude; rows).
struct ShipPose {
    Vec3 position;
    std::array<Vec3, 3> rows;
};
ShipPose shipPose(double localSeconds);

// A camera in the world frame (positions in metres relative to the planet's frame, up +y), before it is
// carried into the render frame. shipCamera() is this, carried.
Camera shipWorldCamera(const show::TimelineFrame& frame);
// The camera of a FLYBY frame in the render frame (the ship's model space, metres).
Camera shipCamera(const show::TimelineFrame& frame);
// A world point (or direction) in the render frame of the pose: R^T (x - P) (R^T d).
Vec3 shipRenderPoint(const ShipPose& pose, Vec3 world);
Vec3 shipRenderDirection(const ShipPose& pose, Vec3 world);

// The exposure (EV) of a shot.
float shipExposureEV(std::uint32_t shot);

// The nozzles' exits in model space (metres): the centre of each engine's exhaust, right then left, and the
// direction the exhaust leaves along (-z).
std::array<Vec3, 2> shipNozzles();

class Ship final : public ModernScene {
public:
    explicit Ship(SceneDetail detail = {});

    const SceneStatic& staticScene() const override { return static_; }
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    Camera camera(const show::TimelineFrame& frame) const override { return shipCamera(frame); }
    bool samplesRetro() const override { return false; }

    const ShipStats& stats() const { return stats_; }
    // The material table with the panel lines, nozzles and lights at a frame's kick and beat (the frame's
    // per-frame table).
    std::vector<rt::Material> materials(const show::TimelineFrame& frame) const;
    // Indices of the per-frame materials (for tests).
    std::uint32_t panelMaterial() const;
    std::uint32_t strobeMaterial() const;

private:
    SceneStatic static_;
    std::vector<SceneInstance> shipInstances_; // the ship's parts, all at the model transform
    ShipStats stats_;
    std::uint32_t stars_ = 0, limb_ = 0;
};

} // namespace evo::modern
