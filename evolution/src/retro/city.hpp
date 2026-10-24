#pragma once

#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace evo::retro {

// VECTOR CITY (chapter 8): a gouraud-shaded vector city at sunset. The camera flies low down the main street
// towards a setting sun at its vanishing point, buildings sliding past on both sides, then pulls up over the
// roofs and banks into a right turn. A scene effect at 35 retro frames a second on the haze colour; every
// frame is a pure function of (accentSeconds, retroFrame) and the city's fixed seed, nothing accumulated.
//
// The city is an endless grid, generated where the camera looks: world x right, y up, z down the main
// street. Streets kCityStreet wide run along z at x = kCityCell * i and along x at z = kCityCell * m; the
// lot between them, cell (i, m), spans x in [kCityCell i + kCityStreet / 2, kCityCell (i + 1) - kCityStreet /
// 2] (z likewise). The main street is the one at x = 0. Each lot holds 1, 2 or 4 buildings on a 2 x 2 grid,
// each a box (some with a set-back tower on top), their heights, splits and ramps drawn from cityHash, a
// small integer hash of (i, m) and kCitySeed (no std random engine or distribution: those are
// implementation-defined). Only the cells within kCityDrawDistance of the eye are drawn, fading through a
// distance haze into the background, so a frame draws about 300 polygons (at most about 400): walls, roofs,
// street tiles, markings and lit windows, drawn as triangles through addTriangle, which clips and culls them.
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

// The palette (the rest black): a 48-step sky from the hazy horizon through orange, pink and purple to deep
// indigo at the zenith; the sun's 8 steps; two 48-step building ramps, warm (A) and purple (B), from near
// black through the haze colour to pale gold or peach; 16-step road and lot ramps and 16 lit-window shades,
// each from its own colour into the haze; the road markings. Every building, road, lot and window shade
// reaches the haze colour (kCityHaze, the background) at kCityDrawDistance, so the city dissolves into it.
inline constexpr std::uint8_t kCitySky = 1; // 1..48, horizon .. zenith
inline constexpr std::uint8_t kCitySkyShades = 48;
inline constexpr std::uint8_t kCitySun = 49; // 49..56, the sun's bottom .. top
inline constexpr std::uint8_t kCitySunShades = 8;
inline constexpr std::uint8_t kCityHaze = 57;
inline constexpr std::uint8_t kCityRampA = 64;  // 64..111
inline constexpr std::uint8_t kCityRampB = 112; // 112..159
inline constexpr std::uint8_t kCityRampShades = 48;
inline constexpr double kCityRampHaze = 0.7;      // where the haze colour sits on a building ramp (0..1)
inline constexpr std::uint8_t kCityRoad = 160;    // 160..175, near .. haze
inline constexpr std::uint8_t kCityLot = 176;     // 176..191
inline constexpr std::uint8_t kCityMarking = 192; // 192..199
inline constexpr std::uint8_t kCityWindow = 200;  // 200..215
inline constexpr std::uint8_t kCityGroundShades = 16;
inline constexpr std::uint8_t kCityMarkingShades = 8;

// The city's hash: 32 well-mixed bits of (i, m, salt) and kCitySeed, in plain unsigned arithmetic.
std::uint32_t cityHash(std::int32_t i, std::int32_t m, std::uint32_t salt);

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
std::vector<CityBuilding> cityLot(std::int32_t i, std::int32_t m);

// The camera at a retro frame (see the flight above).
struct CityPose {
    double x, y, z;          // the eye
    double yaw, pitch, roll; // radians: yaw 0 down +z, positive to the right (+x); roll positive to the right
};
CityPose cityPose(const show::TimelineFrame& frame, std::uint32_t retroFrame);
Camera cityCamera(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// One triangle of the city in world space as cityScene hands it to addTriangle, in drawing order: the sky
// dome round the eye, the sun, the ground (lots, streets, crossings), the main street's markings, then the
// buildings. Buildings go cell by cell, the cells farthest first by their Manhattan distance in cells from
// the eye's cell (a ray moves away from the eye in x and in z alike, so a cell can only hide one farther in
// that distance); within a lot the same on its 2 x 2 grid; a set-back tower before its base while the eye is
// below the base's roof, after it once above; within a box its walls, each followed by its lit windows,
// then its roof. Faces turned away from the eye are left out, and so are cells the camera cannot see.
struct CityFace {
    double world[3][3];
    std::uint8_t index[3];
    std::uint8_t mode; // kFlat or kGouraud
};
std::vector<CityFace> cityFaces(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The EffectInfo parts of VECTOR CITY (see retro/effects.hpp).
void cityPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::uint8_t cityBackground(const show::TimelineFrame& frame, std::uint32_t retroFrame); // kCityHaze
void cityScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
