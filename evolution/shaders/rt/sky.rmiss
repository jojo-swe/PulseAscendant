#version 460
#extension GL_GOOGLE_include_directive : require

// Miss 0: the main ray left the scene. Marks the miss (t = -1) and returns the environment's radiance along
// the ray: what lights the scene (environmentLighting) once the path has scattered off a rough surface
// (coneSpread < 0), else the full view filtered over the ray cone's spread (environmentView). Never traces.

#include "common.glsl"
#include "environment.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() {
    payload.t = -1.0;
    vec3 d = gl_WorldRayDirectionEXT;
    payload.emission = payload.coneSpread < 0.0 ? environmentLighting(d) : environmentView(d, payload.coneSpread);
}
