#pragma once

// FLYBY's flight (retro/flyby.hpp documents the chapter): the ship's attitude and the camera's path around it,
// as continuous functions of time. Header-only so that the 2026 side's Ship scene (src/modern/scenes/ship.cpp,
// which does not link the retro effects) flies the very same flight: the 1993 side evaluates them at whole
// retro frames, the 2026 side at any time.

#include "retro/math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace evo::retro {

// FLYBY's retro frame rate and the vertical field of view of its camera, degrees.
inline constexpr std::uint32_t kFlybyRetroRate = 35;
inline constexpr double kFlybyFovY = 42;

// The ship's attitude at t seconds into the chapter, a rotation from the ship's model space (x to starboard, y
// up, z along the nose) into the world, row by row: a gentle bank (roll about z), with a slower nod (pitch
// about x) and sway (yaw about y) so it never sits still. yaw * pitch * roll.
struct FlybyAttitude {
    double m[3][3];
};

inline FlybyAttitude flybyAttitude(double t) {
    const double tau = 2 * std::numbers::pi;
    const double roll = radians(13 * std::sin(tau * t / 6.0) + 4 * std::sin(tau * t / 2.9));
    const double pitch = radians(4 * std::sin(tau * t / 4.7));
    const double yaw = radians(5 * std::sin(tau * t / 7.3));
    const double cr = std::cos(roll), sr = std::sin(roll);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const FlybyAttitude rz{{{cr, -sr, 0}, {sr, cr, 0}, {0, 0, 1}}};
    const FlybyAttitude rx{{{1, 0, 0}, {0, cp, -sp}, {0, sp, cp}}};
    const FlybyAttitude ry{{{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}}};
    const auto multiply = [](const FlybyAttitude& a, const FlybyAttitude& b) {
        FlybyAttitude r{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        return r;
    };
    return multiply(ry, multiply(rx, rz));
}

// The camera at `retroFrames` retro frames into the chapter (fractional on the 2026 side), the ship at the
// world origin: its eye and target (world up is +y, the field of view kFlybyFovY). Over the chapter
// (progress = retroFrames / (chapterDuration * kFlybyRetroRate), eased) it rounds the ship from behind
// (azimuth 0: the camera on the -z side) past its right side to a three-quarter view from ahead, and from
// below to above; closer at the end.
struct FlybyView {
    double eye[3], target[3];
};

inline FlybyView flybyView(double chapterDuration, double retroFrames) {
    const auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };
    const double frames = chapterDuration * kFlybyRetroRate;
    const double progress = frames > 0 ? std::clamp(retroFrames / frames, 0.0, 1.0) : 0.0;
    const double eased = progress * progress * (3 - 2 * progress);
    const double t = retroFrames / kFlybyRetroRate;

    const double azimuth = radians(lerp(6, 140, eased) + 3 * std::sin(2 * std::numbers::pi * t / 9.0));
    const double elevation = radians(lerp(-26, 30, eased));
    const double distance = lerp(7.4, 5.6, eased);
    FlybyView view{};
    view.eye[0] = distance * std::cos(elevation) * std::sin(azimuth);
    view.eye[1] = distance * std::sin(elevation);
    view.eye[2] = -distance * std::cos(elevation) * std::cos(azimuth);
    // Aimed below the ship, so the ship rides in the upper part of the frame: its lowest vertex reaches about
    // y = 132 px (flyby_test allows 136), less offset while the camera looks up at it from below, which
    // already lifts it.
    view.target[0] = 0;
    view.target[1] = lerp(-0.4, -0.95, eased);
    view.target[2] = 0;
    return view;
}

} // namespace evo::retro
