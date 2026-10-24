// The ACES tonemap curve and the sRGB transfer functions: the tonemap pass applies them, the placeholder fill
// inverts them. Must match src/post/tonemap_reference.cpp.

#ifndef POST_ACES_GLSL
#define POST_ACES_GLSL

// Stephen Hill's ACES fit (BakingLab, ACES.hlsl), column-major (GLSL): the same numbers as the row-major
// tables of the reference.
// sRGB -> AP1, with the RRT's saturation adjustment folded in.
const mat3 kAcesInput = mat3(0.59719, 0.07600, 0.02840,  // column 0
                             0.35458, 0.90834, 0.13383,  // column 1
                             0.04823, 0.01566, 0.83777); // column 2
// AP1 -> sRGB, with the ODT's saturation adjustment folded in.
const mat3 kAcesOutput = mat3(1.60475, -0.10208, -0.00327,  // column 0
                              -0.53108, 1.10813, -0.07276,  // column 1
                              -0.07367, -0.00605, 1.07602); // column 2

// The RRT + ODT curve, per component: (v (v + a) - b) / (v (c v + d) + e).
const float kFitA = 0.0245786;
const float kFitB = 0.000090537;
const float kFitC = 0.983729;
const float kFitD = 0.4329510;
const float kFitE = 0.238081;

vec3 rrtAndOdtFit(vec3 v) {
    vec3 a = v * (v + kFitA) - kFitB;
    vec3 b = v * (kFitC * v + kFitD) + kFitE;
    return a / b;
}

// Linear light (already exposed) -> linear display light, clamped to [0, 1].
vec3 acesFitted(vec3 rgb) {
    return clamp(kAcesOutput * rrtAndOdtFit(kAcesInput * rgb), 0.0, 1.0);
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

// The light acesFitted maps to `display` (linear, in [0, 1]): the output matrix undone, the curve solved for
// its positive root, the input matrix undone. A component at 1 gets light well past the curve's 1 crossing,
// so the tonemap's clamp makes it exactly 1 again. Components may come out negative for saturated colours;
// the input matrix brings them back.
vec3 acesFittedInverse(vec3 display) {
    vec3 v = inverse(kAcesOutput) * clamp(display, 0.0, 1.0);
    // (1 - c v) x^2 + (a - d v) x - (b + e v) = 0, solved for x >= 0 (1 - c v > 0 for v <= 1).
    vec3 qa = 1.0 - kFitC * v;
    vec3 qb = kFitA - kFitD * v;
    vec3 qc = -(kFitB + kFitE * v);
    vec3 x = (-qb + sqrt(qb * qb - 4.0 * qa * qc)) / (2.0 * qa);
    x = mix(x, vec3(64.0), greaterThanEqual(v, vec3(1.0 - 1e-5)));
    return inverse(kAcesInput) * x;
}

#endif
