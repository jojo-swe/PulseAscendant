#include "modern/environment.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <stdexcept>
#include <string>

namespace evo::modern {

namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.f;

Vec3 fromElevation(float elevationDegrees, float azimuthDegrees) {
    const float e = elevationDegrees * kDegrees, a = azimuthDegrees * kDegrees;
    return {std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)};
}

float luminance(Vec3 c) {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

void store(float out[3], Vec3 v) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

[[noreturn]] void refuse(const std::string& what) {
    throw std::invalid_argument("EnvironmentDesc: " + what);
}

void checkNonNegative(const char* name, float value) {
    if (!std::isfinite(value) || value < 0.f)
        refuse(std::string(name) + " must be finite and not negative");
}

void checkColour(const char* name, Vec3 c) {
    for (const float v : {c.x, c.y, c.z})
        checkNonNegative(name, v);
}

} // namespace

EnvironmentDesc environmentDefaults(EnvironmentKind kind) {
    EnvironmentDesc d; // NightSky: the original night sky around the Studio, the moon an emitter of its own
    d.kind = kind;
    switch (kind) {
    case EnvironmentKind::NightSky:
        break;
    case EnvironmentKind::Space: // black, dense stars, no air (no glow, no town)
        d.discDirection = fromElevation(25.f, 140.f);
        d.zenith = d.horizon = d.ground = {0.0004f, 0.0005f, 0.0009f};
        d.starDensity = 6.f;
        d.starBrightness = 1.5f;
        d.townDensity = 0.f;
        d.glow = 0.f;
        break;
    case EnvironmentKind::Dusk: // a low orange sun, a deep blue zenith, the first stars and the town's lights
        d.discDirection = fromElevation(4.f, -60.f);
        d.discAngularRadius = 0.6f * kDegrees;
        d.discRadiance = {3000.f, 1500.f, 660.f};
        d.zenith = {0.03f, 0.05f, 0.12f};
        d.horizon = {0.55f, 0.28f, 0.12f};
        d.ground = {0.02f, 0.018f, 0.016f};
        d.starDensity = 0.5f;
        d.starBrightness = 0.15f;
        d.townDensity = 0.5f;
        d.haze = 0.6f;
        d.select = 0.5f;
        break;
    case EnvironmentKind::Day: // a high white sun over a blue sky; its disc is drawn about twice the real one
        d.discDirection = fromElevation(50.f, -30.f);
        d.discAngularRadius = 0.5f * kDegrees;
        d.discRadiance = {26000.f, 24500.f, 22000.f};
        d.zenith = {0.12f, 0.25f, 0.6f};
        d.horizon = {0.55f, 0.65f, 0.8f};
        d.ground = {0.12f, 0.11f, 0.1f};
        d.starDensity = 0.f;
        d.starBrightness = 0.f;
        d.townDensity = 0.f;
        d.haze = 0.3f;
        d.select = 0.5f;
        break;
    }
    return d;
}

rt::EnvironmentUniforms packEnvironment(const EnvironmentDesc& d) {
    if (static_cast<std::uint32_t>(d.kind) > static_cast<std::uint32_t>(EnvironmentKind::Day))
        refuse("unknown kind " + std::to_string(static_cast<std::uint32_t>(d.kind)));
    checkNonNegative("intensity", d.intensity);
    checkNonNegative("starDensity", d.starDensity);
    checkNonNegative("starBrightness", d.starBrightness);
    checkNonNegative("townDensity", d.townDensity);
    checkNonNegative("haze", d.haze);
    checkNonNegative("glow", d.glow);
    checkColour("discRadiance", d.discRadiance);
    checkColour("zenith", d.zenith);
    checkColour("horizon", d.horizon);
    checkColour("ground", d.ground);
    if (!(d.select >= 0.f && d.select <= 1.f))
        refuse("select must lie in [0, 1]");
    if (!(d.discAngularRadius >= 0.f && d.discAngularRadius < std::numbers::pi_v<float> / 2))
        refuse("discAngularRadius must lie in [0, pi / 2)");
    const float directionLength = length(d.discDirection);
    if (!std::isfinite(directionLength) || !(directionLength > 0.f))
        refuse("discDirection must be finite and not zero");
    const Vec3 direction = d.discDirection / directionLength;

    rt::EnvironmentUniforms u{};
    u.kind = static_cast<std::uint32_t>(d.kind);
    u.intensity = d.intensity;
    u.discCosRadius = static_cast<float>(std::cos(static_cast<double>(d.discAngularRadius)));
    store(u.discDirection, direction);
    store(u.discRadiance, d.discRadiance);
    u.starDensity = d.starDensity;
    store(u.zenith, d.zenith);
    u.starBrightness = d.starBrightness;
    store(u.horizon, d.horizon);
    u.townDensity = d.townDensity;
    store(u.ground, d.ground);
    u.haze = d.haze;
    store(u.glowDirection, direction);
    u.glowStrength = d.glow;

    // The shares of the environment's samples, by power: the disc's radiance times its solid angle against
    // the dome's (pi times the gradient's mean above the horizon; a rough estimate, which only moves noise
    // between the two). Each gets at least a tenth when both have light, so neither is left to BSDF rays.
    const double discSolidAngle =
        2 * std::numbers::pi * (1 - std::cos(static_cast<double>(d.discAngularRadius)));
    const double discPower = luminance(d.discRadiance) * discSolidAngle;
    const double domePower = std::numbers::pi * luminance((d.zenith + d.horizon) * 0.5f);
    if (discPower > 0 && domePower > 0) {
        u.discShare = static_cast<float>(std::clamp(discPower / (discPower + domePower), 0.1, 0.9));
        u.domeShare = 1.f - u.discShare;
    } else if (discPower > 0) {
        u.discShare = 1.f;
    } else if (domePower > 0) {
        u.domeShare = 1.f;
    }
    return u;
}

} // namespace evo::modern
