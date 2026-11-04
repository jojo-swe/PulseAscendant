#version 460
#extension GL_EXT_ray_tracing : require

// A deliberately invalid miss shader for rt_pipeline_test: it traces a ray, which recursion depth 1 forbids.

layout(set = 0, binding = 0) uniform accelerationStructureEXT tlas;
layout(location = 0) rayPayloadInEXT uint payload;
layout(location = 1) rayPayloadEXT uint inner;

void main() {
    traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF, 0, 1, 0, vec3(0.0), 0.0, vec3(0.0, 0.0, -1.0), 1.0, 1);
    payload = inner;
}
