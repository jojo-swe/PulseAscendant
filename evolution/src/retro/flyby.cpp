// FLYBY's 1993 side: an original arrow-head fighter, flat shaded on the CPU, over a streaming starfield.
// See retro/flyby.hpp for the layout of the palette and the shape of the flight.

#include "retro/flyby.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace evo::retro {

namespace {

using Vec3 = std::array<double, 3>;

Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

double dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

Vec3 normalize(const Vec3& v) {
    const double length = std::sqrt(dot(v, v));
    return {v[0] / length, v[1] / length, v[2] / length};
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

double radians(double degrees) {
    return degrees * std::numbers::pi / 180.0;
}

// --- The ship ------------------------------------------------------------------------------------------
//
// Model space: x to starboard (right), y up, z forward (the nose points along +z); one unit is about a
// fifth of the ship's length. The ship is built from convex parts: the fuselage, two wings, two engine pods
// and two engine flames. Faces may be listed in either winding: a face's outward normal is the one pointing
// away from its part's centroid, which is exact for a convex part.

// Which ramp of the palette a face is shaded in.
enum class Ramp : std::uint8_t { Hull, Wing, Cockpit, Engine };

struct Face {
    std::uint8_t a, b, c;
    Ramp ramp;
};

// The fuselage: a hexagonal cross-section (flat-topped ridge, wide shoulders, a narrower keel) lofted from a
// needle nose through three sections, each a scaled copy of
//   top (0, 0.30), upper right (0.34, 0.10), lower right (0.26, -0.16), bottom (0, -0.24),
//   lower left (-0.26, -0.16), upper left (-0.34, 0.10)
// about the z axis, at z = 1.0 (scale 0.58), z = -0.3 (scale 1, the widest, where the canopy peaks) and
// z = -1.9 (scale 0.85, the tail). The scale grows ever more slowly from the nose back (0, 0.58, 1, 0.85 at
// z = 2.5, 1, -0.3, -1.9), so the loft is convex and its side quads are planar trapezoids.
constexpr std::array<Vec3, 19> kFuselageVertices = {{
    {0, 0, 2.5}, // 0: nose
    // 1..6: the forward section, z = 1.0, scale 0.58 (top, upper right, lower right, bottom, lower left,
    // upper left)
    {0, 0.174, 1.0},
    {0.1972, 0.058, 1.0},
    {0.1508, -0.0928, 1.0},
    {0, -0.1392, 1.0},
    {-0.1508, -0.0928, 1.0},
    {-0.1972, 0.058, 1.0},
    // 7..12: the middle section, z = -0.3, scale 1
    {0, 0.30, -0.3},
    {0.34, 0.10, -0.3},
    {0.26, -0.16, -0.3},
    {0, -0.24, -0.3},
    {-0.26, -0.16, -0.3},
    {-0.34, 0.10, -0.3},
    // 13..18: the tail section, z = -1.9, scale 0.85
    {0, 0.255, -1.9},
    {0.289, 0.085, -1.9},
    {0.221, -0.136, -1.9},
    {0, -0.204, -1.9},
    {-0.221, -0.136, -1.9},
    {-0.289, 0.085, -1.9},
}};

constexpr std::array<Face, 34> kFuselageFaces = {{
    // The nose cone: the nose to each edge of the forward section.
    {0, 1, 2, Ramp::Hull},
    {0, 2, 3, Ramp::Hull},
    {0, 3, 4, Ramp::Hull},
    {0, 4, 5, Ramp::Hull},
    {0, 5, 6, Ramp::Hull},
    {0, 6, 1, Ramp::Hull},
    // Forward section to middle section. The two quads either side of the top ridge are the canopy.
    {1, 7, 8, Ramp::Cockpit},
    {1, 8, 2, Ramp::Cockpit},
    {2, 8, 9, Ramp::Hull},
    {2, 9, 3, Ramp::Hull},
    {3, 9, 10, Ramp::Hull},
    {3, 10, 4, Ramp::Hull},
    {4, 10, 11, Ramp::Hull},
    {4, 11, 5, Ramp::Hull},
    {5, 11, 12, Ramp::Hull},
    {5, 12, 6, Ramp::Hull},
    {6, 12, 7, Ramp::Cockpit},
    {6, 7, 1, Ramp::Cockpit},
    // Middle section to tail section.
    {7, 13, 14, Ramp::Hull},
    {7, 14, 8, Ramp::Hull},
    {8, 14, 15, Ramp::Hull},
    {8, 15, 9, Ramp::Hull},
    {9, 15, 16, Ramp::Hull},
    {9, 16, 10, Ramp::Hull},
    {10, 16, 17, Ramp::Hull},
    {10, 17, 11, Ramp::Hull},
    {11, 17, 18, Ramp::Hull},
    {11, 18, 12, Ramp::Hull},
    {12, 18, 13, Ramp::Hull},
    {12, 13, 7, Ramp::Hull},
    // The tail cap, a fan over the tail section.
    {13, 14, 15, Ramp::Hull},
    {13, 15, 16, Ramp::Hull},
    {13, 16, 17, Ramp::Hull},
    {13, 17, 18, Ramp::Hull},
}};

// The fuselage lies within |x| <= 0.34 (its middle section); the wings and pods lie outside, but for the
// wing roots, which sink just under the fuselage's skin. This plane on either side separates them.
constexpr double kFuselageHalfWidth = 0.34;

// The right wing, swept back hard with a little dihedral: a thin wedge, 0.08 thick at the root (inside the
// fuselage, whose skin it meets at y = +-0.04) and sharp at the tip. The left wing is its mirror image.
constexpr std::array<Vec3, 6> kWingVertices = {{
    {0.225, 0.04, 0.60},  // 0: root, leading edge, top
    {0.205, -0.04, 0.60}, // 1: root, leading edge, bottom
    {0.28, 0.04, -1.70},  // 2: root, trailing edge, top
    {0.25, -0.04, -1.70}, // 3: root, trailing edge, bottom
    {1.80, 0.10, -1.25},  // 4: tip, leading edge
    {1.95, 0.10, -1.85},  // 5: tip, trailing edge
}};

constexpr std::array<Face, 6> kWingFaces = {{
    {0, 2, 5, Ramp::Wing}, // top
    {0, 5, 4, Ramp::Wing},
    {1, 5, 3, Ramp::Wing}, // bottom
    {1, 4, 5, Ramp::Wing},
    {0, 4, 1, Ramp::Wing}, // leading edge
    {2, 3, 5, Ramp::Wing}, // trailing edge
}};

// The face of the wing whose plane separates the wing from the pod hung under it.
constexpr std::size_t kWingBottomFace = 2;

// The right engine pod, hung just under the right wing: a box tapering towards its front, its rear (the
// nozzle) standing out behind the wing's trailing edge. The left pod is its mirror image.
constexpr std::array<Vec3, 8> kPodVertices = {{
    // 0..3: front, z = -0.40 (top inner, top outer, bottom outer, bottom inner)
    {0.67, -0.06, -0.40},
    {0.81, -0.06, -0.40},
    {0.81, -0.19, -0.40},
    {0.67, -0.19, -0.40},
    // 4..7: rear, z = -2.05, in the same order
    {0.62, -0.01, -2.05},
    {0.86, -0.01, -2.05},
    {0.86, -0.25, -2.05},
    {0.62, -0.25, -2.05},
}};

constexpr std::array<Face, 12> kPodFaces = {{
    {0, 1, 2, Ramp::Hull}, // front
    {0, 2, 3, Ramp::Hull},
    {0, 4, 5, Ramp::Hull}, // top
    {0, 5, 1, Ramp::Hull},
    {1, 5, 6, Ramp::Hull}, // outer side
    {1, 6, 2, Ramp::Hull},
    {2, 6, 7, Ramp::Hull}, // bottom
    {2, 7, 3, Ramp::Hull},
    {3, 7, 4, Ramp::Hull}, // inner side
    {3, 4, 0, Ramp::Hull},
    {4, 6, 5, Ramp::Engine}, // the nozzle, glowing
    {4, 7, 6, Ramp::Engine},
}};

// The engine flames: two crossed triangles (vertical and horizontal) behind each nozzle, drawn from both
// sides, their length and brightness flickering by retroFrame. They all lie behind this plane, the rest of
// the ship in front of it.
constexpr double kNozzleZ = -2.05;
constexpr double kFlamePlaneZ = -2.055;
constexpr Vec3 kNozzleCentre = {0.74, -0.13, kNozzleZ}; // of the right pod
constexpr double kFlameHalfWidth = 0.10;

static_assert(kFuselageFaces.size() + 2 * kWingFaces.size() + 2 * kPodFaces.size() + 2 * 2 ==
              kFlybyShipTriangles);

// One directional light, from the right, a little above and behind the ship (towards the light, world
// space), and the share of light every face gets regardless.
const Vec3 kLight = normalize({0.75, 0.35, -0.55});
constexpr double kAmbient = 0.26;
constexpr double kDiffuse = 1.0 - kAmbient;

constexpr double kFovY = 42; // degrees, the ship's camera and the stars'

// A small integer hash (lowbias32): the stars' fixed seeds and the engines' flicker.
std::uint32_t hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// A number in [0, 1) from a hash.
double unit(std::uint32_t h) {
    return static_cast<double>(h) / 4294967296.0;
}

// A 3x3 rotation, row by row.
struct Rotation {
    double m[3][3];

    Vec3 apply(const Vec3& v) const {
        return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
                m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
                m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
    }
    Vec3 applyInverse(const Vec3& v) const { // the transpose
        return {m[0][0] * v[0] + m[1][0] * v[1] + m[2][0] * v[2],
                m[0][1] * v[0] + m[1][1] * v[1] + m[2][1] * v[2],
                m[0][2] * v[0] + m[1][2] * v[1] + m[2][2] * v[2]};
    }
};

Rotation multiply(const Rotation& a, const Rotation& b) {
    Rotation r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}

// The ship's attitude at t seconds into the chapter: a gentle bank (roll about z), with a slower nod
// (pitch about x) and sway (yaw about y) so it never sits still.
Rotation shipRotation(double t) {
    const double tau = 2 * std::numbers::pi;
    const double roll = radians(13 * std::sin(tau * t / 6.0) + 4 * std::sin(tau * t / 2.9));
    const double pitch = radians(4 * std::sin(tau * t / 4.7));
    const double yaw = radians(5 * std::sin(tau * t / 7.3));
    const double cr = std::cos(roll), sr = std::sin(roll);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const Rotation rz{{{cr, -sr, 0}, {sr, cr, 0}, {0, 0, 1}}};
    const Rotation rx{{{1, 0, 0}, {0, cp, -sp}, {0, sp, cp}}};
    const Rotation ry{{{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}}};
    return multiply(ry, multiply(rx, rz));
}

// A part of the ship ready to draw: its vertices in model space (mirrored for the left side) and faces.
struct Part {
    std::vector<Vec3> vertices;
    std::vector<Face> faces;
    bool flame = false; // a flame: both sides drawn, in the flame's shade of the engine ramp
};

Vec3 centroidOf(const Part& part) {
    Vec3 centroid{0, 0, 0};
    for (const Vec3& v : part.vertices)
        for (int k = 0; k < 3; ++k)
            centroid[k] += v[k] / static_cast<double>(part.vertices.size());
    return centroid;
}

// A face's unit normal, pointing out of its (convex) part.
Vec3 outwardNormal(const Part& part, const Face& face, const Vec3& centroid) {
    const Vec3& a = part.vertices[face.a];
    const Vec3& b = part.vertices[face.b];
    const Vec3& c = part.vertices[face.c];
    const Vec3 normal = normalize(cross(sub(b, a), sub(c, a)));
    const Vec3 middle = {(a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3};
    return dot(normal, sub(middle, centroid)) < 0 ? Vec3{-normal[0], -normal[1], -normal[2]} : normal;
}

template <std::size_t V, std::size_t F>
Part makePart(const std::array<Vec3, V>& vertices, const std::array<Face, F>& faces, bool mirror) {
    Part part;
    for (const Vec3& v : vertices)
        part.vertices.push_back({mirror ? -v[0] : v[0], v[1], v[2]});
    part.faces.assign(faces.begin(), faces.end());
    return part;
}

// The flames of one engine at retroFrame; `side` is +1 for the right engine, -1 for the left.
Part makeFlames(std::uint32_t retroFrame, int side) {
    const std::uint32_t h = hash(retroFrame * 2u + (side > 0 ? 1u : 0u) + 0x9e3779b9u);
    const double length = 0.30 + 0.07 * static_cast<double>(h % 5u);
    const Vec3 c = {side * kNozzleCentre[0], kNozzleCentre[1], kFlamePlaneZ - 0.005};
    Part part;
    part.flame = true;
    part.vertices = {
        {c[0], c[1] + kFlameHalfWidth, c[2]}, {c[0], c[1] - kFlameHalfWidth, c[2]},
        {c[0] - kFlameHalfWidth, c[1], c[2]}, {c[0] + kFlameHalfWidth, c[1], c[2]},
        {c[0], c[1], c[2] - length},
    };
    part.faces = {{0, 1, 4, Ramp::Engine}, {2, 3, 4, Ramp::Engine}};
    return part;
}

std::uint8_t rampBase(Ramp ramp) {
    switch (ramp) {
    case Ramp::Wing:
        return kFlybyWing;
    case Ramp::Cockpit:
        return kFlybyCockpit;
    case Ramp::Engine:
        return kFlybyEngine;
    default:
        return kFlybyHull;
    }
}

// The nozzles' and flames' entries in the engine ramp at retroFrame: near white and bright orange-yellow,
// a few shades of flicker each.
struct EngineShades {
    std::uint8_t nozzle, flame;
};

EngineShades engineShades(std::uint32_t retroFrame, int side) {
    const std::uint32_t h = hash(retroFrame * 2u + (side > 0 ? 1u : 0u));
    return {static_cast<std::uint8_t>(11u + h % 5u), static_cast<std::uint8_t>(5u + (h >> 8) % 5u)};
}

// Draws a part's faces into the scene: each face the camera sees (all of a flame's), flat shaded; the
// engine faces in their flickering shade.
void drawPart(RetroScene& scene, const Camera& camera, const Rotation& rotation, const Vec3& eyeModel,
              const Part& part, EngineShades engine) {
    const Vec3 centroid = centroidOf(part);
    for (const Face& face : part.faces) {
        const Vec3& a = part.vertices[face.a];
        const Vec3& b = part.vertices[face.b];
        const Vec3& c = part.vertices[face.c];
        const Vec3 normal = outwardNormal(part, face, centroid);
        if (!part.flame && dot(normal, sub(eyeModel, a)) <= 0)
            continue; // a back face

        std::uint8_t shade;
        if (face.ramp == Ramp::Engine) {
            shade = part.flame ? engine.flame : engine.nozzle;
        } else {
            const double lambert = std::max(0.0, dot(rotation.apply(normal), kLight));
            const double intensity = kAmbient + kDiffuse * lambert;
            shade = static_cast<std::uint8_t>(
                std::clamp(static_cast<int>(intensity * kFlybyShades), 0, kFlybyShades - 1));
        }
        const std::uint8_t index = static_cast<std::uint8_t>(rampBase(face.ramp) + shade);
        const std::uint8_t indices[3] = {index, index, index};
        double world[3][3];
        for (int k = 0; k < 3; ++k) {
            const Vec3 w = rotation.apply(k == 0 ? a : k == 1 ? b : c);
            world[k][0] = w[0], world[k][1] = w[1], world[k][2] = w[2];
        }
        addTriangle(scene, camera, world, indices, kFlat);
    }
}

// Which side of the plane through a part's face the eye is on: true on the side its outward normal faces.
bool eyeOutside(const Part& part, std::size_t faceIndex, const Vec3& eyeModel) {
    const Face& face = part.faces[faceIndex];
    const Vec3 normal = outwardNormal(part, face, centroidOf(part));
    return dot(normal, sub(eyeModel, part.vertices[face.a])) > 0;
}

// --- The stars -----------------------------------------------------------------------------------------
//
// The stars live in their own view space (a camera at the origin looking along +z with the ship camera's
// field of view) and fly straight at it: a star keeps its x and y while z runs from kStarFar down, so on the
// screen it starts at radius r0 from the centre (in units of the half screen, x and y each in [-1, 1]) and
// streams outwards, faster and brighter as it nears. Its run ends at z = max(kStarNear, r0 * kStarFar /
// kStarExit), at radius kStarExit, the screen's corners (along most directions it has left the screen a
// little before and is skipped); then it starts again at the far end in a new direction (one per run, from
// its seed). r0 and the phase of its run are fixed per star.
constexpr double kStarFar = 10.0;
constexpr double kStarNear = 0.6;
constexpr double kStarSpeed = 4.0; // units per second
constexpr double kStarExit = 1.42;
constexpr std::uint32_t kStarSeed = 1993;

Camera starCamera() {
    return Camera{{0, 0, 0}, {0, 0, 1}, {0, 1, 0}, kFovY};
}

// Whether pixel (px, py) is covered by the triangle as shaders/retro/polys.comp decides it: its centre inside
// or on a top or left edge (exact in 64-bit integers; the vertices are within the guard band).
bool covers(const RetroTriangle& t, std::int64_t px, std::int64_t py) {
    std::int64_t x[3] = {t.x[0], t.x[1], t.x[2]}, y[3] = {t.y[0], t.y[1], t.y[2]};
    const auto edge = [](std::int64_t ax, std::int64_t ay, std::int64_t bx, std::int64_t by, std::int64_t cx,
                         std::int64_t cy) { return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax); };
    const std::int64_t area = edge(x[0], y[0], x[1], y[1], x[2], y[2]);
    if (area == 0)
        return false;
    if (area < 0) {
        std::swap(x[1], x[2]);
        std::swap(y[1], y[2]);
    }
    const std::int64_t cx = px * kSubpixel + kSubpixel / 2, cy = py * kSubpixel + kSubpixel / 2;
    for (int e = 0; e < 3; ++e) {
        const int i = (e + 1) % 3, j = (e + 2) % 3; // edge i -> j, opposite vertex e
        const std::int64_t w = edge(x[i], y[i], x[j], y[j], cx, cy);
        const std::int64_t dy = y[j] - y[i];
        const bool owns = dy < 0 || (dy == 0 && x[j] > x[i]);
        if (!(w > 0 || (w == 0 && owns)))
            return false;
    }
    return true;
}

// Whether any pixel of the point's square is covered by one of the triangles.
bool hidden(const RetroPoint& point, const std::vector<RetroTriangle>& triangles, std::size_t first) {
    const int size = point.size;
    const int firstX = (point.x - size * 8 + 8) >> 4, firstY = (point.y - size * 8 + 8) >> 4;
    for (std::size_t i = first; i < triangles.size(); ++i) {
        const RetroTriangle& t = triangles[i];
        const int minX = (std::min({t.x[0], t.x[1], t.x[2]}) - 8) >> 4;
        const int maxX = (std::max({t.x[0], t.x[1], t.x[2]}) - 8) >> 4;
        const int minY = (std::min({t.y[0], t.y[1], t.y[2]}) - 8) >> 4;
        const int maxY = (std::max({t.y[0], t.y[1], t.y[2]}) - 8) >> 4;
        if (firstX + size - 1 < minX || firstX > maxX + 1 || firstY + size - 1 < minY || firstY > maxY + 1)
            continue;
        for (int y = firstY; y < firstY + size; ++y)
            for (int x = firstX; x < firstX + size; ++x)
                if (covers(t, x, y))
                    return true;
    }
    return false;
}

void addStars(RetroScene& scene, std::uint32_t retroFrame, std::size_t shipFirst) {
    const Camera camera = starCamera();
    const double focalY = (kScreenHeight / 2.0) / std::tan(radians(kFovY / 2));
    const double focalX = focalY * kPixelAspect;
    const double travelled = kStarSpeed * static_cast<double>(retroFrame) / kFlybyRetroRate;

    RetroScene stars;
    for (std::uint32_t i = 0; i < kFlybyStars; ++i) {
        const std::uint32_t seed = hash(kStarSeed * 7919u + i * 2u);
        const double r0 = 0.03 + 0.97 * unit(seed);
        const double end = std::max(kStarNear, r0 * kStarFar / kStarExit);
        const double run = kStarFar - end;
        const double along = unit(hash(seed + 1u)) * run + travelled;
        const auto cycle = static_cast<std::uint32_t>(std::floor(along / run));
        const double z = kStarFar - (along - static_cast<double>(cycle) * run);
        const double angle = 2 * std::numbers::pi * unit(hash(seed ^ (cycle * 0x632be5abu + 0x51u)));
        // At the far end the star is at (r0 cos, r0 sin) of the half screen; x and y keep that line of sight.
        const double world[3] = {r0 * std::cos(angle) * kStarFar * (kScreenWidth / 2.0) / focalX,
                                 r0 * std::sin(angle) * kStarFar * (kScreenHeight / 2.0) / focalY, z};
        const double nearness = std::clamp(1.0 - (z - kStarNear) / (kStarFar - kStarNear), 0.0, 1.0);
        const auto shade = static_cast<std::uint8_t>(std::min(15, static_cast<int>(nearness * kFlybyShades)));
        const std::uint8_t size = z < 0.35 * kStarFar ? 2 : 1;
        addPoint(stars, camera, world, static_cast<std::uint8_t>(kFlybyStar + shade), size);
    }
    sortBackToFront(stars);
    for (std::size_t i = 0; i < stars.points.size(); ++i) {
        if (hidden(stars.points[i], scene.triangles, shipFirst))
            continue;
        scene.points.push_back(stars.points[i]);
        scene.pointDepth.push_back(stars.pointDepth[i]);
    }
}

// Linear from `from` to `to` over `count` entries starting at `first`.
void ramp(Palette& palette, std::size_t first, std::size_t count, const std::array<int, 3>& from,
          const std::array<int, 3>& to) {
    for (std::size_t i = 0; i < count; ++i) {
        const double t = count > 1 ? static_cast<double>(i) / static_cast<double>(count - 1) : 0;
        const auto channel = [&](int c) {
            return static_cast<std::uint8_t>(std::lround(lerp(from[c], to[c], t)));
        };
        palette[first + i] = rgba(channel(0), channel(1), channel(2));
    }
}

} // namespace

Camera flybyCamera(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const double frames = frame.chapterDuration * kFlybyRetroRate;
    const double progress = frames > 0 ? std::clamp(static_cast<double>(retroFrame) / frames, 0.0, 1.0) : 0.0;
    const double eased = progress * progress * (3 - 2 * progress);
    const double t = static_cast<double>(retroFrame) / kFlybyRetroRate;

    // Round the ship from behind (azimuth 0: the camera on the -z side) past its right side to a
    // three-quarter view from ahead, and from below to above; closer at the end.
    const double azimuth = radians(lerp(6, 140, eased) + 3 * std::sin(2 * std::numbers::pi * t / 9.0));
    const double elevation = radians(lerp(-26, 30, eased));
    const double distance = lerp(7.4, 5.6, eased);
    Camera camera{};
    camera.eye[0] = distance * std::cos(elevation) * std::sin(azimuth);
    camera.eye[1] = distance * std::sin(elevation);
    camera.eye[2] = -distance * std::cos(elevation) * std::cos(azimuth);
    // Aimed below the ship, so the ship rides in the upper part of the frame (less so while the camera looks
    // up at it from below, which already lifts it).
    camera.target[0] = 0;
    camera.target[1] = lerp(-0.3, -0.75, eased);
    camera.target[2] = 0;
    camera.up[0] = 0;
    camera.up[1] = 1;
    camera.up[2] = 0;
    camera.fovYDegrees = kFovY;
    return camera;
}

void flybyPalette(Palette& palette, const show::TimelineFrame&, std::uint32_t) {
    palette.fill(rgba(0, 0, 0));
    ramp(palette, kFlybyHull, kFlybyShades, {14, 16, 32}, {200, 212, 238});
    ramp(palette, kFlybyWing, kFlybyShades, {0, 22, 28}, {120, 242, 222});
    ramp(palette, kFlybyCockpit, kFlybyShades, {40, 18, 0}, {255, 216, 100});
    // Engine glow: deep orange to yellow-orange, then on to white.
    ramp(palette, kFlybyEngine, 8, {150, 34, 0}, {255, 150, 24});
    ramp(palette, kFlybyEngine + 8, 8, {255, 170, 50}, {255, 255, 255});
    ramp(palette, kFlybyStar, kFlybyShades, {44, 44, 56}, {240, 240, 255});
    palette[kFlybyWhite] = rgba(255, 255, 255);
}

void flybyScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const Camera camera = flybyCamera(frame, retroFrame);
    const Rotation rotation = shipRotation(static_cast<double>(retroFrame) / kFlybyRetroRate);
    const Vec3 eye = rotation.applyInverse({camera.eye[0], camera.eye[1], camera.eye[2]});
    const std::size_t shipFirst = scene.triangles.size();

