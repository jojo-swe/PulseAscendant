#pragma once

// What the path tracer renders: a scene's static part (meshes and materials, uploaded once) and one frame of
// it (camera, placed instances, area lights). Built in code (every 2026 asset is procedural).

#include "modern/caustics.hpp"
#include "modern/environment.hpp"
#include "modern/lensing.hpp"
#include "modern/math.hpp"
#include "modern/medium.hpp"
#include "modern/particles.hpp"
#include "modern/splats.hpp"
#include "rt/geometry.hpp"
#include "rt/gpu_types.hpp"
#include "rt/spheres.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace evo::modern {

// A thin-lens camera looking from eye at target; up need only not be parallel to the view direction.
// aperture is the lens diameter (0: a pinhole); focusDistance is measured along the view direction. shot
// counts the camera's cuts: a change resets temporal history.
struct Camera {
    Vec3 eye, target, up;
    float fovYDegrees;
    float aperture = 0;
    float focusDistance = 1;
    std::uint32_t shot = 0;
};

// The most a scene's frames ever hold, anywhere in its chapters: the path tracer sizes its per-frame
// buffers and TLAS for the largest over the scenes it has prepared, and refuses a frame that exceeds its
// scene's capacity (std::length_error). Every scene task sweeps its chapters against it (checkSceneSweep).
struct SceneCapacity {
    std::uint32_t instances = 1024, lights = 64, materials = 256, spheres = 0, particles = 0, splats = 0;
    // The largest caustic receiver resolution of any frame (Task 13; 0: the scene has no caustics). The
    // modern stage sizes its one CausticMaps to the largest prepared.
    std::uint32_t causticResolution = 0;
    std::uint32_t treeLights = 0; // the light tree's lights (Task 12), at most rt::kMaxTreeLights
};

// The meshes (BLAS per mesh) and materials of a scene, and what its frames may hold. An instance's hit group
// is its material's materialClass. `spheres` are its static analytic spheres (Task 8, at most
// capacity.spheres), built into one AABB BLAS per range of `sphereRanges` (empty: one range of them all); a
// static sphere instance places exactly one of those ranges. A sphere's `material` indexes the material
// table (the frame's, when it has one) and must have its instance's class. The shader paths name its
// particle, splat and medium compute shaders (empty: the scene has none; Tasks 8, 10 and 11); a relative
// path names a file in gfx::shaderDir() (modern_particles_<scene>.comp.spv).
struct SceneStatic {
    std::vector<rt::Mesh> meshes;
    std::vector<rt::Material> materials;
    SceneCapacity capacity;
    std::vector<rt::SphereRecord> spheres;
    std::vector<rt::SphereRange> sphereRanges;
    std::filesystem::path particleShader, splatShader, mediumShader;
};

// One mesh placed in the world with a material. `light` names the SceneFrame light whose emitting surface
// this instance is (the MIS partner of a BSDF ray that hits it; its emission then shows on the light's
// emitting side only); kNoLight for every other instance, whose emission (if any) shows on both sides.
// PathTracer::trace() refuses an instance and light that describe different emitters: the light must not be
// a Portal; an Emitter light's emission is the material's emission * emissionScale, on a material without
// the retro texture; a RetroScreen light's is (s, s, s), s = the emissionScale of a kRetroScreenTexture
// material of class Emissive; and the transform must not mirror. The instance should be the very quad(w, h)
// that rectLightForQuad(transform, w, h, ...) describes (not checked).
//
// A sphere instance (kind Spheres, Task 8) places spheres firstSphere .. firstSphere + sphereCount - 1 of the
// scene's static spheres (one of SceneStatic's sphere ranges), or the whole particle field (firstSphere 0,
// sphereCount = ParticleFrame::count, the field's material) when dynamicSpheres is set, instead of a mesh
// (`mesh` is then ignored). Its transform places the spheres (scale it uniformly: a sphere stays a sphere
// only then); it is no light (kNoLight), or the light tree's RectLight of kind Tree (Task 12: its spheres
// are the tree's lights, see TreeLightFrame).
struct SceneInstance {
    std::uint32_t mesh, material;
    rt::Mat34 transform;
    std::uint32_t light = rt::kNoLight;
    rt::GeometryKind kind = rt::GeometryKind::Triangles;
    std::uint32_t firstSphere = 0, sphereCount = 0;
    bool dynamicSpheres = false;
};

