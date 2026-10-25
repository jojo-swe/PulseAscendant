#ifndef EVO_RT_SPECTRAL_GLSL
#define EVO_RT_SPECTRAL_GLSL

// The hero-wavelength model of dispersion (step 5; Task 6's interface, used by Task 13's photons): a path or
// photon that meets a dispersive dielectric (Material.abbe > 0) picks one wavelength uniformly in
// [kSpectralMinNm, kSpectralMinNm + kSpectralRangeNm), carries spectralRgb(lambda) as its colour weight from
// then on, and refracts with dispersiveIor(ior, abbe, lambda). spectralRgb is non-negative with a mean of
// (1, 1, 1) over the range, so white light stays white on average and energy is kept. Mirrored on the CPU
// by src/rt/spectral.hpp.

const float kSpectralMinNm = 380.0;
const float kSpectralRangeNm = 400.0;

float spectralLobe(float lambdaNm, float centre, float sigma) {
    float x = (lambdaNm - centre) / sigma;
    return exp(-0.5 * x * x);
}

// The linear RGB weight of one wavelength: Gaussian lobes (red with a violet tail), each scaled to a mean of
// 1 over the range.
vec3 spectralRgb(float lambdaNm) {
    float r = spectralLobe(lambdaNm, 605.0, 40.0) + 0.12 * spectralLobe(lambdaNm, 420.0, 20.0);
    float g = spectralLobe(lambdaNm, 545.0, 38.0);
    float b = spectralLobe(lambdaNm, 452.0, 30.0);
    return vec3(r * 3.7684900, g * 4.1994326, b * 5.3632068);
}

// Cauchy's n = A + B / lambda^2 through the IOR at the d line (587.6 nm) with the given Abbe number
// (n_d - 1) / (n_F - n_C) (486.1 and 656.3 nm). Abbe 0 (or less): no dispersion.
float dispersiveIor(float ior, float abbe, float lambdaNm) {
    if (!(abbe > 0.0)) {
        return ior;
    }
    float b = (ior - 1.0) / (abbe * (1.0 / (486.1 * 486.1) - 1.0 / (656.3 * 656.3)));
    float a = ior - b / (587.6 * 587.6);
    return a + b / (lambdaNm * lambdaNm);
}

#endif
