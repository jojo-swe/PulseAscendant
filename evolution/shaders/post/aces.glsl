// The ACES tonemap curve and (from ../common/color.glsl) the sRGB transfer functions: the tonemap pass applies
// them, the placeholder fill inverts them. Must match src/post/tonemap_reference.cpp.

#ifndef POST_ACES_GLSL
#define POST_ACES_GLSL

#include "../common/color.glsl"

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

// Linear light (already exposed) -> linear display light, clamped to [0, 1]. Negative light is clamped to 0
// in AP1, after the input matrix: the curve's other branch rises again below its root, so -1 grey would come
// out white. Not before the matrix: acesFittedInverse gives negative RGB for saturated colours whose AP1
// components are all positive (at least 0.0033).
vec3 acesFitted(vec3 rgb) {
    return clamp(kAcesOutput * rrtAndOdtFit(max(kAcesInput * rgb, 0.0)), 0.0, 1.0);
}

// The light acesFitted maps to `display` (linear, in [0, 1]): the output matrix undone, the curve solved for
// its positive root, the input matrix undone. A component at 1 gets light well past the curve's 1 crossing,
// so the tonemap's clamp makes it exactly 1 again. Components may come out negative for saturated colours;
// the input matrix brings them back (the inverse output matrix has no negative entry, so v >= 0 and the root
// x >= 0.0033 in AP1, above the tonemap's clamp at 0).
//
// The round trip through the tonemap holds within the dither's 1 step only for the placeholder's palette:
// the light is stored as RGBA16F, and for strongly saturated bright colours the half rounding of the large
// RGB components that cancel in AP1 moves the result by up to 4 bytes.
vec3 acesFittedInverse(vec3 display) {
    // In [0, 1]: the rows of the output matrix sum to 1 within 1e-5, so white may come out a hair above 1.
    vec3 v = clamp(inverse(kAcesOutput) * clamp(display, 0.0, 1.0), 0.0, 1.0);
    // (1 - c v) x^2 + (a - d v) x - (b + e v) = 0, solved for x >= 0 (1 - c v > 0 for v <= 1).
    vec3 qa = 1.0 - kFitC * v;
    vec3 qb = kFitA - kFitD * v;
    vec3 qc = -(kFitB + kFitE * v);
    vec3 x = (-qb + sqrt(qb * qb - 4.0 * qa * qc)) / (2.0 * qa);
    x = mix(x, vec3(64.0), greaterThanEqual(v, vec3(1.0 - 1e-5)));
    return inverse(kAcesInput) * x;
}

#endif
