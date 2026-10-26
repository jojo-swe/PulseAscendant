#pragma once

// The Particles scene: DOTS and PARTICLE OCEAN, two million traced micro-spheres made every
// frame by one stateless compute shader (shaders/modern/particles_particles.comp, through rt::ParticleField;
// Task 8) whose CPU twins are dotsParticle and oceanParticle below.
//
// DOTS, the 2026 answer to 1993's 512 dots: kDotsParticles polished metal micro-spheres (radius 2.2 mm in
// a 1.2 m formation). Until the BeatSwap settles (D) they keep 1993's schedule and camera, so every swap
// compares like with like: cube -> sphere -> torus knot on retro::dotsSchedule's frames, the solids spinning
// as 1993's do. After D the 2026 schedule (dotsSchedule2026) holds the knot while the camera starts a slow
// 50 mm orbit, then prints the EVOLUTION logo one row a beat over the 7 beats before D + 4 bars, where it
// lands: a shock of light, a ring of light across the floor that spreads for a bar, and sparks lifting off
// it towards a low 35 mm camera seen from 15 degrees below; focus follows the sparks, then pulls back to the
// logo's face over the chapter's last bar. Colour is identity, not state: each particle keeps a colour from
// its place in the cube's lattice, ember (bottom) through ivory to ice (top), so a morph shows structure moving;
// on the logo the face turns chrome and the sides ember. A morph is a narrow band of flight sweeping down the
// target shape whose leading edge glows; into the logo the particles fly in threes, two trailing the first
// along its velocity (a glowing streak 3 radii long). 4 % are glowing cores flashing on the kick. The set: a
// dark glossy floor that mirrors the formation, a warm key and a faint fill soft box high on either side
// and a cool rim light high behind, under a black starry sky.
//
// PARTICLE OCEAN: kOceanParticles liquid-metal beads on a sunflower (Vogel) disc of radius kOceanRadius,
// lifted and rolled by eight Gerstner waves (shaders/rt/sea_waves.glsl) whose height follows the energy,
// floating on a black water mirror that runs into a low sea fog, under the night sky with a moon 10 degrees
// up. Each bead is a little mirror pulled onto the swell's normal (rt::Pattern::SeaBead, with its own
// roughness), so the moon's glitter path is the sea's one bright line; troughs read darker than crests, the
// disc's edge dissolves into the fog, and one crest bead in eight turns emissive ice-blue foam, flaring on
// kicks. The beads are jittered off the sunflower's spiral and vary in size. One shot on the 24 mm lens: a skim 30 cm over the beads towards the moon, tilted down so the
// horizon stays out of frame; from D it eases to a stop and cranes up and back, the horizon entering the
// frame on the accent, until the whole sea lies below with the moon's path across it.
//
// World units are metres, +y up; the DOTS formation stands kDotsCentre above the floor (y = 0), the sea's
// mean level is y = 0. The particle counts scale with SceneDetail (at least kMinParticles); the cards' number
// is the full-detail count (scene_particles_test pins it).

#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "rt/gpu_types.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace evo::modern {

inline constexpr std::uint32_t kDotsParticles = 2'097'152;  // the DOTS card's 2,097,152 (128^3)
inline constexpr std::uint32_t kOceanParticles = 2'097'152; // the PARTICLE OCEAN card's
inline constexpr std::uint32_t kMinParticles = 256;
inline constexpr const char* kParticlesShader = "modern_particles_particles.comp.spv";

// The DOTS shapes, in model space about the formation's centre (the shader's constants).
inline constexpr float kDotsCubeSide = 1.2f;      // a lattice of n^3 (128^3 at full detail)
inline constexpr float kDotsSphereRadius = 0.78f; // the outer of 4 shells, each 9 % smaller
inline constexpr std::uint32_t kDotsSphereShells = 4;
inline constexpr float kDotsShellStep = 0.09f;
inline constexpr float kDotsKnotMajor = 0.5f, kDotsKnotMinor = 0.22f, kDotsKnotLift = 1.4f,
                       kDotsKnotTube = 0.1f;
inline constexpr float kDotsLogoCell = 0.0684f; // the logo is 71 cells (4.9 m) wide
inline constexpr float kDotsLogoLift = 1.f;     // the logo's centre above the formation's
inline constexpr float kDotsRadius = 0.0022f;  // at full detail; scales with (full / count)^(1/3)
inline constexpr Vec3 kDotsCentre{0.f, 1.05f, 0.f};
inline constexpr float kOceanRadius = 44.f;
inline constexpr float kOceanWaterLevel = -0.6f; // the dark mirror under the beads

enum class DotsShape : std::uint32_t { Cube = 0, Sphere = 1, Knot = 2, Logo = 3 };

// The particle count at a detail: round(full * scale), at least kMinParticles.
std::uint32_t particleCount(std::uint32_t full, SceneDetail detail);

