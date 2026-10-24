#ifndef EVO_RT_ENVIRONMENT_GLSL
#define EVO_RT_ENVIRONMENT_GLSL

// The environment a ray sees when it leaves the scene, and its sampling for next-event estimation. Hook
// signatures fixed by Task 1 of step 5; Task 2 fills them from the scene's EnvironmentUniforms (binding
// EVO_BINDING_ENVIRONMENT). Until then they keep step 4's behaviour exactly: both radiance functions are the
// night sky (sky.glsl) and the environment is never sampled (FrameUniforms.environmentSelect is 0).
// Needs common.glsl.

#include "sky.glsl"

// What a primary ray or a chain of delta events from the camera sees: the full sky, filtered over the ray
// cone's angular footprint (radians).
vec3 environmentView(vec3 d, float footprint) {
    return skyRadiance(d);
}

// What lights the scene: next-event estimation through a portal or towards the environment, and every miss
// after a rough vertex. Smooth (no stars), so it does not speckle.
vec3 environmentLighting(vec3 d) {
    return skyRadiance(d);
}

// Samples a direction wi towards the environment with radiance le (environmentLighting) and solid angle
// density pdf (environmentPdf(wi)); false when the environment has nothing to sample.
bool sampleEnvironment(vec2 u, out vec3 wi, out vec3 le, out float pdf) {
    wi = vec3(0.0, 1.0, 0.0);
    le = vec3(0.0);
    pdf = 0.0;
    return false;
}

// The solid angle density with which sampleEnvironment picks direction d.
float environmentPdf(vec3 d) {
    return 0.0;
}

#endif
