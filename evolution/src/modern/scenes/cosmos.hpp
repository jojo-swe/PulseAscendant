#pragma once

// The Cosmos scene (step 5, Task 24): GALAXY and ASCENDANT, Act III's sky.
//
// GALAXY: a spiral galaxy of kGalaxyStars splat stars (shaders/modern/cosmos_splats.comp; the model is
// shaders/modern/galaxy.glsl: a bulge of old stars with red giants among them, an exponential disc, and two
// logarithmic arms of young stars, blue on their spines; each star's colour is a blackbody of its
// temperature) with its dust as a grid medium of kGalaxyDust voxels over the disc
// (shaders/modern/cosmos_medium.comp): dark lanes along the arms' inner edges, a thin diffuse disc that shows
// edge-on as the lane across the bulge, filaments, and pink HII knots that glow on the spines. The camera
// approaches edge-on on an 85 mm lens with the lane black across the bulge (shot 0; it starts at REBOOT's
// crossing, where the director shows this scene at negative chapter time and the 1993 cube's 512 points
// become stars ahead of the camera, frozen on REBOOT's accent), then on the bar
// line swings above the plane on a 35 mm lens until it looks straight down at D; on the accent the bulge is
// centred and the arms fill the frame; on the bar after it dives into an arm. The bulge's stars breathe with
// the energy; the arms stay steady.
//
// ASCENDANT: the same galaxy hangs in the sky behind the EVOLUTION logo (logo_targets.hpp). Its core holds
// kAscendantParticles traced particles (shaders/modern/cosmos_particles.comp; ascendantParticle() is the CPU
// twin) in DOTS' identity colours across the logo (ember on the left, ivory, ice on the right; the front face
// lighter than the sides). They lift off strand by strand from the start of shot 1 and stream towards the
// camera in filaments, pass through a waypoint formation one bar before D (held for four frames: a vortex by
// default, or a line of text: waypointTargets), and land on their logo targets exactly on the accent, lit by
// the sun from camera-right while the camera orbits on a 50 mm lens.
// From the fourth bar after D the logo's rows light up one per bar from the bottom; on the chapter's last
// downbeat the particles release and scatter outwards as light while the frame goes to phosphor white.
//
// World units: GALAXY's galaxy has radius kGalaxyRadius about the origin (disc in the xz plane); ASCENDANT's
// logo is centred on the origin, kLogoCell per font pixel, and its galaxy is kAscendantGalaxyCentre away.
// Counts scale with SceneDetail; the cards' numbers are the full-detail configuration (scene_cosmos_test).

#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "modern/scenes/logo_targets.hpp"
#include "rt/gpu_types.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace evo::modern {

inline constexpr std::uint32_t kGalaxyStars = 16'777'216;          // GALAXY's card: 16,777,216 STARS
inline constexpr std::array<std::uint32_t, 3> kGalaxyDust{256, 64, 256}; // and 256x64x256 DUST
inline constexpr std::uint32_t kAscendantParticles = 2'097'152;    // ASCENDANT's card: 2,097,152 PARTICLES
inline constexpr std::uint32_t kMinGalaxyStars = 4096, kMinAscendantParticles = 256;
inline constexpr const char* kCosmosSplatShader = "modern_cosmos_splats.comp.spv";
inline constexpr const char* kCosmosParticleShader = "modern_cosmos_particles.comp.spv";
inline constexpr const char* kCosmosMediumShader = "modern_cosmos_medium.comp.spv";

inline constexpr float kGalaxyRadius = 60.f;
inline constexpr float kGalaxyDustHalfHeight = 0.125f; // galaxy.glsl's: the dust box's y in +-this radius
inline constexpr float kLogoCell = 0.09f;              // the logo is 71 cells (6.4 units) wide
inline constexpr Vec3 kAscendantGalaxyCentre{10.f, 34.f, -320.f};
inline constexpr float kAscendantGalaxyRadius = 70.f, kAscendantGalaxyTilt = 1.05f, kAscendantGalaxyYaw = 0.4f;
inline constexpr float kWaypointHold = 4.f / 60.f; // the waypoint formation holds four frames

// The counts at a detail: round(full * scale), at least the minimum; the dust's voxels per axis scale with
// the cube root of the detail (at least 8).
std::uint32_t galaxyStarCount(SceneDetail detail);
std::uint32_t ascendantParticleCount(SceneDetail detail);
std::array<std::uint32_t, 3> galaxyDustSize(SceneDetail detail);

