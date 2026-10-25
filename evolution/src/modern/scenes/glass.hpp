#pragma once

// The Glass scene (glass): the 2026 side of GLENZ (chapter 5), GLASS CATHEDRAL (15) and CRYSTAL
// CITY (24). Refraction with total internal reflection, spectral dispersion (shaders/rt/spectral.glsl),
// Beer-Lambert absorption inside thick glass, thin-walled stained glass, and caustics from photon maps
// (modern/caustics.hpp) that land sharp, in colour, on the floors.
//
// GLENZ: Future Crew's glenz star (retro::glenzMesh, Second Reality 1993, Unlicense; credited in
//   licenses/FUTURE-CREW-NOTICE.md) as flint glass (IOR 1.62, Abbe 30) around a cobalt core of the same star
//   scaled 0.82, index-matched (IOR 1) so it shows only by its absorption: the 1993 two-colour glenz. It spins
//   and bounces exactly as in 1993 (retro::glenzWorldVertices, scaled to metres), over a white cyclorama
//   under one small, strong key light whose photons throw rainbow caustics on the floor; the caustics tighten
//   as the star drops onto each beat and fan out into spectra as it rises. The 1993 copper bars are six neon
//   tubes riding the same drum behind it, at the copper's angles and colours. On the accent the tubes go dark
//   for a bar and the key light flares.
// GLASS CATHEDRAL: a nave of faceted glass pillars crowned by glenz stars under stone arches and a pointed
//   vault, with stained-glass lancets (thin-walled glass) along both walls and a rose window in the apse. The
//   sun breaks through the clouds on the accent and the east windows lay pools of coloured light, photon by
//   photon, across the flagstones.
// CRYSTAL CITY: quartz towers (hexagonal glass prisms with pyramidal tips, thick enough to turn sea-green by
//   absorption) on the 1993 city's lots (retro::cityLot) at sunrise; the low sun throws their dispersed
//   caustics down the streets. The camera flies the main street and rises over the roofs on the accent.
//
// World units are metres, +y up. Every frame is a pure function of the timeline frame.

#include "modern/caustics.hpp"
#include "modern/environment.hpp"
#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "retro/glenz.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace evo::modern {

// --- The cards' numbers (full detail) -------------------------------------------------------------------

// Photons per frame at full detail; exports at 16 spp and more shoot kExportPhotonFactor times as many.
inline constexpr std::uint32_t kGlenzPhotons = 1u << 19;     // an export's: 2,097,152
inline constexpr std::uint32_t kCathedralPhotons = 1u << 20; // an export's: 4,194,304
inline constexpr std::uint32_t kCrystalPhotons = 1u << 20;
inline constexpr std::uint32_t kCrystalPrisms = 400;

// What each chapter's 2026 card states (full detail, an export frame).
struct GlassCard {
    std::uint32_t exportPhotons; // caustic photons per export frame
    std::uint32_t glassObjects;  // GLENZ: the two stars; CATHEDRAL: the glass pillars; CITY: the prisms
    float abbe;                  // the dispersive glass's Abbe number (0: none)
    std::string line;            // the card's text, its numbers from the configuration
};
GlassCard glassCard(show::Effect effect);

// --- GLENZ ------------------------------------------------------------------------------------------------

// The 1993 glenz in metres: world = kGlenzWorldScale * retro + (0, kGlenzWorldLift, 0), so the star's lowest
// possible point (its centre at retro::kGlenzFloor, a corner straight down) clears the floor (y = 0) by
// kGlenzFloorGap.
inline constexpr float kGlenzWorldScale = 0.35f;
inline constexpr float kGlenzFloorGap = 0.015f;
float glenzWorldLift();
inline constexpr float kGlenzCoreScale = 0.82f;
// The core's turn in the star's glass about the star's own y axis (fixed, as cast): its faces meet the star's
// at angles, so wherever light crosses the core it crosses prisms.
inline constexpr float kGlenzCoreTurn = 0.7853981634f;
// The star: dense flint (N-SF10: a high index for total internal reflection, strong dispersion for fire)
// around a core of denser glass (N-SF66), cobalt-tinted.
inline constexpr float kFlintIor = 1.728f, kFlintAbbe = 28.5f;
inline constexpr float kCoreIor = 1.923f, kCoreAbbe = 20.9f;
// The Abbe number of the interface between two glasses (index b inside index a): (n - 1) / (n_F - n_C) of
// their ratio n = b / a, from each glass's n_F - n_C = (n - 1) / V.
constexpr float relativeAbbe(float a, float abbeA, float b, float abbeB) {
    const float n = b / a;
    return (n - 1.f) / (n * ((b - 1.f) / abbeB / b - (a - 1.f) / abbeA / a));
}

