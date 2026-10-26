// Shared by the denoiser's passes (firefly, accumulate, atrous, combine).

#ifndef POST_COMMON_GLSL
#define POST_COMMON_GLSL

#include "../common/color.glsl"  // luminance
#include "../common/finite.glsl" // isFinite

// Below the half-float maximum (65504): anything stored into an RGBA16F image is clamped to it, so a huge
// sample can never round to infinity there.
const float kHalfMax = 60000.0;

// A primary hit: positive, finite linear depth. A miss is -1; NaN or infinity counts as a miss too.
bool isHit(float depth) {
    return depth > 0.0 && isFinite(depth);
}

// The normal edge stop max(0, dot)^sigma, 0 for a NaN dot product.
float normalWeight(vec3 a, vec3 b, float sigma) {
    float d = dot(a, b);
    return d > 0.0 ? pow(d, sigma) : 0.0;
}

// Each component that is NaN or infinite becomes 0.
vec3 finiteOrZero(vec3 v) {
    return vec3(isFinite(v.x) ? v.x : 0.0, isFinite(v.y) ? v.y : 0.0, isFinite(v.z) ? v.z : 0.0);
}

// The albedo the lighting is divided by and multiplied with again: non-finite or negative components count
// as 0 (their lighting then never reaches the output).
vec3 safeAlbedo(vec3 albedo) {
    return max(finiteOrZero(albedo), vec3(0.0));
}

// Demodulated illumination: lighting / max(albedo, 1e-3), a non-finite sample counting as 0 first, clamped
// to [0, half maximum] (radiance is never negative, and a huge negative value would store as -Inf).
vec3 demodulate(vec3 lighting, vec3 albedo) {
    return clamp(finiteOrZero(lighting) / max(safeAlbedo(albedo), vec3(1e-3)), vec3(0.0), vec3(kHalfMax));
}

// A volume layer sample made safe: non-finite radiance components count as 0 (as a non-finite lighting
// sample does), radiance is clamped to [0, half maximum], and a non-finite transmittance counts as 1 (no
// medium), else clamped to [0, 1].
vec4 safeVolume(vec4 volume) {
    vec3 radiance = clamp(finiteOrZero(volume.rgb), vec3(0.0), vec3(kHalfMax));
    float transmittance = isFinite(volume.a) ? clamp(volume.a, 0.0, 1.0) : 1.0;
    return vec4(radiance, transmittance);
}

#endif
