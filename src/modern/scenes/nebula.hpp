#pragma once

// The Nebula scene: PLASMA's 2026 half and NEBULA, both a grid medium baked every frame by
// shaders/modern/nebula_medium.comp and lit from inside by stars, which are the frame's light tree (an
// emissive sphere field from shaders/modern/nebula_stars.comp, whose C++ twin is nebulaStar below).
//
// NEBULA (chapter 14): a flight into a star-forming cloud, an emission and absorption nebula on a
// kNebulaGrid^3 grid. A dust ridge runs across the bottom of the cloud and four pillars rise from it; three
// hot stars light the gas: A (the hero, behind the pillars), B (deep in the gap between the left pillars)
// and C (embedded in the right pillar's head, lighting it from inside), with kNebulaEmbeddedStars faint
// young stars buried in the pillars and the ridge. The bake marches from every voxel towards A, B and C
// through the cloud, so the ionising flux reaching a voxel falls with the gas in front of it: the emission
// (recombination, ~ density^2 x flux) is concentrated in the thin skins of the pillars that face a star
// (the ionisation fronts, hard-edged), ice-white where the flux is strong and gold where it is weaker, with
// a deeper ember glow of heated dust behind them; the far sides are dark dust that only scatters starlight
// (albedo ~0.64, reddening, Henyey-Greenstein g 0.6). Knots in the pillar heads pulse with the bass, star A's
// radius breathes with the energy, and kNebulaBackgroundStars splats (shaders/modern/nebula_splats.comp)
// sit far behind the cloud, dimmed by it.
//   shot 0, until 4 bars before the downbeat D at or before the accent: a slow 24 mm drift towards the wall
//          of pillars from outside the cloud, stars behind;
//   shot 1, from there: into the gap between the left pillars, the ionisation front on the left (B faces
//          it); on D star A starts to come into view round the centre pillar's edge, on the accent its disc
//          clears it and its forward scatter fans towards the camera; the camera keeps going through the
//          gap and swings slowly round A to the end.
//
// PLASMA (chapter 3): the 1993 plasma's own sum of sines (shaders/retro/plasma.comp) in (x, y), extruded
// along z into a slab with domain warping; its emission is the 1993 palette sampled the way the 1993 effect
// samples it (the field folded kPlasmaFolds times round the 224-entry ring), rotated by one entry per 1993
// frame, so across the wipe line the colours continue. The camera frames the slab so that its mid-plane
// covers the 1993 screen's 4:3 picture exactly, holds through the wipe, then dollies in (24 mm) and crosses
// into the cloud on the accent; two embedded stars light the dust.
//
// PLASMA PLANET (chapter 22): PLASMA's field at planetary scale. A planet of kPlasmaPlanetRadius whose sea is
// the 1993 plasma glowing in its palette ring under plates of basalt crust (an analytic sphere with the
// PlasmaSea pattern, shaders/rt/plasma_sea.glsl, the palette cycling at the 1993 rate), wrapped in an
// atmosphere and a layer of cumulus baked into the grid medium (the bake's mode 2): the air's single
// scattering of the sun (Rayleigh and Mie, its extinction towards the sun and the planet's shadow included,
// seen from the frame's camera) is baked as the grid's emission, so the limb glows blue in forward scatter and
// the terminator burns orange; the clouds scatter for real (the sun, and the sea's glow from below at night).
// The sun is the environment's disc, kPlasmaPlanetSun, fixed: the camera moves, the planet does not.
//   shot 0, until two bars before D: from orbit over the night side, the sea the only light, the limb's air
//          a thin line above it;
//   shot 1: the camera cranes up the tangent towards the sun; on D its disc clears the limb, exactly
//          (plasmaPlanetSunClearance crosses 0 on D);
//   shot 2, from D: towards the sun and down over the terminator, which crosses the frame as the camera
//          descends into the morning;
//   shot 3, from five bars after D: a low pass over the cloud tops above the plasma sea, 24 mm, into the low
//          sun, the clouds' shadows on the sea. The orbit shots bake a kPlasmaPlanetOrbitGrid^3 grid over the
//          whole planet; the low pass bakes the cloud shell over its stretch of the sea at kPlanetCloudGrid.
//
// World units are arbitrary (the cloud is kNebulaBox wide); +y up. Grid sizes scale with SceneDetail's cube
// root (at least kMinNebulaGrid per axis), star splats with SceneDetail; the card's numbers are the
// full-detail configuration (scene_nebula_test pins them).

#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "rt/gpu_types.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>

