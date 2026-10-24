#include "retro/city.hpp"

#include "retro/math.hpp"
#include "retro/vec3.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace evo::retro {

namespace {

// The integral of smoothstep from 0 to u (u clamped to [0, 1]).
double smoothstepIntegral(double u) {
    u = std::clamp(u, 0.0, 1.0);
    return u * u * u - u * u * u * u / 2;
}

double accentOf(const show::TimelineFrame& frame) {
    return std::clamp(frame.accentSeconds, kCityMinAccent, kCityMaxAccent);
}

// The heading (radians, 0 down +z, positive to the right) at t seconds: the yaw rate eases up from
// kCityTurn[0] A to kCityTurn[1] A, holds to A and eases out over kCityTurnEase; integrated in closed form.
double yawAt(double t, double accent) {
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

double yawRateAt(double t, double accent) {
    const double t0 = kCityTurn[0] * accent, t1 = kCityTurn[1] * accent;
    if (t <= accent)
        return radians(kCityTurnRate) * smoothstep((t - t0) / (t1 - t0));
    return radians(kCityTurnRate) * (1 - smoothstep((t - accent) / kCityTurnEase));
}

// Where the camera is on the ground plane at t: straight down the main street until the turn starts, then
// the heading integrated by Simpson's rule in steps of at most 1/200 s (a pure function of t).
std::array<double, 2> groundAt(double t, double accent) {
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

using Rgb = std::array<double, 3>;
struct Stop {
    double at;
    Rgb colour;
};

Rgb mix(const Rgb& a, const Rgb& b, double t) {
    return {a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), a[2] + t * (b[2] - a[2])};
}

// Piecewise linear through the stops (ascending `at`), held beyond the ends.
Rgb gradient(std::initializer_list<Stop> stops, double x) {
    const Stop* previous = stops.begin();
    if (x <= previous->at)
        return previous->colour;
    for (const Stop* s = stops.begin() + 1; s != stops.end(); ++s) {
        if (x <= s->at)
            return mix(previous->colour, s->colour, (x - previous->at) / (s->at - previous->at));
        previous = s;
    }
    return previous->colour;
}

std::uint32_t toRgba(const Rgb& c) {
    const auto channel = [&](int k) {
        return static_cast<std::uint8_t>(std::clamp(std::lround(c[static_cast<std::size_t>(k)]), 0L, 255L));
    };
    return rgba(channel(0), channel(1), channel(2));
}

constexpr Rgb kHaze = {232, 118, 96};
constexpr double kDomeRadius = 2000, kSunDistance = 1800;
constexpr double kSunElevation = 3.5, kSunRadius = 9; // degrees
// The dome's rows (elevation in degrees) and its segments' width (degrees of azimuth).
constexpr double kDomeRows[] = {-3, 0, 2, 5, 9, 15, 24, 36, 52, 70, 90};
constexpr int kDomeSegments = 24;
// The sun's slices, bottom to top, in units of its radius (the horizon cuts it at -kSunElevation /
// kSunRadius): the gaps between them narrow upwards.
constexpr std::array<std::array<double, 2>, 4> kSunSlices = {
    {{-0.39, -0.30}, {-0.24, -0.12}, {-0.07, 0.08}, {0.12, 1.0}}};
// Lit windows, on walls whose middle is within kWindowDistance; from kWindowFade on they shrink about their
// centres and fade into the haze, gone at kWindowDistance, so they never pop in or out.
constexpr double kWindowDistance = 90, kWindowFade = 70, kWindowWidth = 1.3, kWindowHeight = 1.8,
                 kWindowOut = 0.08;
constexpr int kWindowsPerWall = 10;

// Sky ramp position (0 horizon haze .. 1 zenith) of an elevation in degrees.
double skyAt(double elevation) {
    return std::sqrt(std::clamp((elevation + 3) / 93, 0.0, 1.0));
}

// The camera's view basis (addTriangle's, from viewBasis) and the frustum's half-space slopes, the screen's
// half extents over the focal lengths, widened a little: a primitive wholly outside one of them would come
// out of addTriangle empty, so it is left out early.
struct Frustum {
    Vec3 eye, right, up, forward;
    double kx, ky;

    explicit Frustum(const Camera& camera) {
        const ViewBasis view = viewBasis(camera);
        eye = view.eye, right = view.right, up = view.up, forward = view.forward;
        kx = 1.05 * (kScreenWidth / 2.0) / view.focalX;
        ky = 1.05 * (kScreenHeight / 2.0) / view.focalY;
    }

    template <typename Points> bool hidden(const Points& points) const {
        bool behind = true, left = true, rightOut = true, above = true, below = true;
        for (const Vec3& p : points) {
            const Vec3 d = sub(p, eye);
            const double x = dot(d, right), y = dot(d, up), z = dot(d, forward);
            behind = behind && z < kNearPlane;
            rightOut = rightOut && x > kx * z;
            left = left && -x > kx * z;
            above = above && y > ky * z;
            below = below && -y > ky * z;
        }
        return behind || left || rightOut || above || below;
    }
};

// Builds the faces of one frame.
class Builder {
public:
    Builder(const Camera& camera, std::vector<CityFace>& out) : frustum_(camera), out_(out) {}

    double haze(const Vec3& p) const {
        const double d = std::sqrt(dot(sub(p, frustum_.eye), sub(p, frustum_.eye)));
        return smoothstep((d - kCityHazeNear) / (kCityDrawDistance - kCityHazeNear));
    }
    const Vec3& eye() const { return frustum_.eye; }

    void triangle(const Vec3& a, const Vec3& b, const Vec3& c, std::uint8_t ia, std::uint8_t ib,
                  std::uint8_t ic, std::uint8_t mode) {
        CityFace face{};
        const Vec3* corners[3] = {&a, &b, &c};
        for (int v = 0; v < 3; ++v)
            for (int k = 0; k < 3; ++k)
                face.world[v][k] = (*corners[v])[static_cast<std::size_t>(k)];
        face.index[0] = ia, face.index[1] = ib, face.index[2] = ic;
        face.mode = mode;
        out_.push_back(face);
    }

    // A planar convex quad a b c d as two triangles, unless the frustum cannot see it.
    void quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, std::array<std::uint8_t, 4> index,
              std::uint8_t mode) {
        if (hidden({a, b, c, d}))
            return;
        triangle(a, b, c, index[0], index[1], index[2], mode);
        triangle(a, c, d, index[0], index[2], index[3], mode);
    }

    bool hidden(std::initializer_list<Vec3> points) const { return frustum_.hidden(points); }
    bool hidden(const std::vector<Vec3>& points) const { return frustum_.hidden(points); }

private:
    Frustum frustum_;
    std::vector<CityFace>& out_;
};

std::uint8_t rampIndex(std::uint8_t base, int shades, double s) {
    return static_cast<std::uint8_t>(base + std::lround(std::clamp(s, 0.0, 1.0) * (shades - 1)));
}

void addSky(Builder& b) {
    const Vec3& eye = b.eye();
    const auto at = [&](double elevation, double azimuth) {
        const double e = radians(elevation), a = radians(azimuth);
        return Vec3{eye[0] + kDomeRadius * std::cos(e) * std::sin(a), eye[1] + kDomeRadius * std::sin(e),
                    eye[2] + kDomeRadius * std::cos(e) * std::cos(a)};
    };
    const auto index = [](double elevation) { return rampIndex(kCitySky, kCitySkyShades, skyAt(elevation)); };
    constexpr std::size_t rows = std::size(kDomeRows);
    for (int s = 0; s < kDomeSegments; ++s) {
        const double a0 = 360.0 * s / kDomeSegments, a1 = 360.0 * (s + 1) / kDomeSegments;
        for (std::size_t r = 0; r + 1 < rows; ++r) {
            const double e0 = kDomeRows[r], e1 = kDomeRows[r + 1];
            const std::uint8_t i0 = index(e0), i1 = index(e1);
            if (r + 2 == rows) { // the cap: one triangle to the zenith
                const Vec3 p = at(e0, a0), q = at(e0, a1), top = at(e1, 0);
                if (!b.hidden({p, q, top}))
                    b.triangle(p, q, top, i0, i0, i1, kGouraud);
                continue;
            }
            b.quad(at(e0, a0), at(e0, a1), at(e1, a1), at(e1, a0), {i0, i0, i1, i1}, kGouraud);
        }
    }
}

void addSun(Builder& b) {
    const Vec3& eye = b.eye();
    const double e = radians(kSunElevation), r = std::tan(radians(kSunRadius));
    const Vec3 centre = {0, std::sin(e), std::cos(e)}, up = {0, std::cos(e), -std::sin(e)};
    const auto at = [&](double x, double v) {
        return Vec3{eye[0] + kSunDistance * (centre[0] + r * x),
                    eye[1] + kSunDistance * (centre[1] + r * v * up[1]),
                    eye[2] + kSunDistance * (centre[2] + r * v * up[2])};
    };
    const double bottom = kSunSlices.front()[0];
    const auto index = [&](double v) {
        return rampIndex(kCitySun, kCitySunShades, (v - bottom) / (1 - bottom));
    };
    for (const auto& slice : kSunSlices) {
        // The slice's outline: up the right edge of the disc, down the left, sampled every 1/8 of the radius.
        std::vector<double> vs = {slice[0]};
        for (double v = std::ceil(slice[0] * 8) / 8; v < slice[1]; v += 0.125)
            if (v > slice[0])
                vs.push_back(v);
        vs.push_back(slice[1]);
        std::vector<Vec3> outline;
        std::vector<std::uint8_t> shade;
        for (double v : vs) {
            outline.push_back(at(std::sqrt(std::max(0.0, 1 - v * v)), v));
            shade.push_back(index(v));
        }
        for (auto v = vs.rbegin(); v != vs.rend(); ++v) {
            const double x = std::sqrt(std::max(0.0, 1 - *v * *v));
            if (x == 0) // the top of the disc: one point
                continue;
            outline.push_back(at(-x, *v));
            shade.push_back(index(*v));
        }
        if (b.hidden(outline))
            continue;
        for (std::size_t k = 1; k + 1 < outline.size(); ++k)
            b.triangle(outline[0], outline[k], outline[k + 1], shade[0], shade[k], shade[k + 1], kGouraud);
    }
}

// A ground tile at y = 0: x0..x1, z0..z1, in a ramp of kCityGroundShades running into the haze.
void groundTile(Builder& b, double x0, double x1, double z0, double z1, std::uint8_t ramp) {
    const Vec3 p[4] = {{x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1}, {x0, 0, z1}};
    std::array<std::uint8_t, 4> index{};
    for (int k = 0; k < 4; ++k)
        index[static_cast<std::size_t>(k)] = rampIndex(ramp, kCityGroundShades, b.haze(p[k]));
    b.quad(p[0], p[1], p[2], p[3], index, kGouraud);
}

// The ground of cell (i, m): its lot, the street along z at its lower x, the street along x at its lower
// z, and their crossing.
void addGround(Builder& b, std::int32_t i, std::int32_t m) {
    const double x = kCityCell * i, z = kCityCell * m, h = kCityStreet / 2;
    groundTile(b, x + h, x + kCityCell - h, z + h, z + kCityCell - h, kCityLot);
    groundTile(b, x - h, x + h, z + h, z + kCityCell - h, kCityRoad);
    groundTile(b, x + h, x + kCityCell - h, z - h, z + h, kCityRoad);
    groundTile(b, x - h, x + h, z - h, z + h, kCityRoad);
}

// The main street's centre line: dashes 3.5 long every 9 units whose middle lies within kReach of the eye
// on the ground, not on the crossings; just above the road. From kFade on they narrow and fade into the
// haze, gone at kReach, so they never pop in or out.
void addMarkings(Builder& b) {
    constexpr double kLift = 0.02, kHalfWidth = 0.2, kDash = 3.5, kPitch = 9, kReach = 120, kFade = 90;
    const Vec3& eye = b.eye();
    if (std::abs(eye[0]) > kReach)
        return;
    for (double k = std::floor((eye[2] - kReach) / kPitch); k * kPitch <= eye[2] + kReach; ++k) {
        const double z0 = k * kPitch, z1 = z0 + kDash;
        const double local = z0 - kCityCell * std::floor(z0 / kCityCell); // 0..42 from a crossing's middle
        if (local < kCityStreet / 2 || local + kDash > kCityCell - kCityStreet / 2)
            continue;
        const double reach = std::hypot(eye[0], (z0 + z1) / 2 - eye[2]);
        if (reach >= kReach)
            continue;
        const double fade = smoothstep((reach - kFade) / (kReach - kFade));
        const double w = kHalfWidth * (1 - fade);
        const Vec3 p[4] = {{-w, kLift, z0}, {w, kLift, z0}, {w, kLift, z1}, {-w, kLift, z1}};
        const std::uint8_t index =
            rampIndex(kCityMarking, kCityMarkingShades, std::max(b.haze({0, 0, (z0 + z1) / 2}), fade));
        b.quad(p[0], p[1], p[2], p[3], {index, index, index, index}, kFlat);
    }
}

// A box's shade (0..1 along its ramp) at a vertex: the face's light, darker towards the street, then into
// the haze with distance.
double wallShade(const Builder& b, double light, const Vec3& p) {
    const double lit = light * (0.3 + 0.7 * std::min(1.0, p[1] / 32));
    return lit + (kCityRampHaze - lit) * b.haze(p);
}

// The four walls of a box: outward normal, then the corners bottom-left, bottom-right, top-right, top-left
// seen from outside, and the light the face gets from the low sun down +z.
struct Wall {
    Vec3 normal;
    double light;
};
constexpr Wall kWalls[4] = {{{0, 0, -1}, 0.42}, {{1, 0, 0}, 0.60}, {{0, 0, 1}, 0.98}, {{-1, 0, 0}, 0.52}};
constexpr double kRoofLight = 0.55;

void addBox(Builder& b, const CityBox& box, std::int32_t i, std::int32_t m, std::uint32_t salt) {
    const Vec3& eye = b.eye();
    const std::uint8_t ramp = box.ramp == 0 ? kCityRampA : kCityRampB;
    for (int w = 0; w < 4; ++w) {
        const Wall& wall = kWalls[w];
        // The wall's bottom edge from left to right seen from outside.
        Vec3 l{}, r{};
        switch (w) {
        case 0:
            l = {box.x0, box.y0, box.z0}, r = {box.x1, box.y0, box.z0};
            break;
        case 1:
            l = {box.x1, box.y0, box.z0}, r = {box.x1, box.y0, box.z1};
            break;
        case 2:
            l = {box.x1, box.y0, box.z1}, r = {box.x0, box.y0, box.z1};
            break;
        default:
            l = {box.x0, box.y0, box.z1}, r = {box.x0, box.y0, box.z0};
            break;
        }
        if (dot(wall.normal, sub(eye, l)) <= 0) // turned away from the eye
            continue;
        const Vec3 lt = {l[0], box.y1, l[2]}, rt = {r[0], box.y1, r[2]};
        if (b.hidden({l, r, rt, lt}))
            continue;
        std::array<std::uint8_t, 4> index{};
        const Vec3 corners[4] = {l, r, rt, lt};
        for (int k = 0; k < 4; ++k)
            index[static_cast<std::size_t>(k)] =
                rampIndex(ramp, kCityRampShades, wallShade(b, wall.light, corners[k]));
        b.quad(l, r, rt, lt, index, kGouraud);

        // Lit windows: a grid of floors and bays, some lit by the hash, just in front of the wall.
        const Vec3 along = normalize(sub(r, l));
        const double width = std::sqrt(dot(sub(r, l), sub(r, l)));
        const Vec3 middle = {(l[0] + r[0]) / 2, (box.y0 + box.y1) / 2, (l[2] + r[2]) / 2};
        const double reach = std::sqrt(dot(sub(middle, eye), sub(middle, eye)));
        if (reach >= kWindowDistance)
            continue;
        const double fade = smoothstep((reach - kWindowFade) / (kWindowDistance - kWindowFade));
        const double halfW = kWindowWidth / 2 * (1 - fade), halfH = kWindowHeight / 2 * (1 - fade);
        int lit = 0;
        for (int floor = 0; lit < kWindowsPerWall; ++floor) {
            const double y0 = box.y0 + 2.2 + 3.4 * floor, y1 = y0 + kWindowHeight;
            if (y1 > box.y1 - 1.2)
                break;
            for (int bay = 0; lit < kWindowsPerWall; ++bay) {
                const double u0 = 1.4 + 2.8 * bay, u1 = u0 + kWindowWidth;
                if (u1 > width - 1.4)
                    break;
                const std::uint32_t h =
                    cityHash(i, m, salt + static_cast<std::uint32_t>(((w * 32 + floor) * 32 + bay) * 7 + 1));
                if (h % 100 >= 16)
                    continue;
                ++lit;
                const auto at = [&](double u, double y) {
                    return Vec3{l[0] + along[0] * u + wall.normal[0] * kWindowOut, y,
                                l[2] + along[2] * u + wall.normal[2] * kWindowOut};
                };
                const double uc = (u0 + u1) / 2, yc = (y0 + y1) / 2;
                const std::uint8_t windowIndex =
                    rampIndex(kCityWindow, kCityGroundShades, std::max(b.haze(at(uc, yc)), fade));
                b.quad(at(uc - halfW, yc - halfH), at(uc + halfW, yc - halfH), at(uc + halfW, yc + halfH),
                       at(uc - halfW, yc + halfH), {windowIndex, windowIndex, windowIndex, windowIndex},
                       kFlat);
            }
        }
    }
    if (eye[1] > box.y1) {
        const Vec3 p[4] = {{box.x0, box.y1, box.z0},
                           {box.x1, box.y1, box.z0},
                           {box.x1, box.y1, box.z1},
                           {box.x0, box.y1, box.z1}};
        std::array<std::uint8_t, 4> index{};
        for (int k = 0; k < 4; ++k)
            index[static_cast<std::size_t>(k)] =
                rampIndex(ramp, kCityRampShades, wallShade(b, kRoofLight, p[k]));
        b.quad(p[0], p[1], p[2], p[3], index, kGouraud);
    }
}

// Where a coordinate lies on a line of grid cells split at `split` inside [start, end): -1 before, 0 or 1
// inside, 2 after.
int subIndex(double v, double start, double split, double end) {
    return v < start ? -1 : v < split ? 0 : v < end ? 1 : 2;
}

int spanDistance(int at, int from, int to) {
    return at < from ? from - at : at > to ? at - to : 0;
}

void addLot(Builder& b, std::int32_t i, std::int32_t m) {
    const Vec3& eye = b.eye();
    const double x = kCityCell * i, z = kCityCell * m, h = kCityStreet / 2;
    const int ex = subIndex(eye[0], x - h, x + kCityCell / 2, x + kCityCell - h);
    const int ez = subIndex(eye[2], z - h, z + kCityCell / 2, z + kCityCell - h);
    std::vector<CityBuilding> buildings = cityLot(i, m);
    std::vector<std::pair<int, std::size_t>> order;
    for (std::size_t k = 0; k < buildings.size(); ++k) {
        const CityBuilding& bd = buildings[k];
        order.push_back({spanDistance(ex, bd.sx0, bd.sx1) + spanDistance(ez, bd.sz0, bd.sz1), k});
    }
    std::stable_sort(order.begin(), order.end(),
                     [](const auto& a, const auto& c) { return a.first > c.first; });
    for (const auto& [distance, k] : order) {
        const CityBuilding& bd = buildings[k];
        const std::uint32_t salt = 1000 + 1000 * static_cast<std::uint32_t>(k);
        if (bd.tower && eye[1] < bd.base.y1) {
            addBox(b, bd.top, i, m, salt + 500);
            addBox(b, bd.base, i, m, salt);
        } else {
            addBox(b, bd.base, i, m, salt);
            if (bd.tower)
                addBox(b, bd.top, i, m, salt + 500);
        }
    }
}

} // namespace

