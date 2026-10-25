#ifndef EVO_RT_MEDIUM_GLSL
#define EVO_RT_MEDIUM_GLSL

// Participating media (height fog and baked density grids) between surface hits. Hook fixed by Task 1 of
// step 5; Task 11 fills it from MediumUniforms (`mediumData`, binding EVO_BINDING_MEDIUM) and the density and
// majorant grids (EVO_BINDING_DENSITY, EVO_BINDING_MAJORANT: texelFetch the majorant), and adds the integrator
// around it in pathtrace.rgen. Until then no frame has a medium, and the raygen writes the volume layer as
// (0, 0, 0, 1). Needs common.glsl.

// Whether this frame has any medium.
bool mediumActive() {
    return false;
}

// The transmittance of the media along a shadow ray from origin along direction (unit) up to tMax (Task 11:
// ratio tracking, a pure function of the ray and the frame). Every next-event estimate in pathtrace.rgen
// multiplies its contribution by it once its shadow ray is unoccluded. 1 without a medium (Task 1b's stub).
float mediumTransmittance(vec3 origin, vec3 direction, float tMax) {
    return 1.0;
}

#endif
