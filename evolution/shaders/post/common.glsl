// Shared by the denoiser's passes (accumulate, atrous, combine).

#ifndef POST_COMMON_GLSL
#define POST_COMMON_GLSL

// Below the half-float maximum (65504): anything stored into an RGBA16F image is clamped to it, so a huge
// sample can never round to infinity there.
const float kHalfMax = 60000.0;

// Rec. 709 luminance of linear RGB.
float luminance(vec3 rgb) {
    return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
}

// A comparison rather than isnan/isinf: false for NaN and for either infinity.
bool isFinite(float x) {
    return abs(x) <= 3.0e38;
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
// to the half range.
vec3 demodulate(vec3 lighting, vec3 albedo) {
    return min(finiteOrZero(lighting) / max(safeAlbedo(albedo), vec3(1e-3)), vec3(kHalfMax));
}

#endif
