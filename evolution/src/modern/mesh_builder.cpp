#include "modern/mesh_builder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace evo::modern {

namespace {

void require(bool condition, const char* what) {
    if (!condition)
        throw std::invalid_argument(what);
}

std::uint32_t addVertex(rt::Mesh& mesh, Vec3 p, Vec3 n, float u, float v) {
    mesh.vertices.push_back(
        {{p.x, p.y, p.z}, {n.x, n.y, n.z}, {std::clamp(u, 0.f, 1.f), std::clamp(v, 0.f, 1.f)}});
    return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
}

void addTriangle(rt::Mesh& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    mesh.indices.insert(mesh.indices.end(), {a, b, c});
}

float component(Vec3 v, int axis) {
    return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

Vec3 mul(Vec3 a, Vec3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

// One face of an axis-aligned box: its outward normal and in-face axes with cross(u, v) == normal, so the
// corners (-u -v), (+u -v), (+u +v), (-u +v) run counter-clockwise seen from outside.
struct Face {
    Vec3 normal, u, v;
};

constexpr std::array<Face, 6> kFaces{{
    {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
    {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
    {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
    {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
}};

int axisOf(Vec3 unitAxis) {
    return unitAxis.x != 0 ? 0 : unitAxis.y != 0 ? 1 : 2;
}

// A ring of `segments` + 1 columns per profile point (the last column repeats the first one's position with
// u = 1), quads between consecutive points, a single triangle where one side is a pole (radius 0).
void revolve(rt::Mesh& mesh, std::span<const float> radii, std::span<const float> ys,
             std::span<const Vec2> normals /* (radial, y), unit */, std::span<const float> vs, int segments) {
    const auto columns = static_cast<std::uint32_t>(segments) + 1;
    std::vector<float> cosines(segments), sines(segments);
    for (int j = 0; j < segments; ++j) {
        const float phi =
            2.f * std::numbers::pi_v<float> * static_cast<float>(j) / static_cast<float>(segments);
        cosines[j] = j == 0 ? 1.f : std::cos(phi);
        sines[j] = j == 0 ? 0.f : std::sin(phi);
    }
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t i = 0; i < radii.size(); ++i) {
        for (std::uint32_t j = 0; j < columns; ++j) {
            const float c = cosines[j % segments];
            const float s = sines[j % segments];
            addVertex(mesh, {radii[i] * c, ys[i], radii[i] * s},
                      {normals[i].x * c, normals[i].y, normals[i].x * s},
                      static_cast<float>(j) / static_cast<float>(segments), vs[i]);
        }
    }
    for (std::uint32_t i = 0; i + 1 < radii.size(); ++i) {
        for (std::uint32_t j = 0; j < static_cast<std::uint32_t>(segments); ++j) {
            const std::uint32_t a = first + i * columns + j; // this point, this column
            const std::uint32_t b = a + 1;                   // this point, next column
            const std::uint32_t d = a + columns;             // next point, this column
            const std::uint32_t c = d + 1;                   // next point, next column
            if (radii[i + 1] != 0.f)
                addTriangle(mesh, a, d, c);
            if (radii[i] != 0.f)
                addTriangle(mesh, a, c, b);
        }
    }
}

} // namespace

rt::Mesh box(Vec3 halfExtents) {
    require(halfExtents.x > 0 && halfExtents.y > 0 && halfExtents.z > 0,
            "box: half extents must be positive");
    rt::Mesh mesh;
    for (const Face& face : kFaces) {
        const Vec3 centre = mul(face.normal, halfExtents);
        const Vec3 u = mul(face.u, halfExtents);
        const Vec3 v = mul(face.v, halfExtents);
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        addVertex(mesh, centre - u - v, face.normal, 0.f, 1.f);
        addVertex(mesh, centre + u - v, face.normal, 1.f, 1.f);
        addVertex(mesh, centre + u + v, face.normal, 1.f, 0.f);
        addVertex(mesh, centre - u + v, face.normal, 0.f, 0.f);
        addTriangle(mesh, first, first + 1, first + 2);
        addTriangle(mesh, first, first + 2, first + 3);
    }
    return mesh;
}

rt::Mesh bevelBox(Vec3 halfExtents, float bevel, int segments) {
    require(halfExtents.x > 0 && halfExtents.y > 0 && halfExtents.z > 0,
            "bevelBox: half extents must be positive");
    require(bevel > 0 && bevel < std::min({halfExtents.x, halfExtents.y, halfExtents.z}),
            "bevelBox: bevel must be positive and smaller than every half extent");
    require(segments >= 1, "bevelBox: needs at least one segment");

    // Grid coordinates along one axis of half extent h: the rounded band from -h to -e, one flat span to e,
    // the band from e to h, where e = h - bevel. The band's points sit at bevel * tan(45 degrees * k /
    // segments) past e, so the rounded vertices below are evenly spaced in angle.
    const Vec3 inner{halfExtents.x - bevel, halfExtents.y - bevel, halfExtents.z - bevel};
    std::array<std::vector<float>, 3> grid;
    for (int axis = 0; axis < 3; ++axis) {
        const float h = component(halfExtents, axis);
        const float e = component(inner, axis);
        std::vector<float> band(segments + 1);
        for (int k = 0; k <= segments; ++k) {
            const float angle =
                0.25f * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(segments);
            band[k] = k == segments ? h : e + bevel * std::tan(angle);
        }
        std::vector<float>& values = grid[axis];
        for (int k = segments; k >= 0; --k)
            values.push_back(-band[k]);
        values.insert(values.end(), band.begin(), band.end());
    }

    rt::Mesh mesh;
    for (const Face& face : kFaces) {
        const int axisU = axisOf(face.u);
        const int axisV = axisOf(face.v);
        const float hu = component(halfExtents, axisU);
        const float hv = component(halfExtents, axisV);
        const Vec3 centre = mul(face.normal, halfExtents);
        const std::vector<float>& us = grid[axisU];
        const std::vector<float>& vs = grid[axisV];
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        for (float b : vs) {
            for (float a : us) { // the grid is symmetric, so it serves either direction of the axis
                // The point on the sharp box, pulled onto the rounded box: the nearest point of the inner box
                // plus `bevel` along the direction from it.
                const Vec3 p = centre + face.u * a + face.v * b;
                const Vec3 nearest{std::clamp(p.x, -inner.x, inner.x), std::clamp(p.y, -inner.y, inner.y),
                                   std::clamp(p.z, -inner.z, inner.z)};
                const Vec3 n = normalize(p - nearest);
                addVertex(mesh, nearest + n * bevel, n, 0.5f * (a / hu + 1.f), 0.5f * (1.f - b / hv));
            }
        }
        // `a` runs along face.u and `b` along face.v, so each cell's corners in that order are
        // counter-clockwise.
        const auto row = static_cast<std::uint32_t>(us.size());
        for (std::uint32_t j = 0; j + 1 < vs.size(); ++j) {
            for (std::uint32_t i = 0; i + 1 < row; ++i) {
                const std::uint32_t c00 = first + j * row + i;
                const std::uint32_t c10 = c00 + 1;
                const std::uint32_t c01 = c00 + row;
                const std::uint32_t c11 = c01 + 1;
                addTriangle(mesh, c00, c10, c11);
                addTriangle(mesh, c00, c11, c01);
            }
        }
    }
    return mesh;
}

rt::Mesh cylinder(float radius, float height, int segments, bool caps) {
    require(radius > 0 && height > 0, "cylinder: radius and height must be positive");
    require(segments >= 3, "cylinder: needs at least three segments");
    const float y0 = -0.5f * height;
    const float y1 = 0.5f * height;
    const std::array<float, 2> radii{radius, radius};
    const std::array<float, 2> ys{y0, y1};
    const std::array<Vec2, 2> normals{{{1.f, 0.f}, {1.f, 0.f}}};
    const std::array<float, 2> vs{1.f, 0.f};
    rt::Mesh mesh;
    revolve(mesh, radii, ys, normals, vs, segments);
    if (caps) {
        // The cap rings repeat the side's rim positions exactly (the same cosines and sines), with flat
        // normals.
        for (const float y : {y0, y1}) {
            const Vec3 n{0.f, y > 0 ? 1.f : -1.f, 0.f};
            const std::uint32_t centre = addVertex(mesh, {0.f, y, 0.f}, n, 0.5f, 0.5f);
            for (int j = 0; j < segments; ++j) {
                const auto& rim = mesh.vertices[(y > 0 ? segments + 1 : 0) + j].position;
                addVertex(mesh, {rim[0], rim[1], rim[2]}, n, 0.5f + 0.5f * rim[0] / radius,
                          0.5f + 0.5f * rim[2] / radius);
            }
            for (std::uint32_t j = 0; j < static_cast<std::uint32_t>(segments); ++j) {
                const std::uint32_t a = centre + 1 + j;
                const std::uint32_t b = centre + 1 + (j + 1) % static_cast<std::uint32_t>(segments);
                if (y > 0)
                    addTriangle(mesh, centre, b, a);
                else
                    addTriangle(mesh, centre, a, b);
            }
        }
    }
    return mesh;
}

rt::Mesh lathe(std::span<const Vec2> profile, int segments) {
    require(profile.size() >= 2, "lathe: the profile needs at least two points");
    require(segments >= 3, "lathe: needs at least three segments");
    bool offAxis = false;
    for (std::size_t i = 0; i < profile.size(); ++i) {
        require(profile[i].x >= 0 && std::isfinite(profile[i].x) && std::isfinite(profile[i].y),
                "lathe: profile radii must be finite and not negative");
        offAxis |= profile[i].x > 0;
        if (i > 0)
            require(profile[i].x != profile[i - 1].x || profile[i].y != profile[i - 1].y,
                    "lathe: consecutive profile points must differ");
    }
    require(offAxis, "lathe: the profile lies on the axis");

    const std::size_t count = profile.size();
    std::vector<float> radii(count), ys(count), vs(count), lengths(count, 0.f);
    std::vector<Vec2> normals(count);
    for (std::size_t i = 1; i < count; ++i)
        lengths[i] =
            lengths[i - 1] + std::hypot(profile[i].x - profile[i - 1].x, profile[i].y - profile[i - 1].y);
    for (std::size_t i = 0; i < count; ++i) {
        radii[i] = profile[i].x;
        ys[i] = profile[i].y;
        vs[i] = 1.f - lengths[i] / lengths[count - 1];
        // The profile's tangent by central differences (one-sided at the ends), turned to point outwards.
        const Vec2 prev = profile[i == 0 ? 0 : i - 1];
        const Vec2 next = profile[i + 1 == count ? i : i + 1];
        const float tr = next.x - prev.x;
        const float ty = next.y - prev.y;
        const float len = std::hypot(tr, ty);
        normals[i] = {ty / len, -tr / len};
    }
    rt::Mesh mesh;
    revolve(mesh, radii, ys, normals, vs, segments);
    return mesh;
}

rt::Mesh uvSphere(float radius, int rings, int segments) {
    require(radius > 0, "uvSphere: radius must be positive");
    require(rings >= 2 && segments >= 3, "uvSphere: needs at least two rings and three segments");
    std::vector<float> radii(rings + 1), ys(rings + 1), vs(rings + 1);
    std::vector<Vec2> normals(rings + 1);
    for (int i = 0; i <= rings; ++i) {
        const float theta = std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(rings);
        // The poles exactly on the axis, so their triangles meet in one point.
        const float s = i == 0 || i == rings ? 0.f : std::sin(theta);
        const float c = i == 0 ? 1.f : i == rings ? -1.f : std::cos(theta);
        normals[i] = {s, -c};
        radii[i] = radius * s;
        ys[i] = -radius * c;
        vs[i] = 1.f - static_cast<float>(i) / static_cast<float>(rings);
    }
    rt::Mesh mesh;
    revolve(mesh, radii, ys, normals, vs, segments);
    return mesh;
}

rt::Mesh quad(float width, float height) {
    require(width > 0 && height > 0, "quad: width and height must be positive");
    const float x = 0.5f * width;
    const float y = 0.5f * height;
    const Vec3 n{0.f, 0.f, 1.f};
    rt::Mesh mesh;
    addVertex(mesh, {-x, -y, 0.f}, n, 0.f, 1.f);
    addVertex(mesh, {x, -y, 0.f}, n, 1.f, 1.f);
    addVertex(mesh, {x, y, 0.f}, n, 1.f, 0.f);
    addVertex(mesh, {-x, y, 0.f}, n, 0.f, 0.f);
    addTriangle(mesh, 0, 1, 2);
    addTriangle(mesh, 0, 2, 3);
    return mesh;
}

} // namespace evo::modern
