#pragma once

#include "show/timeline.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace evo::retro {

// The 1993 polygon and point effects work as they did on a 486: the CPU transforms the scene once per retro
// frame (in double precision), clips, projects and quantises it, and hands the GPU a list of screen-space
// primitives in painter's order; shaders/retro/polys.comp and shaders/retro/points.comp draw them into the
// 320x200 palette index image (render::SceneRaster).

// Screen space: 320x200 pixels, x right, y down; pixel (i, j) covers [i, i + 1) x [j, j + 1) and its centre
// is (i + 0.5, j + 0.5). Coordinates are fixed point 28.4: kSubpixel units a pixel.
inline constexpr std::int32_t kScreenWidth = 320;
inline constexpr std::int32_t kScreenHeight = 200;
inline constexpr std::int32_t kSubpixel = 16;

// The most primitives one retro frame may hand the rasterisers (the retro stage throws past them).
inline constexpr std::size_t kMaxTriangles = 2048;
inline constexpr std::size_t kMaxPoints = 8192;

// How a triangle writes its pixels (RetroTriangle::mode).
inline constexpr std::uint8_t kFlat = 0;    // index[0] everywhere
inline constexpr std::uint8_t kGouraud = 1; // the vertex indices interpolated, 4x4 Bayer-dithered
inline constexpr std::uint8_t kGlenz = 2;   // pixel |= index[0]: overlapping glenz faces OR their codes

// One triangle of the polygon rasteriser (std430, 32 bytes). Vertices are in 28.4 fixed point and may come in
// either winding. A pixel is covered when its centre is inside the triangle, or on an edge that is a top or a
// left edge (the top-left rule), so triangles that share an edge never both draw, nor both miss, a pixel on
// it. Later triangles overwrite earlier ones (flat, gouraud); glenz ORs. Gouraud: the index at a pixel is
// floor(v + (bayer + 0.5) / 16), where v interpolates the three vertex indices with the pixel's barycentric
// weights (18-bit fixed point) and bayer is the 4x4 threshold 0..15 (shaders/retro/common.glsl).
struct RetroTriangle {
    std::int32_t x[3], y[3];
    std::uint8_t index[3]; // per-vertex palette index (flat and glenz read index[0])
    std::uint8_t mode;     // kFlat, kGouraud or kGlenz
    std::uint8_t pad[4] = {};
};
static_assert(sizeof(RetroTriangle) == 32);

// One point of the point rasteriser (std430, 12 bytes): a size x size square of index at (x, y) in 28.4
// fixed point. The square's top-left pixel is (floor(x / 16 - size / 2 + 1 / 2), same for y): a size-1 point
// is the pixel its position falls in, a size-2 point the 2x2 block whose centre is nearest. Pixels off the
// screen are skipped; where points overlap, the later one is drawn.
struct RetroPoint {
    std::int32_t x, y;
    std::uint8_t index;
    std::uint8_t size;
    std::uint8_t pad[2] = {};
};
static_assert(sizeof(RetroPoint) == 12);

// The top-left pixel of a point's square, as points.comp computes it: (x - size * 8 + 8) >> 4, the same for y
// (an arithmetic shift, so a floor left of and above the screen too).
struct PointCorner {
    std::int32_t x, y;
};
constexpr PointCorner pointCorner(const RetroPoint& point) {
    const std::int32_t half = std::int32_t{point.size} * 8;
    return {(point.x - half + 8) >> 4, (point.y - half + 8) >> 4};
}

// One retro frame's primitives in draw order. addTriangle and addPoint also record each primitive's view
// depth in triangleDepth / pointDepth (one entry per primitive), which sortBackToFront orders by. A provider
// that writes primitives directly in screen space may leave the depths empty if it does not sort.
struct RetroScene {
    std::vector<RetroTriangle> triangles;
    std::vector<RetroPoint> points;
    std::vector<double> triangleDepth;
    std::vector<double> pointDepth;

    void clear() {
        triangles.clear();
        points.clear();
        triangleDepth.clear();
        pointDepth.clear();
    }
};

// Builds a retro frame's scene. Like every builder and provider of retro/effects.hpp it must be a pure
// function of the chapter and the retro frame: the stage clears the scene and calls it only when it
// re-renders.
using SceneProvider = void (*)(RetroScene&, const show::TimelineFrame&, std::uint32_t retroFrame);

// A pinhole camera. Its view space has x right, y up and z along the view direction (positive in front);
// fovYDegrees spans the 200 rows. Pixels are 1.2 times taller than wide (320x200 shown at 4:3), so a unit
// across covers 1.2 times as many pixels as a unit up.
struct Camera {
    double eye[3], target[3], up[3], fovYDegrees;
};

inline constexpr double kNearPlane = 0.1;   // view-space z of the near clipping plane
inline constexpr double kPixelAspect = 1.2; // pixel height over width on the 4:3 display

// Where the 2D clipper cuts triangles that reach far off the screen, in pixels: inside this band every edge
// function of polys.comp fits in 32 bits. Only triangles crossing it are cut, and the cut lies off screen.
inline constexpr double kGuardMinX = -512, kGuardMaxX = kScreenWidth + 512;
inline constexpr double kGuardMinY = -512, kGuardMaxY = kScreenHeight + 512;

// Checks a scene the way the rasterisers need it (render::SceneRaster::draw runs it on every scene): throws
// std::length_error for more than kMaxTriangles triangles or kMaxPoints points, std::invalid_argument for a
// triangle vertex or a point outside the guard band (bounds included). addTriangle and addPoint keep within
// the band; the limits are the provider's to keep.
void checkScene(const RetroScene& scene);

// A point in view space and on the screen (pixels, unquantised). x and y are meaningful only when z > 0.
struct Projected {
    double viewX, viewY, z; // view space
    double x, y;            // screen
};
Projected project(const Camera& camera, const double world[3]);

// Rounds a screen coordinate in pixels to 28.4 fixed point (std::lround of x * 16).
std::int32_t toFixed(double pixels);

// Adds a world-space triangle: clips it against z >= kNearPlane in view space, projects it, cuts it to the
// guard band, quantises the vertices to 28.4 and appends the pieces (none for a triangle wholly behind the
// camera, off the screen or of zero area once quantised; a clipped triangle becomes a fan of up to five).
// Vertices made by clipping take the index interpolated along their edge, rounded. Every piece records the
// mean view depth of the three original vertices. Shared edges stay shared: an edge is clipped the same way
// in both triangles that use it, whichever way round they list it.
void addTriangle(RetroScene& scene, const Camera& camera, const double world[3][3],
                 const std::uint8_t index[3], std::uint8_t mode);
// Adds a world-space point unless it is behind the near plane or its square lies wholly off the screen;
// records its view depth.
void addPoint(RetroScene& scene, const Camera& camera, const double world[3], std::uint8_t index,
              std::uint8_t size);
// Orders the triangles and the points farthest first (painter's order), each list on its own; stable, so
// equal depths (the pieces of one clipped triangle) keep their order. Throws std::logic_error when a depth
// list is not as long as its primitive list.
void sortBackToFront(RetroScene& scene);

} // namespace evo::retro
