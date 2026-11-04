#include "modern/light_select.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace evo::modern {

namespace {

Vec3 load(const float v[3]) {
    return {v[0], v[1], v[2]};
}

float luminance(Vec3 c) {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

Vec3 normalizeOrZero(Vec3 v) {
    const float len2 = dot(v, v);
    return len2 > 1e-30f && std::isfinite(len2) ? v / std::sqrt(len2) : Vec3{};
}

Vec3 normalOf(const rt::RectLight& l) {
    return normalizeOrZero(cross(load(l.edgeU), load(l.edgeV)));
}

Vec3 centreOf(const rt::RectLight& l) {
    return load(l.corner) + (load(l.edgeU) + load(l.edgeV)) * 0.5f;
}

} // namespace

float lightImportanceReference(const rt::RectLight& l, Vec3 p, Vec3 n, bool transmissive,
                               float screenLuminance, float portalLuminance) {
    const float weight = std::max(l.selectWeight, 0.f);
    if (!(weight > 0.f) || !(l.area > 0.f))
        return 0.f;
    const Vec3 centre = centreOf(l), toCentre = centre - p;
    if (!(dot(toCentre, normalOf(l)) < 0.f))
        return 0.f;
    float cosBound = 1.f;
    if (!transmissive) {
        const Vec3 corner = load(l.corner), u = load(l.edgeU), v = load(l.edgeV);
        const std::array<Vec3, 5> points = {centre, corner, corner + u, corner + v, corner + u + v};
        cosBound = 0.f;
        for (const Vec3& q : points)
            cosBound = std::max(cosBound, dot(n, normalizeOrZero(q - p)));
        if (!(cosBound > 0.f))
            return 0.f;
    }
    float radiance = luminance(load(l.emission));
    if (l.kind == static_cast<std::uint32_t>(rt::LightKind::Portal))
        radiance = portalLuminance;
    else if (l.kind == static_cast<std::uint32_t>(rt::LightKind::RetroScreen))
        radiance *= screenLuminance;
    const float w = weight * radiance * l.area * cosBound / std::max(dot(toCentre, toCentre), 0.25f * l.area);
    return w > 0.f && std::isfinite(w) ? w : 0.f;
}

bool segmentCrossesPortal(const rt::RectLight& portal, Vec3 p, Vec3 q) {
    const Vec3 n = normalOf(portal), d = q - p;
    const float denom = dot(d, n);
    if (!(denom < 0.f))
        return false;
    const float t = dot(load(portal.corner) - p, n) / denom; // along d: 1 at q
    if (!(t > 0.f && t < 1.f))
        return false;
    const Vec3 hit = p + d * t - load(portal.corner);
    const Vec3 u = load(portal.edgeU), v = load(portal.edgeV);
    const float a = dot(hit, u) / dot(u, u), b = dot(hit, v) / dot(v, v);
    return a >= 0.f && a <= 1.f && b >= 0.f && b <= 1.f;
}

std::vector<float> lightSelectPdfsReference(std::span<const rt::RectLight> lights, Vec3 p, Vec3 n,
                                            bool transmissive, float screenLuminance,
                                            std::span<const float> portalLuminance) {
    if (portalLuminance.size() != lights.size())
        throw std::invalid_argument("lightSelectPdfsReference: one portal luminance per light");
    std::vector<float> w(lights.size(), 0.f);
    float total = 0.f;
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const rt::RectLight& l = lights[i];
        w[i] = lightImportanceReference(l, p, n, transmissive, screenLuminance, portalLuminance[i]);
        if (l.gate != rt::kNoLight &&
            (l.gate >= lights.size() || !segmentCrossesPortal(lights[l.gate], p, centreOf(l))))
            w[i] = 0.f;
        total += w[i];
    }
    for (float& x : w)
        x = total > 0.f && std::isfinite(total) ? x / total : 0.f;
    return w;
}

} // namespace evo::modern
