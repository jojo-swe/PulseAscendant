#pragma once

// The Ship scene (step 5, Task 16): the starship of scenes/ship_asset.hpp at its hero shot, WORMHOLE
// (chapter 23), through a gate and a tunnel of rings 1 km apart. The rings, the planet and the sky here are a
// stand-in for the wormhole itself, which belongs to the Tunnel scene; the ship, its plumes, its stars and
// its motion blur are the asset any scene can fly.
//
// The flight, cut on bar lines (chapter seconds from the beat grid; WORMHOLE's in brackets):
// - shot 0, the gate [0, 4.35): over an ocean planet's limb, looking down the flight path at the first ring;
// - shot 1, the crossing [4.35, 9.14): a locked-off camera beside the path; the ship crosses the picture at
//   300 m/s, at its centre on a downbeat [6.09], engines trailing volumetric fire (the export shutter's
//   motion blur); the camera pans after it and follows it towards the gate;
// - shot 2, into the tunnel [9.14, D 15.67): a chase from behind and above; the afterburner lights on the cut
//   and the ship accelerates through the rings until, on D, it covers one ring per beat;
// - shot 3, riding [D, 22.62): from D a ring arrives on every beat, its band of gold light sweeping the hull;
//   the camera closes in on the sun's side of the ship and holds there from the accent [16.15], the rings
//   streaking past;
// - shot 4, into the light [22.62, end): where the music breaks the camera lets the ship go; it pulls away
// into
//   the sun and the picture floods to white over the last bars.
// The ship banks with FLYBY's 1993 attitude (retro::flybyAttitude).
//
// The render frame is the ship's own: the ship stands still at the origin in its model space and the world
// moves around it (every world point X is rendered at R^T (X - P), P the ship's position, R its attitude), so
// coordinates stay small however far it flies. Cameras keep the world's up. Metres; the ship flies along
// world +z.

#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "modern/scenes/ship_asset.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace evo::modern {

// The full-detail configuration the WORMHOLE card states (the scene test pins the card to it).
inline constexpr float kWormholeRingSpacing = 1000.f; // metres: "1 KM RINGS"
inline constexpr float kWormholeRingRadius = 140.f, kWormholeRingTube = 10.f;
inline constexpr std::uint32_t kShipAirSplats = 98304; // the planet's atmosphere shell
inline constexpr float kShipCruise = 300.f;            // metres per second in shots 0 and 1
inline constexpr float kShipPlanetRadius = 6000.f;
// Towards the 5800 K sun in the world: ahead, a little right and up (the tunnel leads into it).
inline constexpr Vec3 kShipSunDirection{0.30f, 0.14f, 0.944f};

// The chapter's times (chapter seconds), from its beat grid: the cut to the crossing (the last downbeat over
// a second before the pass), the pass (the ship at the crossing camera's centre), the cut into the tunnel, D
// (the downbeat at or before the accent), the break (the fourth downbeat after D), the white-out's start (the
// next one) and the end.
struct ShipTimes {
    double crossing = 0, pass = 0, tunnel = 0, drop = 0, accent = 0, release = 0, whiteout = 0, duration = 0;
    double beat = 0.5; // the beat's length at D
};
ShipTimes shipTimes(const show::TimelineFrame& frame);
struct ShipShot {
    double start, end;
};
std::vector<ShipShot> shipShots(const show::TimelineFrame& frame);

// The ship in the world at a frame: its position along +z (metres; double, it travels tens of kilometres) and
// its attitude (model space to world, rows).
struct ShipPose {
    double z = 0;
    std::array<Vec3, 3> rows;
};
ShipPose shipPose(const show::TimelineFrame& frame);
// How far the ship has flown at a frame, and the world z of ring k (rings at D's position + k km).
double shipDistance(const show::TimelineFrame& frame);
double shipRingZ(const show::TimelineFrame& frame, int k);

// The camera of a frame in the render frame (the ship's model space).
Camera shipCamera(const show::TimelineFrame& frame);

// The throttle at a frame: 0.7 cruising with the bass; the afterburner lights on the cut into the tunnel
// (a flash that decays over a quarter of a second) and flares again on D.
float shipThrottle(const show::TimelineFrame& frame);
// The exposure (EV) of a frame: shot 3 whites out.
float shipExposureEV(const show::TimelineFrame& frame);

struct ShipStats {
    std::uint32_t stars = 0, airSplats = 0, splats = 0;
    std::array<std::uint32_t, 3> plumeGrid{};
    std::uint32_t plumeVoxels = 0, triangles = 0;
};
ShipStats shipStats(SceneDetail detail = {});

class Ship final : public ModernScene {
public:
    explicit Ship(SceneDetail detail = {});

    const SceneStatic& staticScene() const override { return static_; }
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    Camera camera(const show::TimelineFrame& frame) const override { return shipCamera(frame); }
    bool samplesRetro() const override { return false; }

    const ShipStats& stats() const { return stats_; }
    const ShipAsset& asset() const { return *asset_; }
    // The frame's material table (the ship's seams, engines and strobe; the rings' glow).
    std::vector<rt::Material> materials(const show::TimelineFrame& frame) const;
    std::uint32_t ringMaterial() const;

private:
    SceneStatic static_;
    std::unique_ptr<ShipAsset> asset_;
    ShipStats stats_;
    std::uint32_t stars_ = 0, air_ = 0;
};

} // namespace evo::modern