    const Part fuselage = makePart(kFuselageVertices, kFuselageFaces, false);
    // Index 0: the right side (+x), 1: the left.
    const Part wings[2] = {makePart(kWingVertices, kWingFaces, false),
                           makePart(kWingVertices, kWingFaces, true)};
    const Part pods[2] = {makePart(kPodVertices, kPodFaces, false), makePart(kPodVertices, kPodFaces, true)};
    const Part flames[2] = {makeFlames(retroFrame, 1), makeFlames(retroFrame, -1)};
    const EngineShades engines[2] = {engineShades(retroFrame, 1), engineShades(retroFrame, -1)};

    // Painter's order from separating planes, like a fixed BSP of the model: of two parts on either side of a
    // plane, the one on the camera's side is drawn last.
    const auto drawFlames = [&] {
        for (int side = 0; side < 2; ++side)
            drawPart(scene, camera, rotation, eye, flames[side], engines[side]);
    };
    // A wing and the pod under it: the wing's bottom face separates them.
    const auto drawSide = [&](int side) {
        if (eyeOutside(wings[side], kWingBottomFace, eye)) { // below the wing: the pod is in front
            drawPart(scene, camera, rotation, eye, wings[side], engines[side]);
            drawPart(scene, camera, rotation, eye, pods[side], engines[side]);
        } else {
            drawPart(scene, camera, rotation, eye, pods[side], engines[side]);
            drawPart(scene, camera, rotation, eye, wings[side], engines[side]);
        }
    };
    const bool behind = eye[2] < kFlamePlaneZ; // the flames lie between the camera and the rest
    if (!behind)
        drawFlames();
    // The plane x = 0 separates the two sides; x = +-kFuselageHalfWidth each side from the fuselage.
    const int nearSide = eye[0] >= 0 ? 0 : 1;
    drawSide(1 - nearSide);
    if (std::abs(eye[0]) > kFuselageHalfWidth) {
        drawPart(scene, camera, rotation, eye, fuselage, engines[0]);
        drawSide(nearSide);
    } else {
        drawSide(nearSide);
        drawPart(scene, camera, rotation, eye, fuselage, engines[0]);
    }
    if (behind)
        drawFlames();

    addStars(scene, retroFrame, shipFirst);
}

} // namespace evo::retro
