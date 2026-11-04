#pragma once

// VECTOR CITY's plan (retro/city.hpp documents the chapter): the grid of streets and lots, the buildings on a
// lot, and the camera's flight as a continuous function of time. Header-only so that the 2026 side's City
// scene (src/modern/scenes/city.cpp, which does not link the retro effects) builds the very same streets and
// flies the very same flight: the 1993 side evaluates the flight at whole retro frames, the 2026 side at any
// time, so a wipe between the eras shows the same city and the same motion.

#include "retro/math.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace evo::retro {

inline constexpr std::uint32_t kCityRetroRate = 35;
inline constexpr std::uint32_t kCitySeed = 0x19930714u;
inline constexpr double kCityCell = 42;
inline constexpr double kCityStreet = 12;
inline constexpr double kCitySidewalk = 1.5; // between a lot's edge and its buildings
inline constexpr double kCityAlley = 1.5;    // between the buildings of one lot
inline constexpr double kCityMaxHeight = 54; // no roof, set-back tower included, is higher
inline constexpr double kCityDrawDistance = 190;
inline constexpr double kCityHazeNear = 70; // the haze starts here and is total at kCityDrawDistance

// The flight, on t = retroFrame / 35 seconds and A = accentSeconds clamped to [kCityMinAccent,
// kCityMaxAccent]. The chapter wipes to 2026 over the two bars before its accent, so the flight is anchored
// to A: down the main street at kCityFlightHeight, climbing (smoothstep) to kCityCruiseHeight between
// kCityClimb[0] A and kCityClimb[1] A while the view tilts down to kCityCruisePitch, and turning right from
// kCityTurn[0] A, its yaw rate easing (smoothstep) up to kCityTurnRate by kCityTurn[1] A; the bank (roll) is
// kCityBank times the yaw rate over kCityTurnRate (plus a 1.5 degree sway while in the street). After A the
// turn eases out over kCityTurnEase seconds and the camera flies on level. The speed is kCitySpeed
// throughout. The heading is closed form; the position is integrated from the heading by Simpson's rule
// (steps of at most 1/200 s) from the turn's start on every call, so it too is a pure function of t.
inline constexpr double kCityMinAccent = 5, kCityMaxAccent = 40;
inline constexpr double kCitySpeed = 24;   // units a second
inline constexpr double kCityStartZ = -30; // where the camera is at retro frame 0, on the main street
inline constexpr double kCityFlightHeight = 3.5;
inline constexpr double kCityCruiseHeight = 70;
inline constexpr double kCityFlightPitch = -2;  // degrees
inline constexpr double kCityCruisePitch = -24; // degrees
inline constexpr std::array<double, 2> kCityClimb = {0.42, 0.85};
inline constexpr std::array<double, 2> kCityTurn = {0.62, 0.85};
inline constexpr double kCityTurnRate = 13; // degrees a second
inline constexpr double kCityBank = 32;     // degrees
inline constexpr double kCityTurnEase = 3;  // seconds
inline constexpr double kCityFovY = 64;     // degrees

