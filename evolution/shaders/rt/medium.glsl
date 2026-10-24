#ifndef EVO_RT_MEDIUM_GLSL
#define EVO_RT_MEDIUM_GLSL

// Participating media (height fog and baked density grids) between surface hits. Hook fixed by Task 1 of
// step 5; Task 11 fills it from MediumUniforms (binding EVO_BINDING_MEDIUM) and the density and majorant grids
// (EVO_BINDING_DENSITY, EVO_BINDING_MAJORANT), and adds the integrator around it in pathtrace.rgen. Until then
// no frame has a medium, and the raygen writes the volume layer as (0, 0, 0, 1). Needs common.glsl.

// Whether this frame has any medium.
bool mediumActive() {
    return false;
}

#endif
