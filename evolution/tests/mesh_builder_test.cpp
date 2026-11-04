// The procedural meshes and the transform helpers of the 2026 side, on the CPU: every closed mesh is
// watertight (after welding positions within 1e-6 every edge belongs to exactly two triangles, once in each
// direction), every triangle winds counter-clockwise around its outward vertex normals, normals are unit and
// uvs lie in [0, 1]; on the revolved meshes u grows along cross(up, n) (to the right seen from outside, so an
// image is not mirrored); the sphere's area converges on 4 pi; the Mat34 helpers move points and normals as
// documented.

#include "modern/math.hpp"
#include "modern/mesh_builder.hpp"
#include "rt/geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using evo::modern::Vec2;
using evo::modern::Vec3;
using evo::rt::Mesh;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

template <typename F> bool throwsInvalidArgument(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

Vec3 position(const Mesh& mesh, std::uint32_t i) {
    const auto& p = mesh.vertices[i].position;
    return {p[0], p[1], p[2]};
}

Vec3 normal(const Mesh& mesh, std::uint32_t i) {
    const auto& n = mesh.vertices[i].normal;
    return {n[0], n[1], n[2]};
}

bool near(Vec3 a, Vec3 b, float tolerance) {
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance &&
           std::abs(a.z - b.z) <= tolerance;
}

// Indices are triangles into the mesh's own vertices.
void checkIndices(const Mesh& mesh) {
    CHECK(!mesh.vertices.empty());
    CHECK(!mesh.indices.empty());
    CHECK(mesh.indices.size() % 3 == 0);
    CHECK(std::all_of(mesh.indices.begin(), mesh.indices.end(),
                      [&](std::uint32_t i) { return i < mesh.vertices.size(); }));
}

// Each vertex's representative: the first vertex at the same position within 1e-6.
std::vector<std::uint32_t> weld(const Mesh& mesh) {
    std::vector<std::uint32_t> id(mesh.vertices.size());
    for (std::uint32_t i = 0; i < mesh.vertices.size(); ++i) {
        id[i] = i;
        for (std::uint32_t j = 0; j < i; ++j) {
            if (id[j] == j && near(position(mesh, i), position(mesh, j), 1e-6f)) {
                id[i] = j;
                break;
            }
        }
    }
    return id;
}

// Closed and consistently oriented: after welding no triangle is degenerate, every directed edge occurs once
// and its reverse occurs once, so every edge has exactly two triangles winding it in opposite directions.
bool watertight(const Mesh& mesh) {
    const std::vector<std::uint32_t> id = weld(mesh);
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> directed;
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const std::array<std::uint32_t, 3> v{id[mesh.indices[t]], id[mesh.indices[t + 1]],
                                             id[mesh.indices[t + 2]]};
        if (v[0] == v[1] || v[1] == v[2] || v[2] == v[0])
            return false;
        for (int k = 0; k < 3; ++k)
            ++directed[{v[k], v[(k + 1) % 3]}];
    }
    for (const auto& [edge, count] : directed) {
        if (count != 1)
            return false;
        const auto reverse = directed.find({edge.second, edge.first});
        if (reverse == directed.end() || reverse->second != 1)
            return false;
    }
    return true;
}

Vec3 faceNormal(const Mesh& mesh, std::size_t t) {
    const Vec3 a = position(mesh, mesh.indices[t]);
    const Vec3 b = position(mesh, mesh.indices[t + 1]);
    const Vec3 c = position(mesh, mesh.indices[t + 2]);
    return evo::modern::cross(b - a, c - a);
}

// Counter-clockwise around the outward normals: the winding normal agrees with every vertex normal.
bool windsWithNormals(const Mesh& mesh) {
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const Vec3 n = faceNormal(mesh, t);
        for (int k = 0; k < 3; ++k)
            if (!(evo::modern::dot(n, normal(mesh, mesh.indices[t + k])) > 0.f))
                return false;
    }
    return true;
}

bool unitNormals(const Mesh& mesh) {
    for (std::uint32_t i = 0; i < mesh.vertices.size(); ++i)
        if (!(std::abs(evo::modern::length(normal(mesh, i)) - 1.f) <= 1e-5f))
            return false;
    return true;
}

bool uvsInUnitSquare(const Mesh& mesh) {
    return std::all_of(mesh.vertices.begin(), mesh.vertices.end(), [](const evo::rt::Vertex& v) {
        return v.uv[0] >= 0.f && v.uv[0] <= 1.f && v.uv[1] >= 0.f && v.uv[1] <= 1.f;
    });
}

