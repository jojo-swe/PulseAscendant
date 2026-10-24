#include "retro/dots.hpp"

#include "text/font8x8.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <utility>
#include <vector>

namespace evo::retro {

namespace {

using Point = std::array<double, 3>;
constexpr double kFovY = 40.0;
constexpr double kTau = 2 * std::numbers::pi;

// --- The shapes ------------------------------------------------------------------------------------------

DotsPoints makeCube() {
    DotsPoints points{};
    const double step = kDotsCubeSide / 7, half = kDotsCubeSide / 2;
    std::size_t i = 0;
    for (int y = 0; y < 8; ++y)
        for (int z = 0; z < 8; ++z)
            for (int x = 0; x < 8; ++x)
                points[i++] = {x * step - half, y * step - half, z * step - half};
    return points;
}

// A Fibonacci spiral: point i at height 1 - (2i + 1) / 512, turned by the golden angle from the last.
DotsPoints makeSphere() {
    DotsPoints points{};
    const double golden = std::numbers::pi * (3 - std::sqrt(5.0));
    for (std::size_t i = 0; i < kDotsPoints; ++i) {
        const double y = 1 - (2.0 * static_cast<double>(i) + 1) / kDotsPoints;
        const double ring = std::sqrt(1 - y * y), angle = golden * static_cast<double>(i);
        points[i] = {kDotsSphereRadius * ring * std::cos(angle), kDotsSphereRadius * y,
                     kDotsSphereRadius * ring * std::sin(angle)};
    }
    return points;
}

DotsPoints makeTorus() {
    DotsPoints points{};
    std::size_t i = 0;
    for (int major = 0; major < 32; ++major)
        for (int minor = 0; minor < 16; ++minor) {
            const double phi = kTau * major / 32, psi = kTau * (minor + 0.5 * (major % 2)) / 16;
            const double radius = kDotsTorusMajor + kDotsTorusMinor * std::cos(psi);
            points[i++] = {radius * std::cos(phi), kDotsTorusMinor * std::sin(psi), radius * std::sin(phi)};
        }
    return points;
}

DotsPoints makeYear() {
    // The set pixels of the four glyphs, left to right and top to bottom, as (column, row).
    std::vector<std::pair<int, int>> pixels;
    for (int row = 0; row < 8; ++row)
        for (int g = 0; g < 4; ++g) {
            const text::GlyphRows& rows = text::glyph(kDotsYearText[g]);
            for (int col = 0; col < 8; ++col)
                if (rows[row] & (0x80u >> col))
                    pixels.push_back({g * 8 + col, row});
        }
    int minCol = 99, maxCol = -1, minRow = 99, maxRow = -1;
    for (const auto& [col, row] : pixels) {
        minCol = std::min(minCol, col), maxCol = std::max(maxCol, col);
        minRow = std::min(minRow, row), maxRow = std::max(maxRow, row);
    }
    const double centreCol = (minCol + maxCol) / 2.0, centreRow = (minRow + maxRow) / 2.0;
    const std::size_t n = pixels.size();

    DotsPoints points{};
    std::size_t i = 0;
    for (std::size_t j = 0; j < n; ++j) {
        const std::size_t count = (j + 1) * kDotsPoints / n - j * kDotsPoints / n;
        const double x = (pixels[j].first - centreCol) * kDotsYearCell;
        const double y = (centreRow - pixels[j].second) * kDotsYearCell;
        for (std::size_t k = 0; k < count; ++k) {
            const double dx = (k % 2 == 0 ? -0.25 : 0.25) * kDotsYearCell;
            const double dy = (k % 4 < 2 ? 0.25 : -0.25) * kDotsYearCell;
            points[i++] = {x + dx, y + dy, -kDotsYearForward + static_cast<double>(k / 4) * kDotsYearLayer};
        }
    }
    return points;
}

// --- Orientation ---------------------------------------------------------------------------------------

// About y by `yaw`, then about x by `pitch`.
Point turn(const Point& p, double yaw, double pitch) {
    const double cy = std::cos(yaw), sy = std::sin(yaw), cx = std::cos(pitch), sx = std::sin(pitch);
    const double x = cy * p[0] + sy * p[2], z = -sy * p[0] + cy * p[2];
    return {x, cx * p[1] - sx * z, sx * p[1] + cx * z};
}

// --- The schedule --------------------------------------------------------------------------------------

constexpr DotsShape kOrder[] = {DotsShape::Cube,  DotsShape::Sphere, DotsShape::Torus, DotsShape::Year,
                                DotsShape::Torus, DotsShape::Sphere, DotsShape::Cube};
constexpr std::size_t kMorphs = std::size(kOrder) - 1;

using Step = DotsStep;
static_assert(kMorphs == 6); // dotsSchedule's array

std::array<Step, kMorphs> schedule(const show::TimelineFrame& frame) {
    const double duration = std::max(frame.chapterDuration, 0.0);
    const double accent = std::clamp(frame.accentSeconds, 0.3 * duration, 0.7 * duration);
    const double after = duration - accent;
    const double arrivals[kMorphs + 1] = {0,
                                          accent / 3,
                                          2 * accent / 3,
                                          accent,
                                          accent + 0.45 * after,
                                          accent + 0.65 * after,
                                          accent + 0.85 * after};
    const auto toFrame = [](double seconds) {
        return static_cast<std::uint32_t>(std::floor(seconds * kDotsRetroRate));
    };
    const auto longest = static_cast<std::uint32_t>(std::lround(kDotsMorphSeconds * kDotsRetroRate));
    std::array<Step, kMorphs> steps{};
    for (std::size_t m = 0; m < kMorphs; ++m) {
        const std::uint32_t from = toFrame(arrivals[m]), to = toFrame(arrivals[m + 1]);
        const std::uint32_t frames = std::max(2u, std::min(longest, (to - from) * 4 / 5));
        // In a short chapter a morph may get fewer frames than it asks for, but never starts before the last
        // one has arrived.
        steps[m] = {std::max(from, to > frames ? to - frames : 0u), to};
    }
    return steps;
}

double smoothstep(double s) {
    return s * s * (3 - 2 * s);
}

// The order in which the points of `world` pair up with another shape's: sorted by height (highest first)
// into 8 bands of 64, each band by angle round the y axis from the camera's side (-z), left to right; ties
// by model index. order[r] is the point of rank r. The keys are doubles from std::sin/atan2, so near-ties may
// break differently on another compiler or library, but always the same way within one build.
std::array<std::size_t, kDotsPoints> pairingOrder(const DotsPoints& world) {
    std::array<std::size_t, kDotsPoints> order{};
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return world[a][1] > world[b][1]; });
    constexpr std::size_t kBand = kDotsPoints / 8;
    for (std::size_t band = 0; band < 8; ++band)
        std::stable_sort(order.begin() + band * kBand, order.begin() + (band + 1) * kBand,
                         [&](std::size_t a, std::size_t b) {
                             return std::atan2(world[a][0], -world[a][2]) <
                                    std::atan2(world[b][0], -world[b][2]);
                         });
    return order;
}

} // namespace