// galaxy.glsl's galaxyDust on the CPU: the dust's density and HII strength at a galaxy-space point (radius 1).
Vec2 galaxyDust(Vec3 p);
// The dust's density per world unit at a world point of GALAXY's galaxy turned by `turn` (what the grid holds
// before it is voxelised), for the dust-lane check.
float galaxyDustDensity(Vec3 world, float turn);

// ASCENDANT's moments, in chapter seconds, from the beat grid: the landing (the accent), the waypoint (the
// downbeat before D) and its hold, the release (the chapter's last downbeat), the bottom row's lighting (the
// fourth downbeat after D) and the bar's length.
struct AscendantTimes {
    float accent, waypoint, hold, release, firstRow, bar, liftStart, liftSpread;
};
AscendantTimes ascendantTimes(const show::TimelineFrame& frame);

// A text waypoint (formation 1): the particles pass through `text` in the 8x8 font on their way to the logo,
// filled as the logo's targets fill the logo (logo_targets.hpp's scheme, kWaypointDepth voxels deep, centred on
// the lit pixels' bounding box, the front face at z = +1 cell; shaders/modern/waypoint_text.glsl is the GPU
// twin). Characters are upper-cased; those outside ASCII 32..95 become '?'; at most kWaypointGlyphs show.
// `packed` is what the particle frame carries (params[12..15]: six-bit codes, four to a float).
inline constexpr std::uint32_t kWaypointGlyphs = 16, kWaypointDepth = 2;
struct WaypointTargets {
    std::string text;              // as spelled (upper case, cut to kWaypointGlyphs, trailing spaces dropped)
    std::array<float, 4> packed{}; // the particle frame's params[12..15]
    std::vector<LogoPixel> pixels; // the lit pixels, row-major over the line (column, row)
    Vec2 centre{};                 // their bounding box's centre (column, row)
    // Particle id's target in the text, in the text's frame (cells of `cell`).
    Vec3 target(std::uint32_t id, float cell) const;
};
WaypointTargets waypointTargets(const std::string& text);
std::string decodeWaypointText(const std::array<float, 4>& packed);

// The particle frame: params as cosmos_particles.comp documents them: formation 0 (the vortex), or the text
// waypoint when one is given.
ParticleFrame ascendantParticleFrame(const show::TimelineFrame& frame, std::uint32_t count,
                                     const WaypointTargets* waypoint = nullptr);
// The shader's particle(id, seconds, params) on the CPU (prevCenter and material left 0), and a particle's
// logo target and waypoint (the formation's text from params[12..15]), in the logo's frame.
rt::SphereRecord ascendantParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params);
Vec3 ascendantTarget(std::uint32_t id, float cell);
Vec3 ascendantWaypoint(std::uint32_t formation, std::uint32_t id, float cell, const std::array<float, 4>& text = {});

// REBOOT's crossing (its D, where the director switches to GALAXY's scene) and accent, in GALAXY's chapter
// seconds (negative), from any GALAXY frame; and the 1993 cube's star `id` (< 512) in the world as
// cosmos_splats.comp places it from a GALAXY splat frame's params (the CPU twin).
struct RebootMarks {
    float crossing, accent;
};
RebootMarks rebootMarks(const show::TimelineFrame& galaxyFrame);
Vec3 galaxyCubeStar(std::uint32_t id, const std::array<float, 16>& params);

// The galaxy's splats in either chapter (params as cosmos_splats.comp documents them).
SplatFrame galaxySplatFrame(const show::TimelineFrame& frame, std::uint32_t stars);
// The galaxy's turn at a frame (radians), shared by its stars and its dust.
float galaxyTurn(const show::TimelineFrame& frame);

Camera cosmosCamera(const show::TimelineFrame& frame);
float cosmosExposureEV(const show::TimelineFrame& frame);

class Cosmos final : public ModernScene {
public:
    explicit Cosmos(SceneDetail detail = {});

    enum MaterialIndex : std::uint32_t { kParticles, kFill, kRim, kMaterialCount };
    enum Light : std::uint32_t { kFillLight, kRimLight, kLightCount };

    const SceneStatic& staticScene() const override { return static_; }
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    Camera camera(const show::TimelineFrame& frame) const override { return cosmosCamera(frame); }
    bool samplesRetro() const override { return false; }
    std::string nowCard(show::Effect effect) const override;
    std::uint32_t starCount() const { return stars_; }
    std::uint32_t particleCount() const { return particles_; }
    const std::array<std::uint32_t, 3>& dustSize() const { return dust_; }

private:
    SceneStatic static_;
    std::uint32_t stars_, particles_;
    std::array<std::uint32_t, 3> dust_;
    std::vector<SceneInstance> lightInstances_;
    std::vector<rt::RectLight> lights_;
};

} // namespace evo::modern
