#pragma once

#include "retro/city_plan.hpp"
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
// The layout (kCityCell, cityLot) and the flight (cityPoseAt) live in retro/city_plan.hpp.

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


// The camera at a retro frame (see the flight in retro/city_plan.hpp): cityPoseAt(frame, retroFrame / 35).
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
