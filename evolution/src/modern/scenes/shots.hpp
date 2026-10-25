#pragma once

// Shared by the Cosmos and Singularity scenes (step 5, Tasks 24 and 25): the chapter's downbeats from the beat
// grid, and eased Catmull-Rom camera keys, orbits about a point and the four lenses of the show bible as
// vertical fields of view at 16:9.

#include "modern/math.hpp"
#include "modern/scene.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace evo::modern::shots {

inline constexpr float kDegrees = 3.14159265f / 180.f;

// The chapter's downbeats, in chapter seconds.
inline std::vector<float> downbeats(const show::TimelineFrame& f) {
    const show::BeatGridView grid = show::beatGrid(f.track);
    const double trackToLocal = (f.showSeconds - f.trackSeconds) - (f.showSeconds - f.localSeconds);
    std::vector<float> out;
    for (int i = 0; i < grid.count(); ++i) {
        if (grid.bar(i) != 0)
            continue;
        const double local = grid.beatTime(i) + trackToLocal;
        if (local >= -0.05 && local <= f.chapterDuration + 0.05)
            out.push_back(static_cast<float>(local));
    }
    return out;
}

struct Bars {
    float d = 0, bar = 1.74f; // the downbeat at or before the accent, and the bar's length
    std::vector<float> downbeats;
    // The downbeat k bars from D (negative: before), extrapolated past the grid.
    float at(int k) const {
        const auto found = std::find(downbeats.begin(), downbeats.end(), d);
        const long i = (found - downbeats.begin()) + k;
        if (found != downbeats.end() && i >= 0 && i < static_cast<long>(downbeats.size()))
            return downbeats[static_cast<std::size_t>(i)];
        return d + static_cast<float>(k) * bar;
    }
};

inline Bars barsOf(const show::TimelineFrame& f) {
    Bars b;
    b.downbeats = downbeats(f);
    const auto accent = static_cast<float>(f.accentSeconds);
    b.d = accent;
    for (const float t : b.downbeats)
        if (t <= accent + 0.02f)
            b.d = t;
    if (b.downbeats.size() >= 2)
        b.bar = (b.downbeats.back() - b.downbeats.front()) / static_cast<float>(b.downbeats.size() - 1);
    return b;
}

struct Key {
    Vec3 eye, target, up;
};

inline Vec3 catmullRom(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.f * p1) + (p2 - p0) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 +
                   (3.f * p1 - p0 - 3.f * p2 + p3) * t3);
}

inline Key alongKeys(std::span<const Key> keys, float s) {
    const int n = static_cast<int>(keys.size());
    const float x = std::clamp(s, 0.f, 1.f) * static_cast<float>(n - 1);
    const int i = std::min(static_cast<int>(x), n - 2);
    const float t = x - static_cast<float>(i);
    const auto at = [&](int k) { return keys[static_cast<std::size_t>(std::clamp(k, 0, n - 1))]; };
    return {catmullRom(at(i - 1).eye, at(i).eye, at(i + 1).eye, at(i + 2).eye, t),
            catmullRom(at(i - 1).target, at(i).target, at(i + 1).target, at(i + 2).target, t),
            normalize(catmullRom(at(i - 1).up, at(i).up, at(i + 1).up, at(i + 2).up, t))};
}

// A camera on the sphere of `distance` about `centre` at azimuth and elevation (degrees; azimuth 0 on +z,
// towards +x), looking at the centre, its up the direction of rising elevation.
inline Key orbit(Vec3 centre, float azimuth, float elevation, float distance) {
    const float a = azimuth * kDegrees, e = elevation * kDegrees;
    const Vec3 out{std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)};
    const Vec3 up{-std::sin(e) * std::sin(a), std::cos(e), -std::sin(e) * std::cos(a)};
    return {centre + out * distance, centre, up};
}

// Focal lengths (36 mm equivalent) as vertical fields of view at 16:9.
constexpr float kLens85 = 13.7f, kLens50 = 23.4f, kLens35 = 31.8f;

inline Camera makeCamera(const Key& key, float fov, std::uint32_t shot, float aperture = 0.f, float focus = 0.f) {
    Camera c;
    c.eye = key.eye;
    c.target = key.target;
    c.up = key.up;
    c.fovYDegrees = fov;
    c.aperture = aperture;
    c.shot = shot;
    c.focusDistance = focus > 0.f ? focus : std::max(length(key.target - key.eye), 0.1f);
    return c;
}

} // namespace evo::modern::shots
