#pragma once

// A frame's gravitational lens around a black hole (step 5, Task 25): SceneFrame::lensing, packed into
// rt::LensingUniforms (binding EVO_BINDING_LENSING) and read by shaders/rt/lensing.glsl.
//
// Main rays (the camera's, and every bounce's) follow the Schwarzschild null geodesic about `center` while
// they are inside the sphere of `influenceRadius`: x'' = -3/2 r_s h^2 x / |x|^5 (h = |x x x'|, conserved),
// whose orbits are exactly those of Binet's equation u'' + u = 3/2 r_s u^2, integrated by RK4 in steps of
// stepScale * |x| (at most `steps`), each step one straight traced segment. A ray that comes within 1.01 r_s
// is captured (black: a dark surface at the horizon, so splats behind it are hidden too); one that leaves the
// sphere gets the rest of its weak-field deflection, (r_s / b)(1 - sqrt(1 - b^2 / R^2)), as one turn at the
// boundary, and a ray from outside the same for its way in. A ray that misses the sphere is turned once at
// its origin by the weak-field deflection of its whole path. Capture happens for impact parameters below
// 3 sqrt(3) / 2 r_s = 2.598 r_s.
//
// `disc`: the frame's grid medium is the hole's accretion disc. The lens then marches the grid along every
// bent segment (emission and absorption, no scattering): the voxel's r is the density (times the grid's
// densityScale), g its rest-frame temperature in units of 10^4 K and b its brightness (times emissionScale).
// Each sample is Doppler-shifted and beamed by the gas's circular Keplerian orbit about the lens's y axis and
// redshifted by gravity, g = sqrt(1 - r_s / r) / (gamma (1 - beta . n)): its colour is a blackbody of g T and
// its radiance scales with g^4 (bolometric). The medium integrator leaves such a grid alone.

#include "modern/math.hpp"
#include "rt/gpu_types.hpp"

#include <cstdint>

namespace evo::modern {

inline constexpr std::uint32_t kGeodesicSteps = 48; // SINGULARITY's card: 48-STEP GEODESICS
inline constexpr std::uint32_t kMaxGeodesicSteps = 256;

struct LensingFrame {
    Vec3 center{};
    float massRadius = 1.f;       // r_s; 0 leaves every ray straight
    float influenceRadius = 30.f; // R
    std::uint32_t steps = kGeodesicSteps;
    float stepScale = 0.2f;
    bool disc = false;
};

// The uniforms lensing.glsl reads. Throws std::invalid_argument for a non-finite value, a negative mass
// radius, an influence radius not above 1.5 r_s, steps outside [1, kMaxGeodesicSteps] or a step scale outside
// (0, 1].
rt::LensingUniforms packLensing(const LensingFrame& lens);

// The impact parameter below which a ray from far away is captured: 3 sqrt(3) / 2 r_s.
inline constexpr double kCaptureImpactParameter = 2.598076211353316;

} // namespace evo::modern
