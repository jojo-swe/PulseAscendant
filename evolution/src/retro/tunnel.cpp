// TUNNEL's 1993 side: a dot-ring tunnel that turns into a solid flat-shaded tube. See retro/tunnel.hpp for
// the geometry; everything here is computed in closed form from the retro frame.

#include "retro/tunnel.hpp"
#include "retro/vec3.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

namespace evo::retro {

namespace {

constexpr double kTau = 2 * std::numbers::pi;

double sumOfSines(const std::array<TunnelWave, 2>& waves, double w) {
    double sum = 0;
    for (const TunnelWave& wave : waves)
        sum += wave.amplitude * std::sin(wave.frequency * w + wave.phase);
    return sum;
}

double sumOfSlopes(const std::array<TunnelWave, 2>& waves, double w) {
    double sum = 0;
    for (const TunnelWave& wave : waves)
        sum += wave.amplitude * wave.frequency * std::cos(wave.frequency * w + wave.phase);
    return sum;
}

// The tube's light (towards it, view space): from the lower left, so the walls of the upper right face it.
const Vec3 kTubeLight = normalize({-0.5, -0.8, -0.3});

// Vertex k of tube ring m (at world depth m * kTunnelBandLength) in view space.
Vec3 tubeVertex(std::uint32_t retroFrame, double s, std::int64_t m, std::size_t k) {
    const double w = static_cast<double>(m) * kTunnelBandLength;
    const double z = w - s;
    const std::array<double, 2> c = tunnelRingCentre(s, z);
    const double a = kTau * static_cast<double>(k) / kTunnelSegments + tunnelTwist(retroFrame, w);
    return {c[0] + kTunnelRadius * std::cos(a), c[1] + kTunnelRadius * std::sin(a), z};
}

// A palette ramp through keys (entry, r, g, b), linear between them.
struct Key {
    int entry, r, g, b;
};

template <std::size_t N> void keyedRamp(Palette& palette, std::size_t first, const std::array<Key, N>& keys) {
    for (std::size_t k = 0; k + 1 < N; ++k)
        for (int i = keys[k].entry; i <= keys[k + 1].entry; ++i) {
            const double t = static_cast<double>(i - keys[k].entry) / (keys[k + 1].entry - keys[k].entry);
            const auto channel = [t](int from, int to) {
                return static_cast<std::uint8_t>(std::lround(from + t * (to - from)));
            };
            palette[first + static_cast<std::size_t>(i)] =
                rgba(channel(keys[k].r, keys[k + 1].r), channel(keys[k].g, keys[k + 1].g),
                     channel(keys[k].b, keys[k + 1].b));
        }
}

} // namespace

std::array<double, 2> tunnelPath(double w) {
    return {sumOfSines(kTunnelPathX, w), sumOfSines(kTunnelPathY, w)};
}

std::array<double, 2> tunnelPathSlope(double w) {
    return {sumOfSlopes(kTunnelPathX, w), sumOfSlopes(kTunnelPathY, w)};
}

double tunnelTravel(std::uint32_t retroFrame) {
    return kTunnelSpeed * retroFrame / kTunnelRetroRate;
}

std::array<double, 2> tunnelRingCentre(double s, double z) {
    const std::array<double, 2> distant = tunnelPath(s + z), here = tunnelPath(s),
                                tangent = tunnelPathSlope(s);
    return {distant[0] - here[0] - z * tangent[0], distant[1] - here[1] - z * tangent[1]};
}

double tunnelTwist(std::uint32_t retroFrame, double w) {
    return kTunnelTwistRate * retroFrame / kTunnelRetroRate + kTunnelTwistPerDepth * w;
}

std::vector<TunnelRing> tunnelRings(std::uint32_t retroFrame) {
    const double s = tunnelTravel(retroFrame);
    // The nearest ring on the grid beyond kTunnelNear, then 95 more.
    const auto nearest = static_cast<std::int64_t>(std::floor((s + kTunnelNear) / kTunnelRingSpacing)) + 1;
    std::vector<TunnelRing> rings;
    rings.reserve(kTunnelRings);
    for (auto j = nearest + static_cast<std::int64_t>(kTunnelRings) - 1; j >= nearest; --j) {
        TunnelRing ring{};
        ring.w = static_cast<double>(j) * kTunnelRingSpacing;
        ring.z = ring.w - s;
        ring.centre = tunnelRingCentre(s, ring.z);
        rings.push_back(ring);
    }
    return rings;
}

std::uint32_t tunnelTubeFrame(const show::TimelineFrame& frame) {
    return static_cast<std::uint32_t>(std::floor(frame.chapterDuration * kTunnelRetroRate / 2));
}

double tunnelTubeFront(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::uint32_t first = tunnelTubeFrame(frame);
    if (retroFrame < first)
        return std::numeric_limits<double>::infinity();
    const double u = (retroFrame - first) / (kTunnelTubeSweepSeconds * kTunnelRetroRate);
    if (u >= 1)
        return kTunnelTubeNear;
    const double eased = u * u * (3 - 2 * u);
    return kTunnelFar - (kTunnelFar - kTunnelTubeNear) * eased;
}

std::vector<TunnelFace> tunnelTubeFaces(const show::TimelineFrame& frame, std::uint32_t retroFrame,
                                        bool cull) {
    std::vector<TunnelFace> faces;
    const double front = tunnelTubeFront(frame, retroFrame);
    if (front > kTunnelTubeFar)
        return faces;
    const double s = tunnelTravel(retroFrame);
    const double nearest = std::max(front, kTunnelTubeNear);
    // Bands m (between tube rings m and m + 1) from the last that ends within kTunnelTubeFar, far to near,
    // down to the last that starts at or beyond the front.
    const auto last = static_cast<std::int64_t>(std::floor((s + kTunnelTubeFar) / kTunnelBandLength)) - 1;
    const auto inFront = [&](std::int64_t m) {
        return static_cast<double>(m) * kTunnelBandLength - s >= nearest;
    };
    if (!inFront(last))
        return faces;
    std::int64_t first = last;
    while (inFront(first - 1))
        --first;

    // The mouth: the near ring of the nearest band, on the screen (a convex polygon). A ray through it enters
    // the tube there, so it meets the inside of a wall before the outside of any; a face seen from outside
    // whose three corners project inside the mouth is hidden, and culled.
    const Camera camera = tunnelCamera();
    std::array<std::array<double, 2>, kTunnelSegments> mouth{};
    for (std::size_t k = 0; k < kTunnelSegments; ++k) {
        const Vec3 v = tubeVertex(retroFrame, s, first, k);
        const double world[3] = {v[0], v[1], v[2]};
        const Projected p = project(camera, world);
        mouth[k] = {p.x, p.y};
    }
    const auto inMouth = [&](const Vec3& v) {
        const double world[3] = {v[0], v[1], v[2]};
        const Projected p = project(camera, world);
        int sign = 0;
        for (std::size_t k = 0; k < kTunnelSegments; ++k) {
            const auto& a = mouth[k];
            const auto& b = mouth[(k + 1) % kTunnelSegments];
            const double side = (b[0] - a[0]) * (p.y - a[1]) - (b[1] - a[1]) * (p.x - a[0]);
            const int here = side > 0 ? 1 : side < 0 ? -1 : 0;
            if (here != 0 && sign != 0 && here != sign)
                return false;
            if (here != 0)
                sign = here;
        }
        return true;
    };

    std::vector<TunnelFace> outside; // a band's faces seen from outside, drawn after its inner faces
    for (std::int64_t m = last; m >= first; --m) {
        const double zMiddle = (static_cast<double>(m) + 0.5) * kTunnelBandLength - s;
        // Every band drawn lies between kTunnelTubeNear and kTunnelTubeFar, so the clamp is only defensive.
        const double fade =
            std::clamp((kTunnelTubeFar - zMiddle) / (kTunnelTubeFar - kTunnelTubeNear), 0.0, 1.0);
        const std::array<double, 2> axisNear = tunnelRingCentre(s, zMiddle - kTunnelBandLength / 2);
        const std::array<double, 2> axisFar = tunnelRingCentre(s, zMiddle + kTunnelBandLength / 2);
        const Vec3 axis = {(axisNear[0] + axisFar[0]) / 2, (axisNear[1] + axisFar[1]) / 2, zMiddle};
        outside.clear();
        for (std::size_t k = 0; k < kTunnelSegments; ++k) {
            const std::size_t next = (k + 1) % kTunnelSegments;
            const Vec3 v00 = tubeVertex(retroFrame, s, m, k), v01 = tubeVertex(retroFrame, s, m, next);
            const Vec3 v10 = tubeVertex(retroFrame, s, m + 1, k),
                       v11 = tubeVertex(retroFrame, s, m + 1, next);
            // The quad's normal from its diagonals, turned to face the tube's axis; flat shaded by depth and
            // by the light on the side the camera sees, in the checker's ramp.
            Vec3 normal = normalize(cross(sub(v11, v00), sub(v01, v10)));
            const Vec3 middle = {(v00[0] + v01[0] + v10[0] + v11[0]) / 4,
                                 (v00[1] + v01[1] + v10[1] + v11[1]) / 4, zMiddle};
            if (dot(normal, sub(axis, middle)) < 0)
                normal = {-normal[0], -normal[1], -normal[2]};
            const std::uint8_t ramp =
                (m + static_cast<std::int64_t>(k)) % 2 == 0 ? kTunnelTubeA : kTunnelTubeB;
            const auto indexFor = [&](double facing) { // facing: +1 the inner side, -1 the outer
                const double light = 0.35 + 0.65 * std::max(0.0, facing * dot(normal, kTubeLight));
                const int shade =
                    std::min(kTunnelTubeShades - 1, static_cast<int>(fade * light * kTunnelTubeShades));
                return static_cast<std::uint8_t>(ramp + shade);
            };

            const Vec3* triangles[2][3] = {{&v00, &v10, &v11}, {&v00, &v11, &v01}};
            for (const auto& triangle : triangles) {
                const Vec3& a = *triangle[0];
                const Vec3& b = *triangle[1];
                const Vec3& c = *triangle[2];
                Vec3 n = cross(sub(b, a), sub(c, a));
                if (dot(n, sub(axis, a)) < 0)
                    n = {-n[0], -n[1], -n[2]};
                const bool outer = dot(n, Vec3{-a[0], -a[1], -a[2]}) <= 0; // the camera sees its outside
                if (outer && cull && inMouth(a) && inMouth(b) && inMouth(c))
                    continue;
                TunnelFace face{};
                for (int v = 0; v < 3; ++v)
                    for (int i = 0; i < 3; ++i)
                        face.world[v][i] = (*triangle[v])[i];
                face.index = indexFor(outer ? -1 : 1);
                (outer ? outside : faces).push_back(face);
            }
        }
        // A band is convex: a ray that crosses both its outside and its inside meets the outside first.
        faces.insert(faces.end(), outside.begin(), outside.end());
    }
    return faces;
}

Camera tunnelCamera() {
    return Camera{{0, 0, 0}, {0, 0, 1}, {0, 1, 0}, kTunnelFovY};
}

void tunnelPalette(Palette& palette, const show::TimelineFrame&, std::uint32_t) {
    palette.fill(rgba(0, 0, 0));
    // The dots, far to near: near black, violet, pink, warm white.
    keyedRamp(
        palette, kTunnelDotRamp,
        std::array<Key, 4>{{{0, 6, 0, 16}, {24, 60, 14, 110}, {44, 190, 60, 170}, {63, 255, 236, 210}}});
    // The tube's checker, far to near: blue, and violet.
    keyedRamp(palette, kTunnelTubeA,
              std::array<Key, 3>{{{0, 2, 4, 14}, {16, 30, 80, 190}, {31, 170, 225, 255}}});
    keyedRamp(palette, kTunnelTubeB,
              std::array<Key, 3>{{{0, 8, 2, 14}, {16, 110, 40, 180}, {31, 240, 180, 255}}});
    palette[kTunnelWhite] = rgba(255, 255, 255);
}

void tunnelScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const Camera camera = tunnelCamera();
    for (const TunnelFace& face : tunnelTubeFaces(frame, retroFrame)) {
        const std::uint8_t indices[3] = {face.index, face.index, face.index};
        addTriangle(scene, camera, face.world, indices, kFlat);
    }

    const double front = tunnelTubeFront(frame, retroFrame);
    for (const TunnelRing& ring : tunnelRings(retroFrame)) {
        if (ring.z >= front)
            continue;
        const double fade = std::clamp((kTunnelFar - ring.z) / (kTunnelFar - kTunnelNear), 0.0, 1.0);
        const auto index = static_cast<std::uint8_t>(
            kTunnelDotRamp + std::lround((kTunnelDotShades - 1) * std::pow(fade, 1.5)));
        const std::uint8_t size = ring.z < kTunnelDotsLarge ? 3 : ring.z < kTunnelDotsMedium ? 2 : 1;
        const double twist = tunnelTwist(retroFrame, ring.w);
        for (std::size_t i = 0; i < kTunnelRingDots; ++i) {
            const double a = kTau * static_cast<double>(i) / kTunnelRingDots + twist;
            const double world[3] = {ring.centre[0] + kTunnelRadius * std::cos(a),
                                     ring.centre[1] + kTunnelRadius * std::sin(a), ring.z};
            addPoint(scene, camera, world, index, size);
        }
    }
}

} // namespace evo::retro