namespace evo::modern {

// The full-detail configuration (the cards' numbers).
inline constexpr std::uint32_t kNebulaGrid = 256;                       // NEBULA's voxels per axis
inline constexpr std::array<std::uint32_t, 3> kPlasmaGrid{288, 152, 128}; // PLASMA's slab
inline constexpr std::uint32_t kNebulaStars = 3;                        // A, B and C: the card's "3 STARS"
inline constexpr std::uint32_t kNebulaEmbeddedStars = 29;               // faint young stars in the dust
inline constexpr std::uint32_t kPlasmaStars = 2;
inline constexpr std::uint32_t kNebulaBackgroundStars = 131'072;
inline constexpr std::uint32_t kMinNebulaGrid = 16;
inline constexpr std::uint32_t kMinNebulaSplats = 512;

// PLASMA PLANET: the cloud shell's voxels along, across and up the low pass (the card's 256x128x32), the
// orbit grid's per axis, the planet, its atmosphere's top and the cloud layer (altitudes above the sea).
inline constexpr std::array<std::uint32_t, 3> kPlanetCloudGrid{256, 128, 32};
inline constexpr std::uint32_t kPlasmaPlanetOrbitGrid = 256;
inline constexpr float kPlasmaPlanetRadius = 1000.f;
inline constexpr float kPlasmaPlanetAir = 60.f;
inline constexpr float kPlasmaPlanetCloudBase = 2.f, kPlasmaPlanetCloudTop = 5.5f;
// The plasma's pixels per radian of the sphere (the 1993 picture, 320 pixels wide, spans about 1.1 radians).
inline constexpr float kPlasmaPlanetPixels = 290.f;

inline constexpr const char* kNebulaMediumShader = "modern_nebula_medium.comp.spv";
inline constexpr const char* kNebulaStarsShader = "modern_nebula_stars.comp.spv";
inline constexpr const char* kNebulaSplatsShader = "modern_nebula_splats.comp.spv";

// The two clouds' boxes (the grids' worldToMedium is mediumBox(lo, hi)).
inline constexpr Vec3 kNebulaBoxLo{-80.f, -60.f, -105.f}, kNebulaBoxHi{80.f, 100.f, 55.f};
inline constexpr Vec3 kPlasmaBoxLo{-27.f, -14.25f, -12.f}, kPlasmaBoxHi{27.f, 14.25f, 12.f};
// PLASMA's mapping of the 1993 screen: a 1993 pixel is kPlasmaPixel world units wide and kPlasmaPixel *
// 1.2 high (the 320x200 pixels are 1.2 times taller than wide), centred on (160, 100) at x = y = 0.
inline constexpr float kPlasmaPixel = 0.1f;
inline constexpr std::uint32_t kPlasmaFolds = 3;   // shaders/retro/plasma.comp's kFolds
inline constexpr float kPlasmaRingRate = 35.f;     // ring entries per second: one per 1993 frame

// The medium's scattering (albedo about 0.38 on average, reddening: dark dust that reads amber where lit) and
// phase.
inline constexpr Vec3 kNebulaAlbedo{0.5f, 0.37f, 0.26f};
inline constexpr float kNebulaPhaseG = 0.6f;

// The pillars (axis from base to top, radius at each end, and how far the axis bows out at mid-height) and
// the three stars, as the bake shader has them. nebulaPillarDistance adds the wobble, bulges and heads.
struct NebulaPillar {
    Vec3 base, top;
    float baseRadius, topRadius;
    Vec3 bow;
};
inline constexpr std::array<NebulaPillar, 4> kNebulaPillars{{
    {{-40.f, -48.f, -30.f}, {-44.f, 30.f, -36.f}, 11.f, 7.5f, {-3.f, 0.f, 2.f}},
    {{-2.f, -48.f, -38.f}, {6.f, 46.f, -46.f}, 12.f, 8.f, {4.f, 0.f, -2.f}},
    {{32.f, -48.f, -26.f}, {36.f, 14.f, -30.f}, 9.f, 6.5f, {-2.5f, 0.f, 3.f}},
    {{60.f, -48.f, -44.f}, {58.f, -6.f, -46.f}, 7.f, 5.f, {2.f, 0.f, 0.f}},
}};
inline constexpr std::array<Vec3, kNebulaStars> kNebulaStarCentres{{
    {20.f, 18.f, -90.f}, // A, the hero: behind the centre pillar
    {-27.f, -10.f, -26.f}, // B: low in the gap, facing the left pillar's flank
    {35.f, 16.f, -30.f},  // C: inside the right pillar's head
}};
inline constexpr std::array<float, kNebulaStars> kNebulaStarRadii{{0.9f, 0.8f, 0.5f}};

// The epoch card's numbers, from the full-detail configuration: "VOLUMETRIC PATH TRACING · 16,777,216
// VOXELS · 3 STARS".
struct NebulaStats {
    std::uint32_t gridSide;     // 256
    std::uint64_t voxels;       // 256^3 = 16,777,216
    std::uint32_t stars;        // the hero stars, 3
    std::uint32_t embeddedStars;
    std::uint32_t backgroundStars;
    std::uint64_t plasmaVoxels; // PLASMA's slab
};
NebulaStats nebulaStats();

// The grid a chapter bakes at a detail: the full size times cbrt(detail.scale) per axis, at least
// kMinNebulaGrid (NEBULA: kNebulaGrid^3; PLASMA: kPlasmaGrid; PLASMA PLANET: plasmaPlanetGrid's; any other
// effect NEBULA's).
std::array<std::uint32_t, 3> nebulaGridSize(show::Effect effect, SceneDetail detail);
std::uint32_t nebulaSplatCount(SceneDetail detail);

// The star field's params (the shader's vec4 params[4]): [0] mode (0 NEBULA, 1 PLASMA), [1] count, [2] energy
// (A's radius breathes with it), [3] kick.
std::array<float, 16> nebulaStarParams(const show::TimelineFrame& frame);
std::uint32_t nebulaStarCount(show::Effect effect);
// The shader's particle(id, seconds, params) on the CPU (prevCenter and material left 0): the stars are
// fixed in the cloud, so it does not depend on seconds.
rt::SphereRecord nebulaStar(std::uint32_t id, float seconds, const std::array<float, 16>& params);

// The bake's params (vec4 params[4]): [0] mode (0 NEBULA, 1 PLASMA), [1] the field's time (chapter seconds),
// [2] bass, [3] energy; [4..6] the box's lo, [8..10] its hi; NEBULA [12] the ionising opacity per unit
// density, [13] the grid's density scale; PLASMA [12] the ring position's shift (kPlasmaRingRate * chapter seconds, continuous), [13] the
// emission gain. Also the medium the frame carries.
std::array<float, 16> nebulaMediumParams(const show::TimelineFrame& frame);
GridMediumDesc nebulaMedium(const show::TimelineFrame& frame, SceneDetail detail);

// The 1993 plasma (shaders/retro/plasma.comp) on the CPU: its field in [-1, 1] at a pixel (fractional
// pixels allowed) and retro time, its ring index, and the palette ring's colour (sRGB in [0, 1]) at a ring
// entry; nebula_medium.comp evaluates the same functions.
float plasmaField(float px, float py, float retroSeconds);
std::uint32_t plasmaRingIndex(float field);
Vec3 plasmaRingColour(std::uint32_t entry);

// The signed distance from p to the pillars before the bake's noise (whose displacement is at most
// kNebulaPillarNoise): each a tapered tube round a bowed, wobbling axis, with bulges and a swollen head.
float nebulaPillarDistance(Vec3 p);
inline constexpr float kNebulaPillarNoise = 4.f;

// PLASMA PLANET's sun (a unit direction; the environment's disc) and its moments, chapter seconds: the shot
// changes on bar lines (shot 1 two bars before D, shot 3 five bars after), and D, the sunrise.
Vec3 plasmaPlanetSun();
struct PlasmaPlanetTimes {
    double craneStart, sunrise, lowPass, barSeconds;
};
PlasmaPlanetTimes plasmaPlanetTimes(const show::TimelineFrame& frame);
// How far the ray from `eye` towards the sun passes clear of the planet (the sea's sphere): its closest
// approach to the centre minus the radius, or +infinity when the sun is up in front of the eye's own
// horizon plane; < 0: the sun is behind the planet.
float plasmaPlanetSunClearance(Vec3 eye);
// The grid a PLASMA PLANET frame bakes: the whole planet from orbit (shots 0-2), the low pass's cloud shell
// (shot 3); lo and hi of the box, and the size at a detail.
struct PlasmaPlanetGrid {
    Vec3 lo, hi;
    std::array<std::uint32_t, 3> size;
};
PlasmaPlanetGrid plasmaPlanetGrid(const show::TimelineFrame& frame, SceneDetail detail);
// The sea's material parameters at a frame: (the plasma's time in 1993 seconds, the ring's rotation in
// entries, the cracks' glow) as the PlasmaSea pattern reads them from baseColor2.
Vec3 plasmaSeaParams(const show::TimelineFrame& frame);

// The cuts and shots (pure functions of the chapter's beat grid): NEBULA's downbeat at or before the accent
// (D) and the bar line 4 bars before it.
struct NebulaTimes {
    double downbeat, approachEnd, barSeconds;
};
NebulaTimes nebulaTimes(const show::TimelineFrame& frame);
Camera nebulaCamera(const show::TimelineFrame& frame);
float nebulaExposureEV(const show::TimelineFrame& frame);

class Nebula final : public ModernScene {
public:
    explicit Nebula(SceneDetail detail = {});

    enum MaterialIndex : std::uint32_t { kStar, kSea, kMaterialCount };

    const SceneStatic& staticScene() const override { return static_; }
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    Camera camera(const show::TimelineFrame& frame) const override { return nebulaCamera(frame); }
    bool samplesRetro() const override { return false; }
    std::string nowCard(show::Effect effect) const override;
    SceneDetail detail() const { return detail_; }

private:
    SceneFrame planetFrame(const show::TimelineFrame& frame) const;

    SceneDetail detail_;
    SceneStatic static_;
};

} // namespace evo::modern
