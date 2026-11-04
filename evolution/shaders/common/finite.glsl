// Finite checks from the bits, shared by the path tracer (shaders/rt) and the denoiser (shaders/post).
// isnan/isinf may be folded away by a fast-math compiler; these read the exponent and mantissa bits instead,
// so they hold on every driver. They are the guards that keep a NaN or infinite sample out of the running mean.

#ifndef EVO_COMMON_FINITE_GLSL
#define EVO_COMMON_FINITE_GLSL

// False for NaN and for either infinity, whose exponent bits are all set.
bool isFinite(float x) {
    return (floatBitsToUint(x) & 0x7F800000u) != 0x7F800000u;
}

// True for +infinity and -infinity only (all exponent bits set, mantissa 0); false for NaN.
bool isInfinite(float x) {
    return (floatBitsToUint(x) & 0x7FFFFFFFu) == 0x7F800000u;
}

bool allFinite(vec3 v) {
    return isFinite(v.x) && isFinite(v.y) && isFinite(v.z);
}

#endif
