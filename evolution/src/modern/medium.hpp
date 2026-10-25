#pragma once

// A frame's participating media (step 5, Task 11): height fog and a baked density grid. The GPU side of the
// grid is MediumVolume (modern/medium_volume.hpp), which ModernStage keeps once for every scene.
//
// - Fog: sigma_t(y) = density * exp(-falloff * (y - baseHeight)) everywhere (falloff 0: homogeneous), with an
//   RGB single-scattering albedo and a Henyey-Greenstein phase function. Its optical depth along a ray has a
//   closed form, so the tracer integrates it analytically: an exact transmittance, and free-flight distances
//   sampled by inverting it (no majorant, no null collisions).
// - Grid: a box (worldToMedium maps it to [0, 1]^3) of size[0] x size[1] x size[2] voxels, baked every frame
//   by the scene's compute shader (SceneStatic::mediumShader) into an RGBA16F image: r the density (sigma_t =
//   densityScale * r per world unit), gba the emission (Le = emissionScale * gba; the medium emits
//   sigma_a * Le with sigma_a = sigma_t * (1 - albedo)). The shader defines the pure function
//       vec4 mediumVoxel(vec3 uvw, float seconds, vec4 params[4]);   // (density, emission rgb)
//   and includes shaders/modern/medium_common.glsl. A majorant per kMajorantCell^3 cell (with a one-voxel
//   apron, so it bounds the trilinear interpolation too) is computed from the baked voxels into an R16F
//   image; the tracer walks those cells with a DDA.
//
// The integrator (shaders/rt/medium.glsl, shaders/rt/medium_path.glsl and the marked hooks in
// pathtrace.rgen):
// - the primary segment (camera to the first surface, or to infinity): a residual ratio-tracking walk gives
//   the transmittance T and, on the same walk, the grid's emission as a quadrature at the grid's resolution
//   (about one sub-step per voxel, the control's exponential against the source linear between its ends),
//   so a glowing grid shows no sampling noise of its own; kPrimaryScatterSamples points sampled along the
//   segment (a mixture of the fog's collision density, the grid's majorant and, in a frame with 1 to
//   kEquiangularMaxLights tree lights, equiangular sampling towards them) each add a next-event estimate, so
//   the in-scattered radiance is dense (every pixel gets an estimate, which the denoiser's volume history
//   clamp needs); one of them, chosen uniformly, continues as a full path (multiple scattering). The volume
//   layer gets (L_v, T); the surface path is traced unattenuated and the denoiser's combine pass computes
//   T * surface + L_v;
// - every later segment: delta tracking (the fog analytically, the grid against its majorant cells), with
//   the collision estimator for the grid's emission, a scatter at a real collision (HG sampling, the albedo
//   as a weight) and next-event estimation from it with the phase function and MIS;
// - shadow rays: the surface occlusion test times the media's transmittance (the fog analytically, the grid
//   by residual ratio tracking).
// A sun or moon disc (EnvironmentDesc) through fog gives light shafts.

#include "modern/math.hpp"
#include "rt/geometry.hpp"
#include "rt/gpu_types.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace evo::modern {

// Height fog: sigma_t(y) = density * exp(-falloff * (y - baseHeight)), per world unit.
struct FogDesc {
    float density = 0, baseHeight = 0, falloff = 0;
    Vec3 albedo{.9f, .9f, .9f};
    float phaseG = .3f;
};

// A baked density grid over the box worldToMedium maps to [0, 1]^3 (see the file comment). size is at most
// the scene's SceneCapacity::mediumGrid per axis; params reach the bake shader as vec4 params[4].
struct GridMediumDesc {
    std::array<std::uint32_t, 3> size{128, 128, 128};
    rt::Mat34 worldToMedium = identity();
    float densityScale = 1;
    Vec3 albedo{.8f, .8f, .8f};
    float phaseG = .5f;
    float emissionScale = 1;
    std::array<float, 16> params{};
};

// The media of a frame: fog, a grid, both, or (an empty MediumFrame) none.
struct MediumFrame {
    std::optional<FogDesc> fog;
    std::optional<GridMediumDesc> grid;
};

// FrameUniforms::mediumFlags (shaders/rt/types.glsl has the same constants).
inline constexpr std::uint32_t kMediumFog = 1u, kMediumGrid = 2u;
// The majorant grid's cell (voxels per axis).
inline constexpr std::uint32_t kMajorantCell = 8;
// The largest grid per axis.
inline constexpr std::uint32_t kMaxMediumGrid = 1024;
// Points sampled on the primary segment per path sample (shaders/rt/medium_path.glsl).
inline constexpr std::uint32_t kPrimaryScatterSamples = 4;
// The most tree lights a frame may have for its primary segments to sample points equiangularly towards
// them (half the points; shaders/rt/medium_path.glsl).
inline constexpr std::uint32_t kEquiangularMaxLights = 64;

// The flags of a frame's media (0 for none).
std::uint32_t mediumFlags(const MediumFrame& medium);
// The uniforms medium.glsl reads (128 bytes, binding EVO_BINDING_MEDIUM): the grid's worldToMedium, voxel
// size, densityScale, albedo, phaseG, majorant cells and emissionScale, and the fog's albedo, phaseG,
// density, baseHeight and falloff (an absent medium's fields are 0). Throws std::invalid_argument for a
// non-finite value, a negative density, falloff or scale, an albedo outside [0, 1], |phaseG| >= 1, a grid
// size of 0 or above kMaxMediumGrid on an axis, or a worldToMedium that is not invertible.
rt::MediumUniforms packMedium(const MediumFrame& medium);

// The cells of the majorant grid over `size` voxels: ceil(size / kMajorantCell) per axis.
std::array<std::uint32_t, 3> majorantSize(std::array<std::uint32_t, 3> size);
// The worldToMedium of an axis-aligned box from lo to hi (each hi > lo).
rt::Mat34 mediumBox(Vec3 lo, Vec3 hi);

// CPU references (the shaders implement the same functions; medium_test holds them to these).
// The Henyey-Greenstein phase function: the density over the sphere of directions at cosTheta to the
// direction of travel (g > 0 scatters forward). Integrates to 1.
double henyeyGreenstein(double g, double cosTheta);
// The cosine to the direction of travel of a direction sampled with density henyeyGreenstein(g, .), from a
// uniform u in [0, 1).
double sampleHenyeyGreenstein(double g, double u);
// The fog's optical depth along origin + t * direction (unit) from t0 to t1 (t1 may be infinite).
double fogOpticalDepth(const FogDesc& fog, Vec3 origin, Vec3 direction, double t0, double t1);

} // namespace evo::modern