std::uint32_t cityHash(std::int32_t i, std::int32_t m, std::uint32_t salt) {
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

std::vector<CityBuilding> cityLot(std::int32_t i, std::int32_t m) {
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

CityPose cityPose(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const double t = static_cast<double>(retroFrame) / kCityRetroRate;
    const double accent = accentOf(frame);
    const double climb =
        smoothstep((t - kCityClimb[0] * accent) / ((kCityClimb[1] - kCityClimb[0]) * accent));
    const std::array<double, 2> ground = groundAt(t, accent);
    CityPose pose{};
    pose.x = ground[0];
    pose.z = ground[1];
    pose.y = kCityFlightHeight + (kCityCruiseHeight - kCityFlightHeight) * climb;
    pose.yaw = yawAt(t, accent);
    pose.pitch = radians(kCityFlightPitch + (kCityCruisePitch - kCityFlightPitch) * climb);
    // The bank follows the turn; in the street a gentle sway.
    pose.roll = radians(kCityBank) * yawRateAt(t, accent) / radians(kCityTurnRate) +
                radians(1.5) * std::sin(1.1 * t) * (1 - climb);
    return pose;
}

Camera cityCamera(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const CityPose p = cityPose(frame, retroFrame);
    const Vec3 forward = {std::sin(p.yaw) * std::cos(p.pitch), std::sin(p.pitch),
                          std::cos(p.yaw) * std::cos(p.pitch)};
    const Vec3 right = {std::cos(p.yaw), 0, -std::sin(p.yaw)};
    const Vec3 level = cross(forward, right);
    const double c = std::cos(p.roll), s = std::sin(p.roll);
    Camera camera{};
    camera.eye[0] = p.x, camera.eye[1] = p.y, camera.eye[2] = p.z;
    for (int k = 0; k < 3; ++k) {
        camera.target[k] = camera.eye[k] + forward[static_cast<std::size_t>(k)];
        camera.up[k] = c * level[static_cast<std::size_t>(k)] + s * right[static_cast<std::size_t>(k)];
    }
    camera.fovYDegrees = kCityFovY;
    return camera;
}

std::vector<CityFace> cityFaces(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const Camera camera = cityCamera(frame, retroFrame);
    std::vector<CityFace> faces;
    Builder b(camera, faces);
    addSky(b);
    addSun(b);

    // The cells within the draw distance whose ground or buildings the frustum may see.
    const Vec3& eye = b.eye();
    const double h = kCityStreet / 2, reach = kCityDrawDistance;
    const auto cellOf = [&](double v) { return static_cast<std::int32_t>(std::floor((v + h) / kCityCell)); };
    const std::int32_t ei = cellOf(eye[0]), em = cellOf(eye[2]);
    struct Cell {
        std::int32_t i, m;
        int distance;
    };
    std::vector<Cell> cells;
    for (std::int32_t i = cellOf(eye[0] - reach); i <= cellOf(eye[0] + reach); ++i)
        for (std::int32_t m = cellOf(eye[2] - reach); m <= cellOf(eye[2] + reach); ++m) {
            const double x0 = kCityCell * i - h, x1 = x0 + kCityCell, z0 = kCityCell * m - h,
                         z1 = z0 + kCityCell;
            const double dx = std::max({x0 - eye[0], 0.0, eye[0] - x1}),
                         dz = std::max({z0 - eye[2], 0.0, eye[2] - z1});
            if (dx * dx + dz * dz > reach * reach)
                continue;
            if (b.hidden({{x0, 0, z0},
                          {x1, 0, z0},
                          {x0, 0, z1},
                          {x1, 0, z1},
                          {x0, kCityMaxHeight, z0},
                          {x1, kCityMaxHeight, z0},
                          {x0, kCityMaxHeight, z1},
                          {x1, kCityMaxHeight, z1}}))
                continue;
            cells.push_back({i, m, std::abs(i - ei) + std::abs(m - em)});
        }
    for (const Cell& c : cells)
        addGround(b, c.i, c.m);
    addMarkings(b);
    std::stable_sort(cells.begin(), cells.end(),
                     [](const Cell& a, const Cell& c) { return a.distance > c.distance; });
    for (const Cell& c : cells)
        addLot(b, c.i, c.m);
    return faces;
}

void cityPalette(Palette& palette, const show::TimelineFrame&, std::uint32_t) {
    palette.fill(rgba(0, 0, 0));
    for (int k = 0; k < kCitySkyShades; ++k) {
        // Entry k shows elevation 93 (k / 47)^2 - 3 degrees (skyAt inverted).
        const double q = static_cast<double>(k) / (kCitySkyShades - 1);
        const double elevation = 93 * q * q - 3;
        palette[static_cast<std::size_t>(kCitySky + k)] = toRgba(gradient({{-3, kHaze},
                                                                           {0, {255, 172, 98}},
                                                                           {3, {255, 146, 92}},
                                                                           {8, {242, 110, 106}},
                                                                           {16, {192, 72, 124}},
                                                                           {28, {118, 44, 126}},
                                                                           {45, {60, 28, 104}},
                                                                           {65, {28, 16, 72}},
                                                                           {90, {10, 8, 40}}},
                                                                          elevation));
    }
    for (int k = 0; k < kCitySunShades; ++k)
        palette[static_cast<std::size_t>(kCitySun + k)] = toRgba(gradient(
            {{0, {255, 84, 56}}, {0.5, {255, 170, 80}}, {1, {255, 246, 176}}}, k / (kCitySunShades - 1.0)));
    palette[kCityHaze] = toRgba(kHaze);
    for (int k = 0; k < kCityRampShades; ++k) {
        const double s = static_cast<double>(k) / (kCityRampShades - 1);
        palette[static_cast<std::size_t>(kCityRampA + k)] = toRgba(gradient({{0, {14, 8, 26}},
                                                                             {0.22, {50, 20, 62}},
                                                                             {0.45, {122, 38, 90}},
                                                                             {kCityRampHaze, kHaze},
                                                                             {0.86, {255, 170, 92}},
                                                                             {1, {255, 230, 150}}},
                                                                            s));
        palette[static_cast<std::size_t>(kCityRampB + k)] = toRgba(gradient({{0, {8, 8, 30}},
                                                                             {0.22, {30, 22, 86}},
                                                                             {0.45, {88, 46, 142}},
                                                                             {kCityRampHaze, kHaze},
                                                                             {0.86, {250, 152, 140}},
                                                                             {1, {255, 214, 198}}},
                                                                            s));
    }
    for (int k = 0; k < kCityGroundShades; ++k) {
        const double f = static_cast<double>(k) / (kCityGroundShades - 1);
        palette[static_cast<std::size_t>(kCityRoad + k)] = toRgba(mix({22, 16, 34}, kHaze, f));
        palette[static_cast<std::size_t>(kCityLot + k)] = toRgba(mix({74, 46, 76}, kHaze, f));
        palette[static_cast<std::size_t>(kCityWindow + k)] = toRgba(mix({255, 216, 112}, kHaze, f));
    }
    for (int k = 0; k < kCityMarkingShades; ++k)
        palette[static_cast<std::size_t>(kCityMarking + k)] =
            toRgba(mix({214, 182, 112}, kHaze, static_cast<double>(k) / (kCityMarkingShades - 1)));
}

std::uint8_t cityBackground(const show::TimelineFrame&, std::uint32_t) {
    return kCityHaze;
}

void cityScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const Camera camera = cityCamera(frame, retroFrame);
    for (const CityFace& face : cityFaces(frame, retroFrame))
        addTriangle(scene, camera, face.world, face.index, face.mode);
}

} // namespace evo::retro
