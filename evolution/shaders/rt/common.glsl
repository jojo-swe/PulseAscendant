#ifndef EVO_RT_COMMON_GLSL
#define EVO_RT_COMMON_GLSL

// What every path tracer shader shares: the descriptor set (src/modern/path_tracer.cpp binds it), the
// payloads and small helpers. Only pathtrace.rgen traces rays (recursion depth 1); closest-hit shaders fill
// the hit payload with the evaluated surface, the sky miss marks a miss with the sky's radiance, and the
// shadow miss marks a shadow ray unoccluded.

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require

#include "../common/color.glsl"  // luminance
#include "../common/finite.glsl" // isFinite, isInfinite, allFinite (from the bits: robust under fast-math)
#include "../../src/rt/bindings.hpp"
#include "types.glsl"

layout(set = 0, binding = EVO_BINDING_TLAS) uniform accelerationStructureEXT tlas;
layout(set = 0, binding = EVO_BINDING_LIGHTING, rgba32f) uniform image2D lightingImage;
layout(set = 0, binding = EVO_BINDING_EMISSION, rgba16f) uniform image2D emissionImage;
layout(set = 0, binding = EVO_BINDING_ALBEDO, rgba16f) uniform image2D albedoImage;
layout(set = 0, binding = EVO_BINDING_NORMAL, rgba16f) uniform image2D normalImage;
layout(set = 0, binding = EVO_BINDING_DEPTH, r32f) uniform image2D depthImage;
layout(set = 0, binding = EVO_BINDING_MOTION, rgba16f) uniform image2D motionImage;
layout(set = 0, binding = EVO_BINDING_FRAME, scalar) readonly buffer FrameBlock { FrameUniforms frame; };
layout(set = 0, binding = EVO_BINDING_MATERIALS, scalar) readonly buffer MaterialBlock { Material materials[]; };
layout(set = 0, binding = EVO_BINDING_LIGHTS, scalar) readonly buffer LightBlock { RectLight lights[]; };
layout(set = 0, binding = EVO_BINDING_INSTANCES, scalar) readonly buffer InstanceBlock {
    InstanceRecord instanceRecords[];
};
layout(set = 0, binding = EVO_BINDING_VERTICES, scalar) readonly buffer VertexBlock { Vertex vertices[]; };
layout(set = 0, binding = EVO_BINDING_INDICES, scalar) readonly buffer IndexBlock { uint indices[]; };
layout(set = 0, binding = EVO_BINDING_MESH_RECORDS, scalar) readonly buffer MeshRecordBlock {
    MeshRecord meshRecords[];
};
// The live 1993 picture (R8G8B8A8_SRGB, every mip level): sampling decodes it to linear light. Its own
// binding, not an array element, so synchronization validation sees it.
layout(set = 0, binding = EVO_BINDING_RETRO_SCREEN) uniform sampler2D retroScreen;

