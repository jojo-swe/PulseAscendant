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
// PathTracer::trace() refuses an instance and light that describe different emitters: the light must not be
// a Portal; an Emitter light's emission is the material's emission * emissionScale, on a material without
// the retro texture; a RetroScreen light's is (s, s, s), s = the emissionScale of a kRetroScreenTexture
// material of class Emissive; and the transform must not mirror. The instance should be the very quad(w, h)
// that rectLightForQuad(transform, w, h, ...) describes (not checked).
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
    // The luminance limit of a contribution after the first bounce (TraceSettings::clampIndirect), in the
    // scene's own units: 64 by default; a scene lit far below that sets its own (the Studio, 2).
    float clampIndirect = 64.f;
};

// The light of a quad(width, height) mesh placed by `transform`: corner = the quad's bottom-left corner,
// edgeU its bottom edge (left to right), edgeV its left edge (bottom to top), so it emits along the quad's
// transformed +z, like the mesh. A RetroScreen light reads the retro picture at uv = (u, 1 - v), the quad's
// own uv, times `emission` (= the screen material's emissionScale on every channel). Portal lights ignore
// `emission` (their radiance is the sky). Throws std::invalid_argument for a mirroring transform (negative
// determinant): its edges would make the light face away from the side the mesh's front faces.
rt::RectLight rectLightForQuad(const rt::Mat34& transform, float width, float height, rt::LightKind kind,
                               Vec3 emission, float selectWeight);

// The determinant of a transform's linear part (negative: it mirrors, turning front faces into back faces).
float linearDeterminant(const rt::Mat34& transform);

// A right-handed orthonormal view basis: forward = normalize(target - eye), right = normalize(forward x up),
// up = right x forward.
struct CameraBasis {
    Vec3 right, up, forward;
};
CameraBasis cameraBasis(const Camera& camera);

} // namespace evo::modern