// Not mirrored: on every triangle of the revolved surface u grows along cross(+y, n), to the viewer's right
// seen from outside with +y up. Triangles facing straight up or down (caps) and those without a u gradient
// are skipped; at least `minimum` must be checked.
bool uGrowsRightward(const Mesh& mesh, std::size_t minimum) {
    std::size_t checked = 0;
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const std::uint32_t i0 = mesh.indices[t], i1 = mesh.indices[t + 1], i2 = mesh.indices[t + 2];
        const Vec3 e1 = position(mesh, i1) - position(mesh, i0);
        const Vec3 e2 = position(mesh, i2) - position(mesh, i0);
        const float du1 = mesh.vertices[i1].uv[0] - mesh.vertices[i0].uv[0];
        const float du2 = mesh.vertices[i2].uv[0] - mesh.vertices[i0].uv[0];
        const Vec3 n = faceNormal(mesh, t);
        const Vec3 right = evo::modern::cross({0.f, 1.f, 0.f}, n);
        if (evo::modern::length(right) < 1e-3f * evo::modern::length(n) || (du1 == 0.f && du2 == 0.f))
            continue;
        // The gradient of u in the triangle's plane, up to the positive factor 1 / (|e1|^2 |e2|^2 -
        // (e1.e2)^2).
        const float e11 = evo::modern::dot(e1, e1), e22 = evo::modern::dot(e2, e2),
                    e12 = evo::modern::dot(e1, e2);
        const Vec3 gradient = e1 * (du1 * e22 - du2 * e12) + e2 * (du2 * e11 - du1 * e12);
        if (!(evo::modern::dot(gradient, right) > 0.f))
            return false;
        ++checked;
    }
    return checked >= minimum;
}

double area(const Mesh& mesh) {
    double sum = 0;
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3)
        sum += 0.5 * evo::modern::length(faceNormal(mesh, t));
    return sum;
}

