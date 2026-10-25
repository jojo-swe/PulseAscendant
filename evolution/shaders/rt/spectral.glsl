#ifndef EVO_RT_SPECTRAL_GLSL
#define EVO_RT_SPECTRAL_GLSL

// The path tracer's hero wavelength (the integrator); the CPU mirror is src/rt/spectral.hpp, which says
// what the functions are (keep the two equal; integrator_test (e) compares them).
//   spectralRgb(lambda)                 linear sRGB weight of one wavelength: >= 0, mean (1, 1, 1) over
//                                       [kSpectralMinNm, kSpectralMinNm + kSpectralRangeNm]
//   dispersiveIor(ior, abbe, lambda)    Cauchy through ior at 587.6 nm with Abbe number abbe (<= 0: ior)

const float kSpectralMinNm = 380.0;
const float kSpectralRangeNm = 400.0;
const vec3 kSpectralChannelMean = vec3(0.4404433055, 0.2884639246, 0.2732961385);

float spectralLobe(float lambda, float centre, float below, float above) {
    float t = (lambda - centre) / (lambda < centre ? below : above);
    return exp(-0.5 * t * t);
}

vec3 spectralRgb(float lambdaNm) {
    float l = lambdaNm;
    float x = 1.056 * spectralLobe(l, 599.8, 37.9, 31.0) + 0.362 * spectralLobe(l, 442.0, 16.0, 26.7) -
              0.065 * spectralLobe(l, 501.1, 20.4, 26.2);
    float y = 0.821 * spectralLobe(l, 568.8, 46.9, 40.5) + 0.286 * spectralLobe(l, 530.9, 16.3, 31.1);
    float z = 1.217 * spectralLobe(l, 437.0, 11.8, 36.0) + 0.681 * spectralLobe(l, 459.0, 26.0, 13.8);
    vec3 rgb = vec3(3.2404542 * x - 1.5371385 * y - 0.4985314 * z, -0.9692660 * x + 1.8760108 * y + 0.0415560 * z,
                    0.0556434 * x - 0.2040259 * y + 1.0572252 * z);
    return max(rgb, vec3(0.0)) / kSpectralChannelMean;
}

float dispersiveIor(float ior, float abbe, float lambdaNm) {
    if (!(abbe > 0.0)) {
        return ior;
    }
    // In micrometres, so that B stays near 1e-2.
    const float d = 0.5876;
    const float f = 0.4861;
    const float c = 0.6563;
    float b = (ior - 1.0) / (abbe * (1.0 / (f * f) - 1.0 / (c * c)));
    float l = lambdaNm * 1e-3;
    return ior + b * (1.0 / (l * l) - 1.0 / (d * d));
}

#endif
