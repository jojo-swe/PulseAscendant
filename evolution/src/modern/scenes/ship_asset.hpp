#pragma once

// The starship (step 5, Task 16) as an asset any scene can fly: its meshes and materials appended to a
// scene's tables, its instances at a model-to-render transform, the per-frame materials (seams pulsing on the
// kick, engines at a throttle, the strobe on the beat), its two plumes as a grid medium, and a star field of
// splats. The Ship scene (scenes/ship.hpp) stages it in WORMHOLE; other scenes (the megamix's cut) add it the
// same way.
//
// The ship: the 1993 FLYBY fighter's layout (retro/flyby.cpp) rebuilt as a designed hull of about 45,000
// triangles, 18.5 m long, model space in metres (y up, z along the nose, so +x is to port):
// - a lofted fuselage with a chine, a dorsal spine and a black ceramic nose cap; twin canted fins; a cranked
//   delta wing with dihedral and a root extension; an engine nacelle under each wing;
// - brushed titanium skin panels (Pattern::Hull: staggered panels, filtered seams, grooves that make the
//   highlight anisotropic), black ceramic tiles under the belly, along the spine and on the wings' leading
//   edges, whose glowing amber seams pulse on the kick;
// - chrome intake lips and spinners, nacelles heat-tinted towards the nozzles, nozzle petals, white-hot
//   throats and glowing nozzle walls;
// - a gold-tinted double-walled glass canopy over a cockpit lit by a dash light (the scene's one rect light)
//   and its screens; navigation lights and a strobe;
// - two plumes: one baked density grid of 192 x 96 x 96 voxels (two plumes of 96^3) behind the nozzles
//   (shaders/modern/ship_plumes.comp; Task 11's medium): a turbulent cone whose emission is Planck's law at a
//   temperature falling along the axis (white-blue, orange, dark), with shock diamonds under the afterburner.
// The stars (shaders/modern/ship_splats.comp): 262,144 splats with four-spike diffraction kernels that grow
// near the sun, the sun's glare, and optionally a planet's atmosphere shell (a glow of splats over its limb).

#include "modern/medium.hpp"
#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace evo::modern {

// --- The numbers an epoch card can state (full detail; the scene test pins them)
// ----------------------------

inline constexpr std::uint32_t kShipStars = 262144;
inline constexpr std::uint32_t kShipPlumes = 2;
inline constexpr std::uint32_t kShipPlumeVoxelsAxis = 96; // per axis of each plume
// Both plumes' grid, side by side along x.
inline constexpr std::array<std::uint32_t, 3> kShipPlumeGrid{kShipPlumes * kShipPlumeVoxelsAxis,
                                                             kShipPlumeVoxelsAxis, kShipPlumeVoxelsAxis};
inline constexpr std::uint32_t kShipPlumeVoxels = kShipPlumeGrid[0] * kShipPlumeGrid[1] * kShipPlumeGrid[2];

// The shaders a scene flying the ship sets as its SceneStatic::mediumShader and splatShader.
inline constexpr const char* kShipPlumeShader = "modern_ship_plumes.comp.spv";
inline constexpr const char* kShipSplatShader = "modern_ship_splats.comp.spv";

// The ship's size and its nozzles' exits in model space (metres, +x then -x); the exhaust leaves along -z.
inline constexpr float kShipLength = 18.5f;
std::array<Vec3, 2> shipNozzles();

// --- The engines ------------------------------------------------------------------------------------------

// The plumes' temperature (kelvin) `behind` metres behind a nozzle's exit at `radial` (a fraction of the
// plume's radius there) at a throttle (0.6 cruising, 1 the afterburner, up to 1.6 in a flash);
// ship_plumes.comp computes the same.
float shipPlumeTemperature(float behind, float radial, float throttle);
// The plume's length (metres) at a throttle.
float shipPlumeLength(float throttle);
// Planck's law at 610, 550 and 465 nm (the sRGB primaries' wavelengths) at `kelvin`, relative to 6500 K: a
// blackbody's radiance per channel, (1, 1, 1) at 6500 K.
Vec3 shipBlackbody(float kelvin);
// The plumes' emission at `kelvin`: the blackbody's colour with its luminance compressed (^0.55), so the
// orange tail stays visible beside the white-hot core (the shader's voxel emission before emissionScale).
Vec3 shipPlumeEmission(float kelvin);

// --- The star field
// -----------------------------------------------------------------------------------------

// A frame's splats: `stars` stars fixed to the world, the sun's glare, and `air` points of a planet's
// atmosphere shell (0: no planet). Directions and points are in the render frame: worldToRender turns world
// directions into it (a rotation), eye is the camera's eye, planetCentre/planetRadius the planet's.
struct ShipSky {
    std::uint32_t stars = 0, air = 0;
    Vec3 eye{};
    std::array<float, 4> worldToRender{0, 0, 0, 1}; // a quaternion (x, y, z, w)
    Vec3 sun{0, 0, 1};
    Vec3 planetCentre{};
    float planetRadius = 0;
};
SplatFrame shipSplats(const ShipSky& sky);
// The quaternion of a rotation given by its rows.
std::array<float, 4> quaternionOfRows(const std::array<Vec3, 3>& rows);

// --- The asset
// ------------------------------------------------------------------------------------------------

struct ShipCounts {
    std::uint32_t triangles = 0, meshes = 0, materials = 0, instances = 0;
};

class ShipAsset {
public:
    // Appends the ship's meshes and materials to `scene` (whose indices it remembers) and sets its medium and
    // splat shaders; raises the scene's capacity for the ship's instances, its light, its materials, the
    // plumes' grid at `detail` and `splats` splats.
    ShipAsset(SceneStatic& scene, SceneDetail detail, std::uint32_t splats);

    // Appends the ship's instances placed by `modelToRender` (rigid) and its dash light to a frame.
    void place(SceneFrame& frame, const rt::Mat34& modelToRender) const;
    // Sets the ship's materials in a frame's table (the whole table, as SceneFrame::materials holds it): the
    // seams at the frame's kick, the engines at `throttle`, the strobe on the beat.
    void animate(std::vector<rt::Material>& table, const show::TimelineFrame& frame, float throttle) const;
    // The plumes at `throttle` behind a ship placed by `modelToRender`, the grid scaled with the asset's
    // detail (at least 16 voxels per axis).
    GridMediumDesc plumes(float throttle, const rt::Mat34& modelToRender) const;

    const ShipCounts& counts() const { return counts_; }
    std::array<std::uint32_t, 3> plumeGrid() const { return grid_; }
    // Material indices in the scene's table (for tests): the glowing ceramic, the strobe, the throats.
    std::uint32_t glowMaterial() const;
    std::uint32_t strobeMaterial() const;
    std::uint32_t throatMaterial() const;

private:
    std::uint32_t firstMesh_ = 0, firstMaterial_ = 0;
    std::vector<SceneInstance> instances_; // model space, the asset's own mesh and material indices
    std::uint32_t dashInstance_ = 0;       // which of them is the dash light
    rt::Mat34 dashTransform_{};
    ShipCounts counts_;
    std::array<std::uint32_t, 3> grid_{};
};

} // namespace evo::modern
