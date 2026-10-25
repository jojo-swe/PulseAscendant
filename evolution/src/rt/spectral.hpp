#pragma once

// The CPU mirror of shaders/rt/spectral.glsl (keep the two equal; integrator_test (e) compares them): the
// path tracer's hero wavelength (the integrator). A path that meets dispersive glass (Material::abbe > 0)
// carries one wavelength, uniform over [kSpectralMinNm, kSpectralMinNm + kSpectralRangeNm], and its
// throughput is multiplied by spectralRgb of it.
//
// spectralRgb: the CIE 1931 2-degree colour matching functions (the multi-lobe Gaussian fit of Wyman, Sloan
// and Shirley, "Simple Analytic Approximations to the CIE XYZ Color Matching Functions", JCGT 2013) taken to
// linear sRGB (D65), each channel's negative lobe clipped to 0, and each channel divided by its mean over the
// range, so that the mean over a uniform wavelength is (1, 1, 1) (to 1e-6): a white path stays white on
// average and the estimator stays unbiased, per channel.
//
// dispersiveIor: Cauchy's n(lambda) = A + B / lambda^2 through n = ior at the d line (587.6 nm) with Abbe
// number V = (n_d - 1) / (n_F - n_C) (F 486.1 nm, C 656.3 nm); abbe <= 0 means no dispersion (n = ior).

#include <algorithm>
#include <array>
#include <cmath>

namespace evo::rt {

inline constexpr float kSpectralMinNm = 380.f, kSpectralRangeNm = 400.f;
inline constexpr float kSpectralLineD = 587.6f, kSpectralLineF = 486.1f, kSpectralLineC = 656.3f;
// The mean of each clipped channel over [380, 780] nm (a 400,000-point midpoint rule).
inline constexpr std::array<float, 3> kSpectralChannelMean{0.4404433055f, 0.2884639246f, 0.2732961385f};

// One lobe of the fit: a Gaussian with a different width below and above its centre.
inline float spectralLobe(float lambda, float centre, float below, float above) {
    const float t = (lambda - centre) / (lambda < centre ? below : above);
    return std::exp(-0.5f * t * t);
}

inline std::array<float, 3> spectralRgb(float lambdaNm) {
    const float l = lambdaNm;
    const float x = 1.056f * spectralLobe(l, 599.8f, 37.9f, 31.0f) +
                    0.362f * spectralLobe(l, 442.0f, 16.0f, 26.7f) -
                    0.065f * spectralLobe(l, 501.1f, 20.4f, 26.2f);
    const float y =
        0.821f * spectralLobe(l, 568.8f, 46.9f, 40.5f) + 0.286f * spectralLobe(l, 530.9f, 16.3f, 31.1f);
    const float z =
        1.217f * spectralLobe(l, 437.0f, 11.8f, 36.0f) + 0.681f * spectralLobe(l, 459.0f, 26.0f, 13.8f);
    const float r = 3.2404542f * x - 1.5371385f * y - 0.4985314f * z;
    const float g = -0.9692660f * x + 1.8760108f * y + 0.0415560f * z;
    const float b = 0.0556434f * x - 0.2040259f * y + 1.0572252f * z;
    return {std::max(r, 0.f) / kSpectralChannelMean[0], std::max(g, 0.f) / kSpectralChannelMean[1],
            std::max(b, 0.f) / kSpectralChannelMean[2]};
}

inline float dispersiveIor(float ior, float abbe, float lambdaNm) {
    if (!(abbe > 0.f))
        return ior;
    // In micrometres, so that B stays near 1e-2.
    const float d = kSpectralLineD * 1e-3f, f = kSpectralLineF * 1e-3f, c = kSpectralLineC * 1e-3f;
    const float b = (ior - 1.f) / (abbe * (1.f / (f * f) - 1.f / (c * c)));
    const float l = lambdaNm * 1e-3f;
    return ior + b * (1.f / (l * l) - 1.f / (d * d));
}

} // namespace evo::rt
