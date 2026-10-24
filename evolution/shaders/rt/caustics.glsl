#ifndef EVO_RT_CAUSTICS_GLSL
#define EVO_RT_CAUSTICS_GLSL

// Caustics from the photon map. Hook fixed by Task 1 of step 5; Task 13 fills it from the receivers' maps
// (binding EVO_BINDING_CAUSTIC_MAPS) and adds it to the receivers' lighting. Until then no surface receives
// a caustic. Included by hit.glsl (needs HitPoint).

// The radiance the caustic photon map reflects off a hit towards the ray's origin (albedo / pi * E on a
// kMaterialCausticReceiver surface; 0 elsewhere).
vec3 causticRadiance(HitPoint h) {
    return vec3(0.0);
}

#endif