// The city's hash: 32 well-mixed bits of (i, m, salt) and kCitySeed, in plain unsigned arithmetic.
inline std::uint32_t cityHash(std::int32_t i, std::int32_t m, std::uint32_t salt) {
    std::uint32_t h = static_cast<std::uint32_t>(i) * 0x9e3779b1u;
    h ^= static_cast<std::uint32_t>(m) * 0x85ebca77u + 0x632be5abu;
    h ^= salt * 0xc2b2ae3du + kCitySeed;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

// One box of a building: x0 < x1, y0 < y1, z0 < z1; ramp 0 (A) or 1 (B).
struct CityBox {
    double x0, x1, y0, y1, z0, z1;
    int ramp;
};
// A building of lot (i, m): its box, standing on the ground, and maybe a set-back tower on the box's roof.
// It covers columns sx0..sx1 and rows sz0..sz1 of the lot's 2 x 2 grid (0 the lower x or z half).
struct CityBuilding {
    CityBox base;
    bool tower;
    CityBox top;
    int sx0, sx1, sz0, sz1;
};
// The buildings of lot (i, m).
inline std::vector<CityBuilding> cityLot(std::int32_t i, std::int32_t m) {
    const double h = kCityStreet / 2 + kCitySidewalk, gap = kCityAlley / 2;
    const double x0 = kCityCell * i + h, x1 = kCityCell * (i + 1) - h, xm = kCityCell * i + kCityCell / 2;
    const double z0 = kCityCell * m + h, z1 = kCityCell * (m + 1) - h, zm = kCityCell * m + kCityCell / 2;
    const std::uint32_t split =
        cityHash(i, m, 0) % 8; // 0, 1 whole; 2, 3 two along x; 4, 5 two along z; 6, 7 four
    const bool splitX = split == 2 || split == 3 || split >= 6, splitZ = split >= 4;
    struct Span {
        double from, to;
        int sub0, sub1;
    };
    const std::vector<Span> xs = splitX ? std::vector<Span>{{x0, xm - gap, 0, 0}, {xm + gap, x1, 1, 1}}
                                        : std::vector<Span>{{x0, x1, 0, 1}};
    const std::vector<Span> zs = splitZ ? std::vector<Span>{{z0, zm - gap, 0, 0}, {zm + gap, z1, 1, 1}}
                                        : std::vector<Span>{{z0, z1, 0, 1}};
    const bool downtown = i == 0 || i == -1; // the lots along the main street
    std::vector<CityBuilding> out;
    std::uint32_t k = 0;
    for (const Span& sz : zs)
        for (const Span& sx : xs) {
            const double u = unit(cityHash(i, m, 16 + k));
            double height = downtown ? 12 + 30 * std::pow(u, 1.3) : 6 + 22 * u * u;
            if (unit(cityHash(i, m, 32 + k)) > 0.92)
                height += 10;
            CityBuilding bd{};
            bd.base = {
                sx.from, sx.to, 0, height, sz.from, sz.to, static_cast<int>(cityHash(i, m, 48 + k) & 1)};
            const double side = std::min(sx.to - sx.from, sz.to - sz.from);
            bd.tower = height > 20 && side > 9 && unit(cityHash(i, m, 64 + k)) < 0.35;
            if (bd.tower) {
                const double inset = 0.22 * side;
                const double top = std::min(kCityMaxHeight, height + 5 + 9 * unit(cityHash(i, m, 80 + k)));
                bd.top = {sx.from + inset, sx.to - inset, height,          top,
                          sz.from + inset, sz.to - inset, 1 - bd.base.ramp};
            }
            bd.sx0 = sx.sub0, bd.sx1 = sx.sub1, bd.sz0 = sz.sub0, bd.sz1 = sz.sub1;
            out.push_back(bd);
            ++k;
        }
    return out;
}

// The camera's pose on the flight.
struct CityPose {
    double x, y, z;          // the eye
    double yaw, pitch, roll; // radians: yaw 0 down +z, positive to the right (+x); roll positive to the right
};

namespace city_flight {

// The integral of smoothstep from 0 to u (u clamped to [0, 1]).
inline double smoothstepIntegral(double u) {
    u = std::clamp(u, 0.0, 1.0);
    return u * u * u - u * u * u * u / 2;
}

inline double accentOf(const show::TimelineFrame& frame) {
    return std::clamp(frame.accentSeconds, kCityMinAccent, kCityMaxAccent);
}

// The heading (radians, 0 down +z, positive to the right) at t seconds: the yaw rate eases up from
// kCityTurn[0] A to kCityTurn[1] A, holds to A and eases out over kCityTurnEase; integrated in closed form.
inline double yawAt(double t, double accent) {
    const double rate = radians(kCityTurnRate);
    const double t0 = kCityTurn[0] * accent, t1 = kCityTurn[1] * accent, ramp = t1 - t0;
    const double until = std::min(t, accent);
    double yaw = rate * ramp * smoothstepIntegral((until - t0) / ramp) + rate * std::max(0.0, until - t1);
    if (t > accent) {
        const double e = std::min(1.0, (t - accent) / kCityTurnEase);
        yaw += rate * kCityTurnEase * (e - smoothstepIntegral(e));
    }
    return yaw;
}

inline double yawRateAt(double t, double accent) {
    const double t0 = kCityTurn[0] * accent, t1 = kCityTurn[1] * accent;
    if (t <= accent)
        return radians(kCityTurnRate) * smoothstep((t - t0) / (t1 - t0));
    return radians(kCityTurnRate) * (1 - smoothstep((t - accent) / kCityTurnEase));
}

// Where the camera is on the ground plane at t: straight down the main street until the turn starts, then
// the heading integrated by Simpson's rule in steps of at most 1/200 s (a pure function of t).
inline std::array<double, 2> groundAt(double t, double accent) {
    const double t0 = kCityTurn[0] * accent;
    if (t <= t0)
        return {0, kCityStartZ + kCitySpeed * t};
    const int n = 2 * std::max(1, static_cast<int>(std::ceil((t - t0) * 100)));
    const double h = (t - t0) / n;
    double sx = 0, sz = 0;
    for (int k = 0; k <= n; ++k) {
        const double weight = (k == 0 || k == n) ? 1 : (k % 2 == 1 ? 4 : 2);
        const double yaw = yawAt(t0 + k * h, accent);
        sx += weight * std::sin(yaw);
        sz += weight * std::cos(yaw);
    }
    return {kCitySpeed * h / 3 * sx, kCityStartZ + kCitySpeed * t0 + kCitySpeed * h / 3 * sz};
}

} // namespace city_flight

// The pose at t seconds into the chapter (the flight above; cityPose(frame, k) = cityPoseAt(frame, k / 35)).
inline CityPose cityPoseAt(const show::TimelineFrame& frame, double t) {
    const double accent = city_flight::accentOf(frame);
    const double climb =
        smoothstep((t - kCityClimb[0] * accent) / ((kCityClimb[1] - kCityClimb[0]) * accent));
    const std::array<double, 2> ground = city_flight::groundAt(t, accent);
    CityPose pose{};
    pose.x = ground[0];
    pose.z = ground[1];
    pose.y = kCityFlightHeight + (kCityCruiseHeight - kCityFlightHeight) * climb;
    pose.yaw = city_flight::yawAt(t, accent);
    pose.pitch = radians(kCityFlightPitch + (kCityCruisePitch - kCityFlightPitch) * climb);
    // The bank follows the turn; in the street a gentle sway.
    pose.roll = radians(kCityBank) * city_flight::yawRateAt(t, accent) / radians(kCityTurnRate) +
                radians(1.5) * std::sin(1.1 * t) * (1 - climb);
    return pose;
}

} // namespace evo::retro
