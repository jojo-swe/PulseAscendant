#pragma once

// Procedural meshes for the 2026 scenes (every Studio asset is built in code). All are centred on the origin
// with +y up, wind counter-clockwise seen from outside and have unit normals and uvs in [0, 1] (v grows
// downwards, so an image maps upright). Closed meshes are watertight: the vertices along a seam or a
// normal discontinuity are duplicated at exactly the same positions. Every builder throws
// std::invalid_argument for a non-positive size or too few segments.

#include "modern/math.hpp"
#include "rt/geometry.hpp"

#include <span>

namespace evo::modern {

// Axis-aligned box from -halfExtents to +halfExtents with flat normals: four vertices per face.
rt::Mesh box(Vec3 halfExtents);
// The same box with every edge and corner rounded to radius `bevel` (0 < bevel < the smallest half extent).
// The faces stay flat; the rounding has smooth normals and `segments` facets on each side of every edge's
// 45 degree line, so 2 * segments facets per quarter round.
rt::Mesh bevelBox(Vec3 halfExtents, float bevel, int segments);
// Along y from -height / 2 to +height / 2, smooth around (segments >= 3), with flat caps when asked.
rt::Mesh cylinder(float radius, float height, int segments, bool caps);
// Revolves a (radius, y) profile, bottom to top, about the y axis (segments >= 3) with smooth normals. A
// profile point with radius 0 is a pole, so a profile that starts and ends on the axis gives a closed
// surface. Needs at least two points, radii >= 0 and no two consecutive points equal.
rt::Mesh lathe(std::span<const Vec2> profile, int segments);
// Smooth sphere: rings >= 2 bands from pole to pole, segments >= 3 around.
rt::Mesh uvSphere(float radius, int rings, int segments);
// Facing +z, centred; uv (0, 0) at the top left corner.
rt::Mesh quad(float width, float height);

} // namespace evo::modern
