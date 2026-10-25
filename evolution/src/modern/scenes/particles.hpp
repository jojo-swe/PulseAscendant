#pragma once

// The Particles scene: DOTS and PARTICLE OCEAN, two million traced micro-spheres made every
// frame by one stateless compute shader (shaders/modern/particles_particles.comp, through rt::ParticleField;
// analytic spheres) whose CPU twins are dotsParticle and oceanParticle below.
//
// DOTS, the 2026 answer to 1993's 512 dots: kDotsParticles polished chrome micro-spheres (radius 2.2 mm in
// a 1.2 m formation) morph cube -> sphere -> torus knot -> the EVOLUTION logo -> knot -> sphere -> cube on
// retro::dotsSchedule's frames, so both eras change shape on the same beat. The solids spin and the logo
// sways as 1993's shapes do. 4 % of the particles are glowing cores in blue, ice, amber and gold, flashing on
// the kick; the logo lights 14 % of its particles. Between two shapes a band of flight sweeps down through
// the formation: the top leaves first (with a little hashed spread), blooms out, swirls about the vertical
// and rides three steps of a curl-noise flow whose amplitude follows the bass, its chrome turning ice blue
// and glowing while it flies; at every arrival each particle lies exactly on its target. The logo lands on
// the chapter's accent with a shock of light (the cores x3, the chrome flaring) that dies away in about a
// second, as the camera cuts to face it. The set: a dark glossy floor that mirrors the formation, a warm key
// and a faint fill soft box high on either side and a cool rim light behind, under a black starry sky; depth
// of field (aperture 2 cm) focused on the formation.
//
// PARTICLE OCEAN: kOceanParticles liquid-metal beads on a sunflower (Vogel) disc of radius kOceanRadius,
// lifted and rolled by eight Gerstner waves whose height follows the energy, floating on a black water mirror
// that runs to the horizon, under the night sky with a moon of 0.5 degrees 16 degrees up. Troughs read darker
// than crests, the disc's edge dissolves (the beads shrink over its last 12 %), and a third of the crest
// beads turn emissive cyan foam, flaring on kicks. The camera skims the swell towards the moon, then from the
// accent cranes up and back until the whole spiral sea lies below it with the moon's path across it.
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
inline constexpr float kDotsLogoCell = 0.038f; // the logo is 71 cells (2.7 m) wide
inline constexpr float kDotsRadius = 0.0022f;  // at full detail; scales with (full / count)^(1/3)
inline constexpr Vec3 kDotsCentre{0.f, 1.05f, 0.f};
inline constexpr float kOceanRadius = 44.f;
inline constexpr float kOceanWaterLevel = -0.6f; // the dark mirror under the beads

enum class DotsShape : std::uint32_t { Cube = 0, Sphere = 1, Knot = 2, Logo = 3 };

// The particle count at a detail: round(full * scale), at least kMinParticles.
std::uint32_t particleCount(std::uint32_t full, SceneDetail detail);

// A shape's target for particle `id` of `count`, in model space (unturned), and turned as at chapter time t
// (the solids spin by yaw 0.6 + 0.85 t then pitch 0.4 + 0.12 t; the logo sways), as the shader computes them.
Vec3 dotsTarget(DotsShape shape, std::uint32_t id, std::uint32_t count);
Vec3 dotsShapePoint(DotsShape shape, std::uint32_t id, std::uint32_t count, float t);
// The cube's lattice side: the largest n with n^3 <= count.
std::uint32_t dotsLatticeSide(std::uint32_t count);

// The particle frames: count, params (the shader's vec4 params[4]) and the material (set by the scene).
// DOTS: params 0..11 = the six morphs' (first, arrival) in chapter seconds (dotsSchedule's frames / 35),
// 12 = count, 13 = turbulence (0.12 + 0.18 bass, metres), 14 = flash (kick), 15 = 0. OCEAN: params 0 = count,
// 1 = kOceanRadius, 2 = wave scale (0.35 + 0.45 energy), 3 = flash (kick), 15 = 1.
ParticleFrame dotsParticleFrame(const show::TimelineFrame& frame, std::uint32_t count);
ParticleFrame oceanParticleFrame(const show::TimelineFrame& frame, std::uint32_t count);

// The shader's particle(id, seconds, params) on the CPU (prevCenter and material left 0).
rt::SphereRecord dotsParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params);
rt::SphereRecord oceanParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params);
rt::SphereRecord particlesParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params);

// The chapter cameras (pure functions of localSeconds, chapterDuration and accentSeconds):
//   DOTS   shot 0 until the accent: a slow orbit from the right towards the front, closing in;
//          shot 1 from the accent: frontal on the logo as it lands, then drifting left and up while the
//          shapes return. Aperture 2 cm, focused on the formation's centre.
//   OCEAN  one shot: skims the swell towards the moon, then from the accent rises and tilts down to reveal
//          the whole sea.
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
    // DOTS (or anything but OCEAN): the floor, the three lights and the particle field at kDotsCentre; OCEAN:
    // the sea's particle field alone under the night sky.
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