// One light of the frame's light tree (Task 12): an emissive sphere, in world space. SceneFrame::treeLights
// holds the tree's lights in order: light k is sphere firstSphere + k (`sphere`, its index in its buffer) of
// the one sphere instance whose `light` is the frame's one RectLight of kind Tree, which stands for the whole
// tree in light selection (its selectWeight scales the tree's importance; its other fields are 0). Static
// spheres or the first lights of the particle field (whose centres the scene computes from the C++ mirror of
// its particle function).
//
// `emission` is the sphere's radiance, its colour * emission + its material's emission * emissionScale
// (checked for static spheres); `scale` is this frame's factor on it (blinking): what next-event estimation
// samples and what a ray that hits the sphere sees is emission * scale. The path tracer builds the tree from
// centre, radius, emission and sphere (rt::buildLightTree) and keeps it while they stay the same, refitting
// it for the scales alone (rt::refitLightTree): blinking costs O(n), moving lights a rebuild. Either way the
// picture is a pure function of the frame.
struct TreeLightFrame {
    Vec3 center;
    float radius = 0;
    Vec3 emission;
    float scale = 1;
    std::uint32_t sphere = 0;
};

// What one frame shows. Beyond step 4's camera, instances, lights and exposure, step 5's features each have
// a member that is absent (empty, nullopt) in a frame without the feature; the path tracer then binds that
// feature's resources to dummies:
//   materials    empty: the scene's static table; else the whole table for this frame (a per-chapter light
//                rig, blinking windows), at most the scene's capacity.materials entries
//   environment  the sky (Task 2; default: step 4's night sky, never sampled by NEE)
//   particles    the particle field (Task 8); splats (Task 10); medium (Task 11); treeLights (Task 12);
//   caustics     (Task 13); lensing (Task 25)
struct SceneFrame {
    Camera camera;
    std::vector<SceneInstance> instances;
    std::vector<rt::RectLight> lights;
    float exposureEV = 0;
    // The luminance limit of a contribution after a rough vertex (TraceSettings::clampIndirect), in the
    // scene's own units: 16 by default (Task 6 of step 5), with path regularisation taking the rest of the
    // caustic noise.
    float clampIndirect = 16.f;
    std::vector<rt::Material> materials;
    EnvironmentDesc environment;
    std::optional<ParticleFrame> particles;
    std::optional<SplatFrame> splats;
    std::optional<MediumFrame> medium;
    std::vector<TreeLightFrame> treeLights;
    std::optional<CausticFrame> caustics;
    std::optional<LensingFrame> lensing;
};

// The light of a quad(width, height) mesh placed by `transform`: corner = the quad's bottom-left corner,
// edgeU its bottom edge (left to right), edgeV its left edge (bottom to top), so it emits along the quad's
// transformed +z, like the mesh. A RetroScreen light reads the retro picture at uv = (u, 1 - v), the quad's
// own uv, times `emission` (= the screen material's emissionScale on every channel). Portal lights ignore
// `emission` (their radiance is the sky). The light is seen through no portal (gate kNoLight). Throws
// std::invalid_argument for a mirroring transform (negative determinant): its edges would make the light
// face away from the side the mesh's front faces.
rt::RectLight rectLightForQuad(const rt::Mat34& transform, float width, float height, rt::LightKind kind,
                               Vec3 emission, float selectWeight);

// The tree lights of a sphere instance that places static spheres: its first `count` spheres in world space
// (the centre transformed, the radius times the transform's scale), their emission (colour * emission + the
// material's emission * emissionScale, from `materials`: the frame's table, or the scene's) and scale 1.
// Throws std::out_of_range for spheres beyond the instance's or the scene's, or a material that does not
// exist.
std::vector<TreeLightFrame> treeLightsOf(const SceneStatic& scene, const SceneInstance& instance,
                                         const std::vector<rt::Material>& materials, std::uint32_t count);

// The determinant of a transform's linear part (negative: it mirrors, turning front faces into back faces).
float linearDeterminant(const rt::Mat34& transform);

// A right-handed orthonormal view basis: forward = normalize(target - eye), right = normalize(forward x up),
// up = right x forward.
struct CameraBasis {
    Vec3 right, up, forward;
};
CameraBasis cameraBasis(const Camera& camera);

} // namespace evo::modern
