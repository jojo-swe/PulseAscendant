#ifndef EVO_RT_LENSING_GLSL
#define EVO_RT_LENSING_GLSL

// Gravitational lensing: main rays bent piecewise around a black hole. Hook fixed by Task 1 of step 5; Task 25
// fills it from LensingUniforms (binding EVO_BINDING_LENSING). It is the one place the step 5b scenes change
// how the raygen traces: pathtrace.rgen sends every main ray through traceLensed() while lensingActive().
// Until then lensing is off and traceLensed() is the plain trace. Included by pathtrace.rgen after its
// traceMain(origin, direction, coneWidth, coneSpread), which fills the hit payload.

// Whether this frame bends rays.
bool lensingActive() {
    return false;
}

// Traces a main ray from origin along direction, bent by the lens, and leaves the result in the payload as
// traceMain() does (a hit, or a miss with the environment it reaches).
void traceLensed(vec3 origin, vec3 direction, float coneWidth, float coneSpread) {
    traceMain(origin, direction, coneWidth, coneSpread);
}

#endif
