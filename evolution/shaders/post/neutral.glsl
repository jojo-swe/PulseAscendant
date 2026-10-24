// Khronos PBR Neutral and (from ../common/color.glsl) the sRGB transfer functions: the tonemap pass applies
// them, the placeholder fill inverts them. Must match src/post/tonemap_reference.cpp.

#ifndef POST_NEUTRAL_GLSL
#define POST_NEUTRAL_GLSL

#include "../common/color.glsl"

// KhronosGroup/ToneMapping, PBR_Neutral/pbrNeutral.glsl: highlights start compressing at 0.8 - F90 (F90 = 0.04,
// the Fresnel reflectance of common dielectrics) and desaturate at Kd = 0.15.
const float kNeutralStart = 0.8 - 0.04;
const float kNeutralDesaturation = 0.15;
const float kNeutralD = 1.0 - kNeutralStart;

// Khronos PBR Neutral for non-negative linear light (pbrNeutral.glsl, unchanged): the same offset is subtracted
// from every component (x - 6.25 x^2 for a minimum x below 0.08, else 0.04); a peak of at least 0.76 is then
// compressed to newPeak and the colour mixed towards newPeak's white. The result is linear, in [0, 1), and has
// the input's hue: it lies in the plane of the input and white.
vec3 pbrNeutral(vec3 color) {
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < kNeutralStart) {
        return color;
    }

    float newPeak = 1.0 - kNeutralD * kNeutralD / (peak + kNeutralD - kNeutralStart);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (kNeutralDesaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3(newPeak), g);
}

// The largest light the inverse gives (a peak, before the offset): white's 1 is the curve's asymptote, so a
// display peak above pbrNeutral(16384) = 0.9999965 is taken as that; 16384 is still exact in RGBA16F and is 255
// in bytes whatever the dither.
const float kNeutralMaxLight = 16384.0;

// The light pbrNeutral maps to `display` (linear, in [0, 1]): Khronos' analytic inverse (lut-writer.mjs,
// inverseNeutral). PBR Neutral is not onto: above the knee its desaturation leaves every component at least
// g(peak) * peak, so a bright colour with a low minimum (normalised by its peak, below g) has no light. That
// colour gets the light of the nearest colour the curve can show with the same hue and the same shape t =
// display / peak: the peak lowered until g(peak) equals min(t). Light >= 0 and finite.
vec3 pbrNeutralInverse(vec3 display) {
    vec3 color = clamp(display, 0.0, 1.0);
    float peak = max(color.r, max(color.g, color.b));
    if (peak > kNeutralStart) {
        const float d2 = kNeutralD * kNeutralD;
        vec3 shape = color / peak; // max 1
        float lowest = min(shape.r, min(shape.g, shape.b));
        peak = min(peak, 1.0 - d2 / (kNeutralMaxLight + kNeutralD - kNeutralStart));
        float oldPeak = d2 / (1.0 - peak) - kNeutralD + kNeutralStart;
        float g = 1.0 - 1.0 / (kNeutralDesaturation * (oldPeak - peak) + 1.0);
        if (g > lowest) {
            // Out of reach: solve g(oldPeak) = lowest. With q = oldPeak + d - start, newPeak = 1 - d^2 / q and
            // oldPeak - newPeak = q + d^2 / q - 2d, so q^2 - (k + 2d) q + d^2 = 0 for k = (1 / (1 - g) - 1) / Kd;
            // the larger root is the one above the knee.
            g = lowest;
            float b = (1.0 / (1.0 - g) - 1.0) / kNeutralDesaturation + 2.0 * kNeutralD;
            float q = 0.5 * (b + sqrt(max(b * b - 4.0 * d2, 0.0)));
            oldPeak = q - kNeutralD + kNeutralStart;
        }
        // Undo the mix towards white, then the scale to the new peak.
        color = max((shape - g) / (1.0 - g), 0.0) * oldPeak;
    }

    // Undo the offset: the forward's minimum after it is y = 6.25 x^2 for x < 0.08, else x - 0.04.
    float y = min(color.r, min(color.g, color.b));
    float x = sqrt(y / 6.25);
    float offset = y < 0.04 ? x - 6.25 * x * x : 0.04;
    return color + offset;
}

#endif
