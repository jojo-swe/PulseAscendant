#version 460
#extension GL_GOOGLE_include_directive : require

// Miss 0: the main ray left the scene. Marks the miss (t = -1) and returns the night sky's radiance along
// the ray. Never traces.

#include "common.glsl"
#include "sky.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() {
    payload.t = -1.0;
    payload.emission = skyRadiance(gl_WorldRayDirectionEXT);
}
