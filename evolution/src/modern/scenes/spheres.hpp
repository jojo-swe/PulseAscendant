#pragma once

// Spheres (the Spheres scene): the 2026 side of RAYTRACE, SPHERE STORM and RAYTRACE INFINITY, all analytic
// spheres (analytic spheres), one scene object for the three chapters.
//
// - RAYTRACE: the 1993 "precalculated" raytrace (retro/raytrace.*) path traced: its six spheres at the same
//   places at the same time (retro::raytraceSpheres), the central chrome one, four chrome ones and the
//   emerald glass one orbiting it, plus 42 more chrome, gold, copper and coloured glass spheres bouncing on
//   the beat round them, over an endless red and white lacquered checkerboard (1 unit squares, as in 1993)
//   under a Day sky whose sun stands where 1993's point light was, and two large soft rect lights. Until the
//   accent the camera is 1993's (so the beat swaps show one composition); from the accent it breaks free.
// - SPHERE STORM: kStormSpheres stateless particle spheres (shaders/modern/particles_spheres.comp, mirrored
//   by spheresParticle) in a vortex over a black mirror floor at dusk; the first kStormEmissive glow and are
//   the light tree's lights.
// - RAYTRACE INFINITY: a lattice of chrome spheres without end, gold ones and glowing lanterns among them:
//   the kInfinitySpheres lattice points round the falling camera, recycled as it falls (the same particle
//   shader, mode 1). Until participating media's fog lands, the spheres shrink away into the sky with distance instead.
//
// World units are 1993's (the board's squares are 1 unit), +y up.

#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace evo::modern {

// The full-detail configuration (the cards' numbers are pinned against these).
inline constexpr std::uint32_t kRaytraceSphereCount = 48;       // RAYTRACE: 6 from 1993 and 42 more
inline constexpr std::uint32_t kRaytraceRetroSpheres = 6;       // the first six: retro::raytraceSpheres
inline constexpr std::uint32_t kStormSpheres = 32768;           // SPHERE STORM's particles
inline constexpr std::uint32_t kStormEmissive = 4096;           // of them the first that glow
inline constexpr std::uint32_t kInfinitySide = 32;              // RAYTRACE INFINITY: 32^3 lattice spheres
inline constexpr std::uint32_t kInfinitySpheres = 32 * 32 * 32; // round the camera
inline constexpr float kInfinitySpacing = 3.5f;                 // lattice spacing
inline constexpr float kInfinityRadius = 0.62f;                 // the chrome spheres' radius

// RAYTRACE's colossal mirror sphere on the horizon (after the 48 balls among the static spheres), and the
// side of its square of bevelled tiles round the camera (beyond it, a checker plane).
inline constexpr std::uint32_t kRaytraceGiants = 1;
inline constexpr std::uint32_t kRaytraceTileSide = 32;

// A chapter's drop: the bar line at or before its accent (where the BeatSwap settles on 2026), in
// chapter-local seconds and as the track's beat index (-1 without a beat grid there: the drop is then the
// accent), and the length of a bar there. Cuts and the big musical moments land on it.
struct SpheresDrop {
    double seconds;
    int beat;
    double barSeconds;
};
SpheresDrop spheresDrop(const show::TimelineFrame& frame);
// The first bar line at or after chapter-local `seconds` (`seconds` itself past the grid's end).
double nextBarLine(const show::TimelineFrame& frame, double seconds);
// Beats since the track's first beat at the frame, continuous (the beat index plus the phase through it).
double beatsAt(const show::TimelineFrame& frame);

// SPHERE STORM's particle count at a detail (kStormSpheres at 1, at least 64), and how many of them glow
// (an eighth: kStormEmissive at full detail).
std::uint32_t stormCount(SceneDetail detail);
std::uint32_t stormEmissiveCount(SceneDetail detail);

// How far SPHERE STORM's funnel has risen out of its spiral on the floor: 0 until four bars before the drop,
// 0.15 at the drop, then up to 1 over the bar after it.
float stormRise(const show::TimelineFrame& frame);

// The particle shader's params (shaders/modern/particles_spheres.comp) of a SPHERE STORM frame, mode 0:
// (count, emissive count, radius scale, 0), (energy, kick, rise, 0). The radius scale grows as the detail
// shrinks (the cube root of the lost count, at most 4), so a test's small field still fills about the same
// volume.
std::array<float, 16> stormParams(const show::TimelineFrame& frame, SceneDetail detail);

// RAYTRACE INFINITY's lattice side at a detail (kInfinitySide at 1, at least 4: the cube root of the
// detail), and the particle params of a frame whose camera stands at `eye`, `sinceDrop` seconds after the
// drop, mode 1: (count, 0, 0, 1), (sinceDrop, 0, 0, 0), (eye, spacing), (side, radius, fadeStart,
// fadeEnd). The spheres shrink to nothing from fadeStart to fadeEnd = (side / 2 - 1) spacing from the
// camera; the lanterns light up from the drop in a wave leaving the camera at 30 units a second.
std::uint32_t infinitySide(SceneDetail detail);
std::array<float, 16> infinityParams(Vec3 eye, SceneDetail detail, double sinceDrop);

