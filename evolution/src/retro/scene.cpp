#include "retro/scene.hpp"
#include "retro/vec3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace evo::retro {

namespace {

Vec3 toVec(const double v[3]) {
    return {v[0], v[1], v[2]};
}

// The camera's view basis and its focal lengths in pixels.
struct View {
    Vec3 eye, right, up, forward;
    double focalX, focalY;
};

View viewOf(const Camera& camera) {
    View view;
    view.eye = toVec(camera.eye);
    view.forward = normalize(sub(toVec(camera.target), view.eye));
    view.right = normalize(cross(toVec(camera.up), view.forward));
    view.up = cross(view.forward, view.right);
    view.focalY = (kScreenHeight / 2.0) / std::tan(camera.fovYDegrees * std::numbers::pi / 360.0);
    view.focalX = view.focalY * kPixelAspect;
    return view;
}

// A polygon vertex while it is clipped: (x, y, z) in view space before projection, (x, y) on the screen in
// pixels after it (z still the view depth); the palette index interpolates along clipped edges.
struct Vertex {
    double x, y, z, index;
};

Vertex toView(const View& view, const double world[3], double index) {
    const Vec3 d = sub(toVec(world), view.eye);
    return {dot(d, view.right), dot(d, view.up), dot(d, view.forward), index};
}

Vertex toScreen(const View& view, const Vertex& v) {
    return {kScreenWidth / 2.0 + view.focalX * v.x / v.z, kScreenHeight / 2.0 - view.focalY * v.y / v.z, v.z,
            v.index};
}

// Orders the two ends of an edge the same way whichever way round a triangle lists them.
bool before(const Vertex& a, const Vertex& b) {
    if (a.x != b.x)
        return a.x < b.x;
    if (a.y != b.y)
        return a.y < b.y;
    if (a.z != b.z)
        return a.z < b.z;
    return a.index < b.index;
}

// Where the edge a-b crosses a clip plane, given the signed distances of its ends (opposite signs). The
// ends are put in a canonical order first, so the two triangles that share the edge get the same point to
// the last bit, which keeps the edge shared after quantisation.
Vertex cut(Vertex a, Vertex b, double da, double db) {
    if (before(b, a)) {
        std::swap(a, b);
        std::swap(da, db);
    }
    const double t = da / (da - db);
    return {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z),
            a.index + t * (b.index - a.index)};
}

// Sutherland-Hodgman against one plane: keeps the part where distance(v) >= 0.
template <typename Distance> std::vector<Vertex> clip(const std::vector<Vertex>& polygon, Distance distance) {
    std::vector<Vertex> out;
    const std::size_t n = polygon.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vertex& current = polygon[i];
        const Vertex& next = polygon[(i + 1) % n];
        const double dc = distance(current), dn = distance(next);
        if (dc >= 0)
            out.push_back(current);
        if ((dc >= 0) != (dn >= 0))
            out.push_back(cut(current, next, dc, dn));
    }
    return out;
}

std::uint8_t toIndex(double index) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(index), 0L, 255L));
}

// The first pixel centre is at 8 in 28.4, the last at 16 * (size - 1) + 8.
constexpr std::int32_t kLastCentreX = kSubpixel * (kScreenWidth - 1) + kSubpixel / 2;
constexpr std::int32_t kLastCentreY = kSubpixel * (kScreenHeight - 1) + kSubpixel / 2;
constexpr std::int32_t kFirstCentre = kSubpixel / 2;

} // namespace

Projected project(const Camera& camera, const double world[3]) {
    const View view = viewOf(camera);
    const Vertex v = toView(view, world, 0);
    const Vertex s = toScreen(view, v);
    return {v.x, v.y, v.z, s.x, s.y};
}

std::int32_t toFixed(double pixels) {
    return static_cast<std::int32_t>(std::lround(pixels * kSubpixel));
}

void checkScene(const RetroScene& scene) {
    if (scene.triangles.size() > kMaxTriangles)
        throw std::length_error(std::to_string(scene.triangles.size()) +
                                " triangles in one retro frame, more than " + std::to_string(kMaxTriangles));
    if (scene.points.size() > kMaxPoints)
        throw std::length_error(std::to_string(scene.points.size()) +
                                " points in one retro frame, more than " + std::to_string(kMaxPoints));
    // Within the guard band every edge function of polys.comp fits in 32 bits.
    const auto inGuardBand = [](std::int32_t x, std::int32_t y) {
        return x >= toFixed(kGuardMinX) && x <= toFixed(kGuardMaxX) && y >= toFixed(kGuardMinY) &&
               y <= toFixed(kGuardMaxY);
    };
    for (std::size_t i = 0; i < scene.triangles.size(); ++i) {
        const RetroTriangle& t = scene.triangles[i];
        for (int v = 0; v < 3; ++v)
            if (!inGuardBand(t.x[v], t.y[v]))
                throw std::invalid_argument("triangle " + std::to_string(i) +
                                            " has a vertex outside the guard band");
    }
    for (std::size_t i = 0; i < scene.points.size(); ++i)
        if (!inGuardBand(scene.points[i].x, scene.points[i].y))
            throw std::invalid_argument("point " + std::to_string(i) + " is outside the guard band");
}

