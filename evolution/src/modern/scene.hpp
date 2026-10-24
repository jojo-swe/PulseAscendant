#pragma once

// What the path tracer renders: a scene's static part (meshes and materials, uploaded once) and one frame of
// it (camera, placed instances, area lights). Built in code (every 2026 asset is procedural).

#include "modern/math.hpp"
#include "rt/geometry.hpp"
#include "rt/gpu_types.hpp"

#include <cstdint>
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

// The meshes (BLAS per mesh) and materials of a scene. An instance's hit group is its material's
// materialClass.
struct SceneStatic {
    std::vector<rt::Mesh> meshes;
    std::vector<rt::Material> materials;
};

// One mesh placed in the world with a material. `light` names the SceneFrame light whose emitting surface
// this instance is (the MIS partner of a BSDF ray that hits it; its emission then shows on the light's
// emitting side only); kNoLight for every other instance, whose emission (if any) shows on both sides.
struct SceneInstance {
    std::uint32_t mesh, material;
    rt::Mat34 transform;
    std::uint32_t light = rt::kNoLight;
};

struct SceneFrame {
    Camera camera;
    std::vector<SceneInstance> instances;
    std::vector<rt::RectLight> lights;
    float exposureEV = 0;
};

// The light of a quad(width, height) mesh placed by `transform`: corner = the quad's bottom-left corner,
// edgeU its bottom edge (left to right), edgeV its left edge (bottom to top), so it emits along the quad's
// transformed +z, like the mesh. A RetroScreen light reads the retro picture at uv = (u, 1 - v), the quad's
// own uv, times `emission` (= the screen material's emissionScale on every channel). Portal lights ignore
// `emission` (their radiance is the sky).
rt::RectLight rectLightForQuad(const rt::Mat34& transform, float width, float height, rt::LightKind kind,
                               Vec3 emission, float selectWeight);

// A right-handed orthonormal view basis: forward = normalize(target - eye), right = normalize(forward x up),
// up = right x forward.
struct CameraBasis {
    Vec3 right, up, forward;
};
CameraBasis cameraBasis(const Camera& camera);

} // namespace evo::modern