// The step 5 feature resources (src/rt/bindings.hpp): always bound, to a one-element dummy when the frame
// lacks the feature. The hook that reads each one (environment.glsl, medium.glsl, lights.glsl, caustics.glsl,
// lensing.glsl, ...) belongs to the task named in bindings.hpp. The single-struct blocks are named *Data
// (mediumData, lensingData) so that they do not read like the fields frame.lensing, Material.medium and
// HitPayload.medium; `environment` keeps its name (no field is called that).
layout(set = 0, binding = EVO_BINDING_ENVIRONMENT, scalar) readonly buffer EnvironmentBlock {
    EnvironmentUniforms environment;
};
layout(set = 0, binding = EVO_BINDING_TREE_NODES, scalar) readonly buffer TreeNodeBlock {
    LightTreeNode treeNodes[];
};
layout(set = 0, binding = EVO_BINDING_TREE_LIGHTS, scalar) readonly buffer TreeLightBlock {
    TreeLight treeLights[];
};
layout(set = 0, binding = EVO_BINDING_MEDIUM, scalar) readonly buffer MediumBlock { MediumUniforms mediumData; };
layout(set = 0, binding = EVO_BINDING_DENSITY) uniform sampler3D mediumDensity;
// The majorant is a per-cell maximum: Task 11 reads it with texelFetch (the bound trilinear sampler is then
// ignored), since interpolating maxima underestimates them and biases delta tracking.
layout(set = 0, binding = EVO_BINDING_MAJORANT) uniform sampler3D mediumMajorant;
layout(set = 0, binding = EVO_BINDING_PREVIOUS_TRANSFORMS, scalar) readonly buffer PreviousTransformBlock {
    Mat34 previousTransforms[];
};
layout(set = 0, binding = EVO_BINDING_STATIC_SPHERES, scalar) readonly buffer StaticSphereBlock {
    SphereRecord staticSpheres[];
};
layout(set = 0, binding = EVO_BINDING_PARTICLE_SPHERES, scalar) readonly buffer ParticleSphereBlock {
    SphereRecord particleSpheres[];
};
layout(set = 0, binding = EVO_BINDING_CAUSTIC_MAPS) uniform sampler2DArray causticMaps;
layout(set = 0, binding = EVO_BINDING_LENSING, scalar) readonly buffer LensingBlock {
    LensingUniforms lensingData;
};
// The G-buffer's volume layer: rgb the radiance in-scattered on the primary segment, a its transmittance
// ((0, 0, 0, 1) without a medium).
layout(set = 0, binding = EVO_BINDING_VOLUME, rgba16f) uniform image2D volumeImage;
layout(set = 0, binding = EVO_BINDING_CAUSTIC_RECEIVERS, scalar) readonly buffer CausticReceiverBlock {
    CausticReceiver receiverTable[]; // Material.receiver indexes it (Task 13)
};

const uint kMissIndexSky = 0u;
const uint kMissIndexShadow = 1u;

// The main ray's payload (location 0). In: the ray cone at the ray's origin (coneSpread < 0: the path has
// scattered off a rough surface and textures use kDiffuseTextureLod, as NEE does). Out, on a hit: the
// surface the closest-hit evaluated, at the hit point; on a miss: t = -1 and emission = the sky's radiance.
// Every closest-hit fills every field through writeSurface() (hit.glsl). The fields from materialFlags on are
// step 5's (Task 1b froze them; a task never adds its own): written by every closest-hit now, read by the
// task that owns each meaning:
//   materialFlags  Material.flags: camera-only emission (Task 6), caustic receiver (Task 13), thin-walled
//   abbe           Material.abbe: dispersion (Task 6)
//   medium         Material.medium: the medium inside the surface, kNoMedium for none (Task 11)
//   primitive      gl_PrimitiveID: the triangle, or the sphere of a sphere instance's AABB BLAS (Task 8;
//                  Task 12's tree light index is primitive - firstSphere)
//   prevPosition   the hit point under the instance's previous transform (Task 9; = position until then)
struct HitPayload {
    vec3 position;
    float t;
    vec3 normal; // shading normal, on the outward side of the surface (a mesh's counter-clockwise side)
    uint light;  // InstanceRecord.light: the RectLight this surface is, or kNoLight
    vec3 geometricNormal; // outward
    uint frontFace;       // the ray hit the outward side
    vec3 baseColor;
    float roughness;
    float metallic;
    float ior;
    float transmission;
    float coat;
    float coatRoughness;
    float coneWidth;
    float coneSpread;
    uint materialClass;
    vec3 emission; // radiance leaving towards the ray's origin
    uint materialFlags;
    float abbe;
    uint medium;
    uint primitive;
    vec3 prevPosition;
};

// Texture level for anything seen after a rough bounce, and for NEE towards a textured light.
const float kDiffuseTextureLod = 3.0;

const float kPi = 3.14159265358979;

float maxComponent(vec3 v) {
    return max(v.x, max(v.y, v.z));
}

vec3 safeNormalize(vec3 v, vec3 fallback) {
    float len2 = dot(v, v);
    return len2 > 1e-30 && isFinite(len2) ? v * inversesqrt(len2) : fallback;
}

#endif