// The glenz's instance transform (the retro model space to the world) at `seconds` into the chapter, in
// continuous time: the same spin and bounce as retro::glenzWorldVertices at seconds = retroFrame / 35.
rt::Mat34 glenzTransform(const show::TimelineFrame& frame, double seconds);
// retro::glenzWorldVertices(frame, retroFrame) mapped into the 2026 world.
retro::GlenzVertices glenzVerticesIn2026(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// --- The scene ----------------------------------------------------------------------------------------------

// The cathedral's stained-glass panes (both walls' lancets and the rose), and its glass pillars.
std::uint32_t stainedPaneCount();
std::uint32_t glassPillarCount();
// How far the cathedral's east shutter of a window (bay 0 .. 14) is open at a frame, 0 to 1.
float cathedralShutter(const show::TimelineFrame& frame, int window);
// The direction towards the cathedral's sun (unit), and the middle of the hero window's glass (the bay whose
// shutter opens on D, the sun's disc in it).
Vec3 cathedralSunDirection();
Vec3 cathedralHeroWindow();
// The distance from a point to the nearest glass pier's axis (the piers stand in four rows of 16), in metres;
// and the outline's largest radius about the axis.
float distanceToPier(Vec3 p);
float pierReach();

// Test hooks: the glass's Abbe number in GLENZ and CRYSTAL CITY (0: no dispersion), and a clear core.
struct GlassOptions {
    float abbe = kFlintAbbe;
    bool clearCore = false; // GLENZ's core without its cobalt absorption
};

// A chapter's camera: authored shots (Catmull-Rom keys under smoothstep, cut on bar lines). GLENZ opens on the
// 1993 camera and cranes up to show the floor, orbits until the accent, then drops to the floor; CATHEDRAL
// dollies down the nave, cuts low across the floor on the accent and cranes up to the rose window; CRYSTAL
// CITY flies the main street, rises over the roofs on the accent and drifts across the skyline.
Camera glassCamera(const show::TimelineFrame& frame);
// Where each shot starts, in seconds into the chapter (the first at 0).
std::vector<float> glassShotStarts(const show::TimelineFrame& frame);
float glassExposureEV(show::Effect effect);

class Glass final : public ModernScene {
public:
    explicit Glass(SceneDetail detail = {}, GlassOptions options = {});

    const SceneStatic& staticScene() const override { return static_; }
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    Camera camera(const show::TimelineFrame& frame) const override { return glassCamera(frame); }
    bool samplesRetro() const override { return false; }
    std::string nowCard(show::Effect effect) const override;

    // The crystal towers of CRYSTAL CITY (one per instance), and the stained-glass panes of the cathedral.
    std::uint32_t crystalTowers() const { return static_cast<std::uint32_t>(towers_.size()); }
    std::uint32_t stainedPanes() const { return panes_; }

    struct Tower {
        Vec3 base;            // the centre of its foot, on the ground
        float radius, height; // across the corners of the hexagon; to the tip
        float twist;          // radians about y
    };
    const std::vector<Tower>& towers() const { return towers_; }

private:
    SceneFrame glenzFrame(const show::TimelineFrame& f) const;
    SceneFrame cathedralFrame(const show::TimelineFrame& f) const;
    SceneFrame crystalFrame(const show::TimelineFrame& f) const;

    SceneDetail detail_;
    GlassOptions options_;
    SceneStatic static_;
    std::vector<Tower> towers_;
    std::uint32_t panes_ = 0;
    std::vector<SceneInstance> cathedral_; // everything fixed
    std::vector<SceneInstance> pillars_;   // the glass pillars, bay by bay, row by row
};

// CRYSTAL CITY's towers: the first kCrystalPrisms of one per building of a block of the 1993 city's lots (retro::cityLot).
std::vector<Glass::Tower> crystalCityTowers();

} // namespace evo::modern