const DotsPoints& dotsModel(DotsShape shape) {
    static const DotsPoints cube = makeCube(), sphere = makeSphere(), torus = makeTorus(), year = makeYear();
    switch (shape) {
    case DotsShape::Cube:
        return cube;
    case DotsShape::Sphere:
        return sphere;
    case DotsShape::Torus:
        return torus;
    default:
        return year;
    }
}

DotsPoints dotsShapePoints(DotsShape shape, std::uint32_t retroFrame) {
    const double t = static_cast<double>(retroFrame) / kDotsRetroRate;
    double yaw, pitch;
    if (shape == DotsShape::Year) {
        yaw = 0.12 * std::sin(kTau * t / 3.7);
        pitch = 0.10 * std::sin(kTau * t / 2.9);
    } else {
        yaw = 0.6 + 0.85 * t;
        pitch = 0.4 + 0.12 * t; // slow, so the torus shows its hole rather than its edge
    }
    const DotsPoints& model = dotsModel(shape);
    DotsPoints world{};
    for (std::size_t i = 0; i < kDotsPoints; ++i)
        world[i] = turn(model[i], yaw, pitch);
    return world;
}

DotsMorph dotsMorph(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::array<Step, kMorphs> steps = schedule(frame);
    DotsShape held = kOrder[0];
    for (std::size_t m = 0; m < kMorphs; ++m) {
        if (retroFrame >= steps[m].arrival) {
            held = kOrder[m + 1];
            continue;
        }
        if (retroFrame > steps[m].first) {
            const double s = static_cast<double>(retroFrame - steps[m].first) /
                             static_cast<double>(steps[m].arrival - steps[m].first);
            return {kOrder[m], kOrder[m + 1], smoothstep(s)};
        }
        break;
    }
    return {held, held, 0};
}

