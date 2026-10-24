// Colour helpers shared by the path tracer (shaders/rt), the denoiser and tonemap (shaders/post) and the
// placeholder fill: Rec. 709 luminance and the sRGB transfer functions. The CPU mirrors are in
// src/post/tonemap_reference.cpp (srgbOetf).

#ifndef EVO_COMMON_COLOR_GLSL
#define EVO_COMMON_COLOR_GLSL

// Rec. 709 luminance of linear RGB.
float luminance(vec3 rgb) {
    return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
}

// The sRGB OETF for v in [0, 1]; exactly 1 at 1 (1.055 - 0.055 rounds below 1 in float).
float srgbOetf(float v) {
    if (v >= 1.0) {
        return 1.0;
    }
    return v <= 0.0031308 ? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

vec3 srgbOetf(vec3 v) {
    return vec3(srgbOetf(v.r), srgbOetf(v.g), srgbOetf(v.b));
}

// The inverse OETF (sRGB-encoded -> linear) for v in [0, 1].
vec3 srgbEotf(vec3 v) {
    vec3 low = v / 12.92;
    vec3 high = pow((v + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, lessThanEqual(v, vec3(0.04045)));
}

#endif