void addTriangle(RetroScene& scene, const Camera& camera, const double world[3][3],
                 const std::uint8_t index[3], std::uint8_t mode) {
    const View view = viewOf(camera);
    std::vector<Vertex> polygon;
    double depth = 0;
    for (int v = 0; v < 3; ++v) {
        // Flat and glenz faces draw index[0] alone; so do all their pieces.
        polygon.push_back(toView(view, world[v], mode == kGouraud ? index[v] : index[0]));
        depth += polygon.back().z;
        if (!std::isfinite(polygon.back().x) || !std::isfinite(polygon.back().y) ||
            !std::isfinite(polygon.back().z))
            return;
    }
    depth /= 3;

    // Near plane in view space, then the guard band on the screen.
    polygon = clip(polygon, [](const Vertex& v) { return v.z - kNearPlane; });
    for (Vertex& v : polygon)
        v = toScreen(view, v);
    polygon = clip(polygon, [](const Vertex& v) { return v.x - kGuardMinX; });
    polygon = clip(polygon, [](const Vertex& v) { return kGuardMaxX - v.x; });
    polygon = clip(polygon, [](const Vertex& v) { return v.y - kGuardMinY; });
    polygon = clip(polygon, [](const Vertex& v) { return kGuardMaxY - v.y; });
    if (polygon.size() < 3)
        return;

    // Quantised once per vertex, so the fan's pieces share their edges exactly.
    struct Fixed {
        std::int32_t x, y;
        std::uint8_t index;
    };
    std::vector<Fixed> fixed;
    // A vertex made by clipping rounds its interpolated gouraud index to a whole entry, so the gradient of a
    // clipped face can shift by up to half an index. Accepted: half a step of a shade ramp does not show.
    for (const Vertex& v : polygon)
        fixed.push_back({toFixed(v.x), toFixed(v.y), toIndex(v.index)});

    for (std::size_t i = 1; i + 1 < fixed.size(); ++i) {
        const Fixed& a = fixed[0];
        const Fixed& b = fixed[i];
        const Fixed& c = fixed[i + 1];
        const std::int64_t area =
            std::int64_t{b.x - a.x} * (c.y - a.y) - std::int64_t{b.y - a.y} * (c.x - a.x);
        if (area == 0)
            continue;
        // No pixel centre inside the bounding box: nothing to draw.
        if (std::max({a.x, b.x, c.x}) < kFirstCentre || std::min({a.x, b.x, c.x}) > kLastCentreX ||
            std::max({a.y, b.y, c.y}) < kFirstCentre || std::min({a.y, b.y, c.y}) > kLastCentreY)
            continue;
        RetroTriangle triangle{};
        triangle.x[0] = a.x, triangle.y[0] = a.y, triangle.index[0] = a.index;
        triangle.x[1] = b.x, triangle.y[1] = b.y, triangle.index[1] = b.index;
        triangle.x[2] = c.x, triangle.y[2] = c.y, triangle.index[2] = c.index;
        triangle.mode = mode;
        scene.triangles.push_back(triangle);
        scene.triangleDepth.push_back(depth);
    }
}

void addPoint(RetroScene& scene, const Camera& camera, const double world[3], std::uint8_t index,
              std::uint8_t size) {
    const View view = viewOf(camera);
    const Vertex v = toView(view, world, index);
    if (!(v.z >= kNearPlane) || size == 0)
        return;
    const Vertex s = toScreen(view, v);
    // Far outside the guard band even the largest square is off screen; inside it, 28.4 cannot overflow.
    if (!(s.x >= kGuardMinX && s.x <= kGuardMaxX && s.y >= kGuardMinY && s.y <= kGuardMaxY))
        return;
    RetroPoint point{};
    point.x = toFixed(s.x);
    point.y = toFixed(s.y);
    point.index = index;
    point.size = size;
    // The square's first pixel as points.comp computes it.
    const std::int32_t firstX = (point.x - size * 8 + 8) >> 4, firstY = (point.y - size * 8 + 8) >> 4;
    if (firstX + size <= 0 || firstX >= kScreenWidth || firstY + size <= 0 || firstY >= kScreenHeight)
        return;
    scene.points.push_back(point);
    scene.pointDepth.push_back(v.z);
}

namespace {

template <typename T> void sortByDepth(std::vector<T>& primitives, std::vector<double>& depths) {
    std::vector<std::size_t> order(primitives.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&depths](std::size_t a, std::size_t b) { return depths[a] > depths[b]; });
    std::vector<T> sorted;
    std::vector<double> sortedDepths;
    sorted.reserve(order.size());
    sortedDepths.reserve(order.size());
    for (std::size_t i : order) {
        sorted.push_back(primitives[i]);
        sortedDepths.push_back(depths[i]);
    }
    primitives = std::move(sorted);
    depths = std::move(sortedDepths);
}

} // namespace

void sortBackToFront(RetroScene& scene) {
    // Checked before anything moves, so a scene that throws is left as it was.
    if (scene.triangles.size() != scene.triangleDepth.size())
        throw std::logic_error("sortBackToFront: " + std::to_string(scene.triangles.size()) +
                               " triangles but " + std::to_string(scene.triangleDepth.size()) + " depths");
    if (scene.points.size() != scene.pointDepth.size())
        throw std::logic_error("sortBackToFront: " + std::to_string(scene.points.size()) + " points but " +
                               std::to_string(scene.pointDepth.size()) + " depths");
    sortByDepth(scene.triangles, scene.triangleDepth);
    sortByDepth(scene.points, scene.pointDepth);
}

} // namespace evo::retro