std::pair<Vec3, Vec3> bounds(const Mesh& mesh) {
    Vec3 lo = position(mesh, 0), hi = lo;
    for (std::uint32_t i = 1; i < mesh.vertices.size(); ++i) {
        const Vec3 p = position(mesh, i);
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    return {lo, hi};
}

// Everything a closed mesh promises.
void checkClosed(const Mesh& mesh) {
    checkIndices(mesh);
    CHECK(watertight(mesh));
    CHECK(windsWithNormals(mesh));
    CHECK(unitNormals(mesh));
    CHECK(uvsInUnitSquare(mesh));
}

void testBox() {
    const Vec3 half{0.5f, 1.f, 2.f};
    const Mesh mesh = evo::modern::box(half);
    checkClosed(mesh);
    CHECK(mesh.vertices.size() == 24 && mesh.indices.size() == 36);
    const auto [lo, hi] = bounds(mesh);
    CHECK(near(lo, -half, 0.f) && near(hi, half, 0.f));
    CHECK(std::abs(area(mesh) - 8.0 * (0.5 * 1 + 1 * 2 + 2 * 0.5)) < 1e-4);
    // Flat: every normal is one of the six axis directions.
    for (std::uint32_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3 n = normal(mesh, i);
        CHECK(std::abs(n.x) + std::abs(n.y) + std::abs(n.z) == 1.f);
    }
    // Uvs span each face.
    float uMin = 1, uMax = 0;
    for (const auto& v : mesh.vertices) {
        uMin = std::min(uMin, v.uv[0]);
        uMax = std::max(uMax, v.uv[0]);
    }
    CHECK(uMin == 0.f && uMax == 1.f);
    CHECK(throwsInvalidArgument([] { evo::modern::box({0.f, 1.f, 1.f}); }));
}

void testBevelBox() {
    const Vec3 half{0.2f, 0.18f, 0.21f};
    for (int segments : {1, 4}) {
        const Mesh mesh = evo::modern::bevelBox(half, 0.03f, segments);
        checkClosed(mesh);
        const auto [lo, hi] = bounds(mesh);
        CHECK(near(lo, -half, 1e-6f) && near(hi, half, 1e-6f));
        // Smaller than the sharp box, larger than the box without its bevel band.
        const double boxArea = 8.0 * (0.2 * 0.18 + 0.18 * 0.21 + 0.21 * 0.2);
        const double innerArea = 8.0 * (0.17 * 0.15 + 0.15 * 0.18 + 0.18 * 0.17);
        CHECK(area(mesh) < boxArea && area(mesh) > innerArea);
    }
    // More segments round the edges more closely: the area approaches the analytic rounded box
    // (faces + quarter cylinders + sphere corners).
    {
        const float r = 0.03f;
        const double ex = 0.17, ey = 0.15, ez = 0.18;
        const double analytic = 8.0 * (ex * ey + ey * ez + ez * ex) +
                                2.0 * std::numbers::pi * r * 2.0 * (ex + ey + ez) +
                                4.0 * std::numbers::pi * r * r;
        CHECK(std::abs(area(evo::modern::bevelBox(half, r, 8)) - analytic) / analytic < 0.002);
    }
    CHECK(throwsInvalidArgument([] { evo::modern::bevelBox({0.2f, 0.2f, 0.2f}, 0.f, 4); }));
    CHECK(throwsInvalidArgument([] { evo::modern::bevelBox({0.2f, 0.1f, 0.2f}, 0.1f, 4); }));
    CHECK(throwsInvalidArgument([] { evo::modern::bevelBox({0.2f, 0.2f, 0.2f}, 0.05f, 0); }));
}

void testCylinder() {
    const Mesh capped = evo::modern::cylinder(0.5f, 2.f, 32, true);
    checkClosed(capped);
    CHECK(uGrowsRightward(capped, 64));
    const auto [lo, hi] = bounds(capped);
    CHECK(std::abs(lo.y + 1.f) < 1e-6f && std::abs(hi.y - 1.f) < 1e-6f);
    CHECK(std::abs(hi.x - 0.5f) < 1e-6f && std::abs(lo.z + 0.5f) < 1e-6f);
    const double analytic = 2.0 * std::numbers::pi * 0.5 * 2.0 + 2.0 * std::numbers::pi * 0.25;
    CHECK(std::abs(area(capped) - analytic) / analytic < 0.01);
    // The side is smooth: its normals are radial.
    for (std::uint32_t i = 0; i < capped.vertices.size(); ++i) {
        const Vec3 n = normal(capped, i);
        CHECK(std::abs(n.y) == 1.f || std::abs(n.y) < 1e-6f);
    }

    const Mesh open = evo::modern::cylinder(0.5f, 2.f, 16, false);
    checkIndices(open);
    CHECK(!watertight(open));
    CHECK(windsWithNormals(open) && unitNormals(open) && uvsInUnitSquare(open));
    CHECK(throwsInvalidArgument([] { evo::modern::cylinder(0.5f, 1.f, 2, true); }));
    CHECK(throwsInvalidArgument([] { evo::modern::cylinder(0.f, 1.f, 8, true); }));
}

void testLathe() {
    // A closed vase: flat base, belly, neck, lip, flat top.
    const std::vector<Vec2> profile{{0.f, 0.f},   {0.5f, 0.f},  {0.6f, 0.3f},
                                    {0.4f, 0.8f}, {0.45f, 1.f}, {0.f, 1.f}};
    const Mesh mesh = evo::modern::lathe(profile, 48);
    checkClosed(mesh);
    CHECK(uGrowsRightward(mesh, 5 * 48));
    const auto [lo, hi] = bounds(mesh);
    CHECK(std::abs(lo.y) < 1e-6f && std::abs(hi.y - 1.f) < 1e-6f);
    CHECK(std::abs(hi.x - 0.6f) < 1e-6f);

    // A profile that does not reach the axis is an open tube.
    const std::vector<Vec2> tube{{0.3f, 0.f}, {0.3f, 1.f}};
    const Mesh open = evo::modern::lathe(tube, 24);
    checkIndices(open);
    CHECK(!watertight(open));
    CHECK(windsWithNormals(open) && unitNormals(open) && uvsInUnitSquare(open));
    CHECK(uGrowsRightward(open, 48));

    const std::vector<Vec2> one{{0.3f, 0.f}};
    CHECK(throwsInvalidArgument([&] { evo::modern::lathe(one, 24); }));
    CHECK(throwsInvalidArgument([&] { evo::modern::lathe(tube, 2); }));
    const std::vector<Vec2> negative{{-0.1f, 0.f}, {0.3f, 1.f}};
    CHECK(throwsInvalidArgument([&] { evo::modern::lathe(negative, 24); }));
}

void testSphere() {
    const Mesh mesh = evo::modern::uvSphere(1.f, 32, 64);
    checkClosed(mesh);
    CHECK(uGrowsRightward(mesh, 32 * 64));
    // Seen from +x with +y up, the viewer's right is -z: the vertex at u = 1/4 on the equator lies there.
    bool sawQuarter = false;
    for (const auto& v : mesh.vertices)
        if (v.uv[0] == .25f && v.uv[1] == .5f) {
            CHECK(near({v.position[0], v.position[1], v.position[2]}, {0.f, 0.f, -1.f}, 1e-6f));
            sawQuarter = true;
        }
    CHECK(sawQuarter);
    CHECK(std::abs(area(mesh) - 4.0 * std::numbers::pi) / (4.0 * std::numbers::pi) < 0.01);
    // Smooth: the normal is the direction from the centre.
    for (std::uint32_t i = 0; i < mesh.vertices.size(); ++i)
        CHECK(near(normal(mesh, i), position(mesh, i), 1e-5f));

    const Mesh small = evo::modern::uvSphere(0.03f, 8, 12);
    checkClosed(small);
    CHECK(uGrowsRightward(small, 8 * 12));
    CHECK(throwsInvalidArgument([] { evo::modern::uvSphere(1.f, 1, 8); }));
    CHECK(throwsInvalidArgument([] { evo::modern::uvSphere(1.f, 4, 2); }));
}

void testQuad() {
    const Mesh mesh = evo::modern::quad(0.28f, 0.21f);
    checkIndices(mesh);
    CHECK(mesh.vertices.size() == 4 && mesh.indices.size() == 6);
    CHECK(windsWithNormals(mesh) && unitNormals(mesh) && uvsInUnitSquare(mesh));
    for (std::uint32_t i = 0; i < 4; ++i) {
        CHECK(near(normal(mesh, i), {0.f, 0.f, 1.f}, 0.f));
        const Vec3 p = position(mesh, i);
        CHECK(std::abs(std::abs(p.x) - 0.14f) < 1e-7f && std::abs(std::abs(p.y) - 0.105f) < 1e-7f &&
              p.z == 0.f);
        // The image is upright: uv (0, 0) at the top left, v grows downwards.
        CHECK(mesh.vertices[i].uv[0] == (p.x > 0 ? 1.f : 0.f));
        CHECK(mesh.vertices[i].uv[1] == (p.y > 0 ? 0.f : 1.f));
    }
    CHECK(std::abs(area(mesh) - 0.28 * 0.21) < 1e-7);
    CHECK(throwsInvalidArgument([] { evo::modern::quad(0.f, 1.f); }));
}

void testMath() {
    using namespace evo::modern;
    const Vec3 p{1.f, 2.f, 3.f};
    CHECK(near(transformPoint(identity(), p), p, 0.f));

    const evo::rt::Mat34 t = translate({0.5f, -1.f, 2.f});
    CHECK(t.m[0][3] == 0.5f && t.m[1][3] == -1.f && t.m[2][3] == 2.f); // row-major 3x4
    CHECK(near(transformPoint(t, p), {1.5f, 1.f, 5.f}, 0.f));
    CHECK(near(transformNormal(t, {0.f, 1.f, 0.f}), {0.f, 1.f, 0.f}, 0.f));

    // Right-handed: a quarter turn about +y takes +x to -z, about +z takes +x to +y.
    const float quarter = std::numbers::pi_v<float> / 2;
    CHECK(near(transformPoint(rotate({0.f, 1.f, 0.f}, quarter), {1.f, 0.f, 0.f}), {0.f, 0.f, -1.f}, 1e-6f));
    CHECK(near(transformPoint(rotate({0.f, 0.f, 2.f}, quarter), {1.f, 0.f, 0.f}), {0.f, 1.f, 0.f}, 1e-6f));

    CHECK(near(transformPoint(scale({2.f, 3.f, 4.f}), p), {2.f, 6.f, 12.f}, 0.f));

    // compose(a, b) applies b first.
    const evo::rt::Mat34 ts = compose(t, scale({2.f, 2.f, 2.f}));
    CHECK(near(transformPoint(ts, p), {2.5f, 3.f, 8.f}, 1e-6f));
    const evo::rt::Mat34 st = compose(scale({2.f, 2.f, 2.f}), t);
    CHECK(near(transformPoint(st, p), {3.f, 2.f, 10.f}, 1e-6f));

    // Normals stay perpendicular to the surface under a non-uniform scale and a rotation, and stay unit.
    const evo::rt::Mat34 m = compose(rotate(normalize(Vec3{1.f, 1.f, 0.f}), 0.7f), scale({1.f, 4.f, 0.5f}));
    const Vec3 n = normalize(Vec3{1.f, 1.f, 1.f});
    const Vec3 tangent = normalize(Vec3{1.f, -1.f, 0.f}); // dot(n, tangent) == 0
    const Vec3 movedTangent = transformPoint(m, tangent) - transformPoint(m, {0.f, 0.f, 0.f});
    const Vec3 movedNormal = transformNormal(m, n);
    CHECK(std::abs(dot(movedNormal, movedTangent)) < 1e-5f);
    CHECK(std::abs(length(movedNormal) - 1.f) < 1e-6f);
    CHECK(dot(movedNormal, transformPoint(m, n) - transformPoint(m, {0.f, 0.f, 0.f})) > 0.f);

    CHECK(near(cross({1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}), {0.f, 0.f, 1.f}, 0.f));
    CHECK(dot(p, p) == 14.f);
}

} // namespace

int main() {
    try {
        testMath();
        testBox();
        testBevelBox();
        testCylinder();
        testLathe();
        testSphere();
        testQuad();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("mesh_builder_test: %d checks passed\n", checksPassed);
    return 0;
}
