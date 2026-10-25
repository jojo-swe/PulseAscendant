#ifndef EVO_MODERN_MEDIUM_COMMON_GLSL
#define EVO_MODERN_MEDIUM_COMMON_GLSL

// The grid medium's bake (step 5, Task 11; modern::MediumVolume records it). A scene's
// shaders/modern/medium_<scene>.comp defines the pure function
//
//     vec4 mediumVoxel(vec3 uvw, float seconds, vec4 params[4]);   // (density, emission rgb)
//
// (uvw in [0, 1]^3 is the voxel's centre in the grid's box) and includes this file first (after #version and
// GL_GOOGLE_include_directive), whose main() runs one invocation per voxel (x over the dispatch's width, y and
// z folded into its height) and stores the voxel into the RGBA16F density image, made safe: a non-finite
// voxel stores 0, and every channel is clamped to [0, 65504] (half precision). Nothing reads what an earlier
// frame wrote, so the grid is a pure function of (uvw, seconds, params): seeking and exports need no history.

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

#include "../common/finite.glsl" // isFinite (from the bits: robust under fast-math)

layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 0, rgba16f) uniform writeonly image3D mediumVoxels;

layout(push_constant, scalar) uniform MediumBakePush {
    uvec3 size; // the voxels baked (from (0, 0, 0)); the image may be larger
    float seconds;
    vec4 params[4];
} mediumBake;

vec4 mediumVoxel(vec3 uvw, float seconds, vec4 params[4]);

void main() {
    uvec3 size = mediumBake.size;
    uint x = gl_GlobalInvocationID.x;
    uint yz = gl_GlobalInvocationID.y;
    if (x >= size.x || yz >= size.y * size.z) {
        return;
    }
    uvec3 voxel = uvec3(x, yz % size.y, yz / size.y);
    vec3 uvw = (vec3(voxel) + 0.5) / vec3(size);
    vec4 params[4] = mediumBake.params;
    vec4 v = mediumVoxel(uvw, mediumBake.seconds, params);
    bool finite = isFinite(v.x) && isFinite(v.y) && isFinite(v.z) && isFinite(v.w);
    imageStore(mediumVoxels, ivec3(voxel), finite ? clamp(v, vec4(0.0), vec4(65504.0)) : vec4(0.0));
}

#endif