std::array<DotsStep, 6> dotsSchedule(const show::TimelineFrame& frame) {
    return schedule(frame);
}

DotsPoints dotsPositions(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::array<Step, kMorphs> steps = schedule(frame);
    // Point k of the cloud starts on cube point k; each morph that has begun moves it to the point of the
    // same pairing rank in the next shape (paired at the morph's first frame), so it moves continuously.
    std::array<std::size_t, kDotsPoints> index{};
    std::iota(index.begin(), index.end(), std::size_t{0});
    for (std::size_t m = 0; m < kMorphs && retroFrame > steps[m].first; ++m) {
        const auto fromOrder = pairingOrder(dotsShapePoints(kOrder[m], steps[m].first));
        const auto toOrder = pairingOrder(dotsShapePoints(kOrder[m + 1], steps[m].first));
        std::array<std::size_t, kDotsPoints> rankOf{};
        for (std::size_t r = 0; r < kDotsPoints; ++r)
            rankOf[fromOrder[r]] = r;
        std::array<std::size_t, kDotsPoints> next{};
        for (std::size_t k = 0; k < kDotsPoints; ++k)
            next[k] = toOrder[rankOf[index[k]]];

        if (retroFrame < steps[m].arrival) {
            const double blend = dotsMorph(frame, retroFrame).blend;
            const DotsPoints from = dotsShapePoints(kOrder[m], retroFrame);
            const DotsPoints to = dotsShapePoints(kOrder[m + 1], retroFrame);
            DotsPoints out{};
            for (std::size_t k = 0; k < kDotsPoints; ++k)
                for (int c = 0; c < 3; ++c)
                    out[k][c] = from[index[k]][c] + blend * (to[next[k]][c] - from[index[k]][c]);
            return out;
        }
        index = next;
    }
    const DotsMorph held = dotsMorph(frame, retroFrame);
    const DotsPoints shape = dotsShapePoints(held.from, retroFrame);
    DotsPoints out{};
    for (std::size_t k = 0; k < kDotsPoints; ++k)
        out[k] = shape[index[k]];
    return out;
}

Camera dotsCamera() {
    return Camera{{0, 0, -kDotsCameraDistance}, {0, 0, 0}, {0, 1, 0}, kFovY};
}

void dotsPalette(Palette& palette, const show::TimelineFrame&, std::uint32_t) {
    palette.fill(rgba(0, 0, 0));
    // Far to near: deep blue, blue, cyan, white, in three linear legs.
    constexpr int kKeys[4][3] = {{6, 10, 70}, {30, 70, 230}, {80, 210, 255}, {255, 255, 255}};
    constexpr int kLegs[4] = {0, 10, 19, 31}; // the ramp entry of each key
    for (int i = 0; i < kDotsShades; ++i) {
        int leg = 0;
        while (i > kLegs[leg + 1])
            ++leg;
        const double t = static_cast<double>(i - kLegs[leg]) / (kLegs[leg + 1] - kLegs[leg]);
        const auto channel = [&](int c) {
            return static_cast<std::uint8_t>(
                std::lround(kKeys[leg][c] + t * (kKeys[leg + 1][c] - kKeys[leg][c])));
        };
        palette[kDotsRamp + i] = rgba(channel(0), channel(1), channel(2));
    }
    palette[kDotsWhite] = rgba(255, 255, 255);
}

void dotsScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const Camera camera = dotsCamera();
    const DotsPoints positions = dotsPositions(frame, retroFrame);
    RetroScene dots;
    for (const Point& p : positions) {
        const double world[3] = {p[0], p[1], p[2]};
        const double depth = p[2] + kDotsCameraDistance; // the camera looks along +z from -distance
        const double nearness = std::clamp((kDotsFar - depth) / (kDotsFar - kDotsNear), 0.0, 1.0);
        const int shade = std::min(kDotsShades - 1, static_cast<int>(nearness * kDotsShades));
        addPoint(dots, camera, world, static_cast<std::uint8_t>(kDotsRamp + shade), depth < kDotsBig ? 2 : 1);
    }
    sortBackToFront(dots);
    scene.points.insert(scene.points.end(), dots.points.begin(), dots.points.end());
    scene.pointDepth.insert(scene.pointDepth.end(), dots.pointDepth.begin(), dots.pointDepth.end());
}

} // namespace evo::retro