// A shape's target for particle `id` of `count`, in model space (unturned), and placed as at chapter time t
// (the solids spin by yaw 0.6 + 0.85 t then pitch 0.4 + 0.12 t and are mirrored in z, so that seen from +z
// they turn as 1993's do seen from -z; the logo sways, kDotsLogoLift up), as the shader computes them.
Vec3 dotsTarget(DotsShape shape, std::uint32_t id, std::uint32_t count);
Vec3 dotsShapePoint(DotsShape shape, std::uint32_t id, std::uint32_t count, float t);
// The cube's lattice side: the largest n with n^3 <= count.
std::uint32_t dotsLatticeSide(std::uint32_t count);

// The 2026 morph schedule of a DOTS frame, (first, arrival) in chapter seconds per morph of cube -> sphere
// -> knot -> logo -> knot -> sphere -> cube: the first two are retro::dotsSchedule's (frames / 35); the logo
// is printed over the 7 beats before D + 4 bars (dotsLanding) and holds to the end (the last three morphs
// never come).
std::array<std::array<float, 2>, 6> dotsSchedule2026(const show::TimelineFrame& frame);
inline constexpr float kDotsNever = 1e6f; // the time of a morph that never comes
// The logo's landing (D + 4 bars) and a bar's length there, in chapter seconds.
struct DotsLanding {
    float seconds, bar;
};
DotsLanding dotsLanding(const show::TimelineFrame& frame);

// The particle frames: count, params (the shader's vec4 params[4]) and the material (set by the scene).
// DOTS: params 0..11 = the six morphs' (first, arrival) in chapter seconds (dotsSchedule2026),
// 12 = count, 13 = turbulence (0.12 + 0.18 bass, metres), 14 = flash (kick), 15 = 0. OCEAN: params 0 = count,
// 1 = kOceanRadius, 2 = wave scale (0.35 + 0.45 energy), 3 = flash (kick), 15 = 1.
ParticleFrame dotsParticleFrame(const show::TimelineFrame& frame, std::uint32_t count);
ParticleFrame oceanParticleFrame(const show::TimelineFrame& frame, std::uint32_t count);

// The shader's particle(id, seconds, params) on the CPU (prevCenter and material left 0).
rt::SphereRecord dotsParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params);
rt::SphereRecord oceanParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params);
rt::SphereRecord particlesParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params);

// The chapter cameras (pure functions of the frame's chapter time and the beat grid):
//   DOTS   shot 0 until D + 1 bar: 1993's camera (4 m out on +z, 40 degrees high: 1993's 6 m for its 1.5x
//          larger shapes), still; shot 1 to D + 3 bars: a slow 50 mm orbit about the knot; shot 2: 35 mm,
//          craning down from beside the printing logo to 15 degrees below its centre by the landing, then a
//          slow push in. Aperture 4 cm in shot 2: focused on the logo's face as it lands, then on the sparks
//          as they lift towards the camera, and back to the face over the chapter's last bar.
//   OCEAN  one shot (24 mm): skims the swell towards the moon, then from D cranes up and back to reveal the
//          whole sea; the frame's top edge meets the horizon on the accent.
// Any other effect gets DOTS' camera.
Camera particlesCamera(const show::TimelineFrame& frame);
float particlesExposureEV(show::Effect effect);

class Particles final : public ModernScene {
public:
    explicit Particles(SceneDetail detail = {});

    // Materials: the floor, the key, fill and rim emitters, the DOTS particles, the OCEAN beads and its
    // water.
    enum MaterialIndex : std::uint32_t {
        kFloor,
        kKey,
        kFill,
        kRim,
        kDotsChrome,
        kOceanMetal,
        kWater,
        kMaterialCount
    };
    enum MeshIndex : std::uint32_t { kFloorMesh, kLightMesh, kMeshCount };
    enum Light : std::uint32_t { kKeyLight, kFillLight, kRimLight, kLightCount };

    const SceneStatic& staticScene() const override { return static_; }
    // DOTS (or anything but OCEAN): the floor (with the landing's ring of light), the three lights and the
    // particle field at kDotsCentre; OCEAN: the sea's particle field and the water under the night sky, in
    // a low sea fog.
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    Camera camera(const show::TimelineFrame& frame) const override { return particlesCamera(frame); }
    bool samplesRetro() const override { return false; }
    std::string nowCard(show::Effect effect) const override;
    std::uint32_t dotsCount() const { return dotsCount_; }
    std::uint32_t oceanCount() const { return oceanCount_; }

private:
    SceneStatic static_;
    std::uint32_t dotsCount_, oceanCount_;
    std::vector<SceneInstance> dotsInstances_;
    std::vector<rt::RectLight> dotsLights_;
};

} // namespace evo::modern