// The C++ mirror of shaders/modern/particles_spheres.comp's particle(): centre, radius, colour, emission and
// prevCenter of particle `id` at chapter-local `seconds` (material 0), in either mode.
rt::SphereRecord spheresParticle(std::uint32_t id, float seconds, std::span<const float, 16> params);

// A RAYTRACE ball at a frame: its centre and radius.
struct RaytraceBall {
    Vec3 centre;
    float radius;
};
// 1993's six spheres (retro::raytraceSpheres, which evo_modern cannot link: mirrored here and pinned by
// spheres_scene_test) at chapter-local `seconds`, and 1993's camera (retro::raytraceCamera). Ball
// kRaytraceGlassBall is the glass one (retro::kRaytraceGlassSphere).
inline constexpr std::uint32_t kRaytraceGlassBall = 3;
std::array<RaytraceBall, kRaytraceRetroSpheres> raytraceRetroBalls(double seconds);
Camera raytraceRetroCamera();

// The 48 balls at a frame: 0..5 are raytraceRetroBalls(localSeconds), 6..47 stand round
// them and bounce on the beat (landing on beats; harder with energy).
std::array<RaytraceBall, kRaytraceSphereCount> raytraceBalls(const show::TimelineFrame& frame);

// The camera of a Spheres chapter's frame (a pure function of the effect, localSeconds, chapterDuration and
// accentSeconds):
//   RAYTRACE           shot 0 until the accent: 1993's camera (retro::raytraceCamera); shot 1: a low orbit
//                      round the central sphere with a shallow depth of field; shot 2 (the last third after
//                      the accent): a crane rising over the ring
//   SPHERE STORM       shot 0 until the accent: a slow orbit closing in on the vortex; shot 1: low under the
//                      funnel, looking up into it
//   RAYTRACE INFINITY  the camera falls through the lattice all chapter on a void column; shot 0 until the
//                      accent looks down along a diagonal, shot 1 along the lattice's z axis (the endless
//                      hall), shot 2 (the last third) straight down, rolling
// Any other effect gets RAYTRACE's.
Camera spheresCamera(const show::TimelineFrame& frame);

// The exposure (EV) of a chapter, chosen by eye.
float spheresExposureEV(show::Effect effect);

class Spheres final : public ModernScene {
public:
    // Builds the meshes, materials and static spheres once (CPU only). The detail scales the particle
    // counts only (SPHERE STORM's and RAYTRACE INFINITY's).
    explicit Spheres(SceneDetail detail = {});

    // The materials (indices into staticScene().materials).
    enum Material : std::uint32_t {
        kBoard,   // RAYTRACE's red and white checker plane under the tiles (Checker)
        kTileRed, // its tiles: three finishes of red, then three of white (Pbr, coated)
        kTileWhite = kTileRed + 3,
        kChrome = kTileWhite + 3, // polished chrome (Pbr)
        kGold,                    // polished gold (Pbr)
        kCopper,                  // satin copper under a clear coat (Pbr)
        kBrushedSteel,            // brushed steel (Pbr)
        kLacquer,                 // black lacquer (Pbr)
        kPearl,                   // coated pearl white (Pbr)
        kGlass,                   // clear glass, tinted per sphere (Glass)
        kSoftbox,                 // RAYTRACE's cool fill light (Emissive)
        kRim,                     // RAYTRACE's warm rim light (Emissive)
        kObsidian,                // SPHERE STORM's black mirror floor (Checker, two blacks)
        kParticleChrome,          // the particles (Pbr; colour and emission per particle)
        kMaterialCount
    };
    // The meshes: the floor (a quad facing +z, laid flat) and a softbox (a quad).
    enum Mesh : std::uint32_t { kFloorMesh, kSoftboxMesh, kRimMesh, kTileMesh, kMeshCount };
    static constexpr std::uint32_t kTileVariants = 3;
    // The static spheres are RAYTRACE's balls and then its giant, one range each (each moves on its own).

    // The floor's side (it follows the camera in steps of two squares, so the board never ends).
    static constexpr float kFloorSize = 8000.f;
    // RAYTRACE's two softboxes' size.
    static constexpr float kSoftboxWidth = 5.f, kSoftboxHeight = 3.f;

    const SceneStatic& staticScene() const override { return static_; }
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    Camera camera(const show::TimelineFrame& frame) const override { return spheresCamera(frame); }
    bool samplesRetro() const override { return false; }
    std::string nowCard(show::Effect effect) const override;

    SceneDetail detail() const { return detail_; }

private:
    SceneFrame raytraceFrame(const show::TimelineFrame& frame) const;
    SceneFrame stormFrame(const show::TimelineFrame& frame) const;
    SceneFrame infinityFrame(const show::TimelineFrame& frame) const;

    SceneDetail detail_;
    SceneStatic static_;
};

} // namespace evo::modern
