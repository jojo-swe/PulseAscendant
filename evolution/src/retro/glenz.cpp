// GLENZ: the mesh is Future Crew's (Second Reality, GLENZ/MAIN.C, Unlicense); see retro/glenz.hpp.

#include "retro/glenz.hpp"

#include "show/music_curves.hpp"

#include <algorithm>
#include <cmath>

namespace evo::retro {

namespace {

constexpr double kFovY = 45.0;

// The spin, radians a second about x, y and z, and the starting angles.
constexpr double kSpin[3] = {0.83, 1.21, 0.47};
constexpr double kStart[3] = {0.4, 0.2, 0.1};

using Point = GlenzPoint;

// About x, then y, then z.
Point turn(const Point& p, const double angle[3]) {
    const double cx = std::cos(angle[0]), sx = std::sin(angle[0]);
    const double cy = std::cos(angle[1]), sy = std::sin(angle[1]);
    const double cz = std::cos(angle[2]), sz = std::sin(angle[2]);
    const double y1 = cx * p[1] - sx * p[2], z1 = sx * p[1] + cx * p[2];
    const double x2 = cy * p[0] + sy * z1, z2 = -sy * p[0] + cy * z1;
    return {cz * x2 - sz * y1, sz * x2 + cz * y1, z2};
}

// Linear blend of two colours, t of the way to b.
std::array<double, 3> mix(const std::array<double, 3>& a, const std::array<double, 3>& b, double t) {
    return {a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), a[2] + t * (b[2] - a[2])};
}

} // namespace

const GlenzMesh& glenzMesh() {
    // GLENZ/MAIN.C points[] / 100 and epolys[] (vertex order unchanged), as ported in the parent project's
    // src/scene_math.cpp.
    static const GlenzMesh mesh{
        {{{-1, -1, -1},
          {1, -1, -1},
          {1, 1, -1},
          {-1, 1, -1},
          {-1, -1, 1},
          {1, -1, 1},
          {1, 1, 1},
          {-1, 1, 1},
          {0, 0, -1.7},
          {0, 0, 1.7},
          {1.7, 0, 0},
          {-1.7, 0, 0},
          {0, 1.7, 0},
          {0, -1.7, 0}}},
        {{{0, 1, 8},  {1, 2, 8},  {2, 3, 8},  {3, 0, 8},  {2, 1, 10}, {1, 5, 10}, {5, 6, 10}, {6, 2, 10},
          {2, 6, 12}, {6, 7, 12}, {7, 3, 12}, {3, 2, 12}, {0, 3, 11}, {3, 7, 11}, {7, 4, 11}, {4, 0, 11},
          {5, 1, 13}, {1, 0, 13}, {0, 4, 13}, {4, 5, 13}, {5, 4, 9},  {4, 7, 9},  {7, 6, 9},  {6, 5, 9}}}};
    return mesh;
}

double glenzBeatPhaseAt(std::span<const double> beats, double t) {
    constexpr double kOnBeat = 1e-9; // a beat this close ahead counts as reached (BeatGridView::beatAt)
    const auto after = std::upper_bound(beats.begin(), beats.end(), t + kOnBeat);
    double previous = 0, length = 0;
    if (after == beats.begin()) { // before the first beat: the first beat length repeats backwards
        const auto second = std::upper_bound(beats.begin(), beats.end(), beats.empty() ? 0.0 : beats.front());
        if (second == beats.end())
            return 0;
        previous = beats.front();
        length = *second - previous;
    } else if (after == beats.end()) { // after the last beat: the last beat length repeats
        previous = beats.back();
        const auto last = std::lower_bound(beats.begin(), beats.end(), previous);
        if (last == beats.begin())
            return 0;
        length = previous - *(last - 1);
    } else { // upper_bound skips duplicates, so the next beat is strictly later
        previous = *(after - 1);
        length = *after - previous;
    }
    if (t < previous && t >= previous - kOnBeat)
        return 0;
    const double phase = (t - previous) / length;
    return phase - std::floor(phase);
}

double glenzBounceAt(std::span<const double> beats, double t) {
    const double p = std::clamp(glenzBeatPhaseAt(beats, t), 0.0, 1.0);
    return 4 * p * (1 - p);
}

double glenzBeatPhase(const show::TimelineFrame& frame, double seconds) {
    // An allowed fixed fact (retro/effects.hpp): the chapter's start in its track and that track's tracked
    // beats are data of the chapter alone, like chapterDuration; nothing here reads the frame's music.
    const double start = show::chapterStart(frame.chapter);
    const int track = show::trackAt(start);
    return glenzBeatPhaseAt(show::beatGrid(track).times(), start - show::trackStart(track) + seconds);
}

double glenzBounce(const show::TimelineFrame& frame, double seconds) {
    const double p = std::clamp(glenzBeatPhase(frame, seconds), 0.0, 1.0);
    return 4 * p * (1 - p);
}

GlenzVertices glenzWorldVertices(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const double t = static_cast<double>(retroFrame) / kGlenzRetroRate;
    const double angle[3] = {kStart[0] + kSpin[0] * t, kStart[1] + kSpin[1] * t, kStart[2] + kSpin[2] * t};
    const double lift = kGlenzFloor + kGlenzLift * glenzBounce(frame, t);
    GlenzVertices world{};
    const GlenzVertices& model = glenzMesh().vertices;
    for (std::size_t i = 0; i < kGlenzVertices; ++i) {
        const Point p =
            turn({model[i][0] * kGlenzScale, model[i][1] * kGlenzScale, model[i][2] * kGlenzScale}, angle);
        world[i] = {p[0], p[1] + lift, p[2]};
    }
    return world;
}

Camera glenzCamera() {
    return Camera{{0, 0, -kGlenzCameraDistance}, {0, 0, 0}, {0, 1, 0}, kFovY};
}

void glenzPalette(Palette& palette, const show::TimelineFrame&, std::uint32_t) {
    // The copper bars: red, gold and green, each from a dim edge to a bright middle with a white sheen.
    constexpr std::array<double, 3> kBars[kGlenzCopperBars] = {{235, 50, 40}, {245, 190, 30}, {50, 210, 70}};
    // The glass: front faces light blue in two shades, back faces deep blue and violet.
    constexpr std::array<double, 3> kFront[2] = {{70, 140, 255}, {215, 235, 255}};
    constexpr std::array<double, 3> kBack[2] = {{15, 25, 125}, {110, 35, 190}};
    constexpr double kThroughBack = 0.5;   // how much of the copper a back face lets through
    constexpr double kThroughFront = 0.55; // and a front face, of what lies behind it

    std::array<std::array<double, 3>, 16> copper{};
    for (int bar = 0; bar < kGlenzCopperBars; ++bar)
        for (int step = 0; step < kGlenzCopperSteps; ++step) {
            const double level = 0.25 + 0.75 * step / (kGlenzCopperSteps - 1.0);
            const std::array<double, 3> lit = {kBars[bar][0] * level, kBars[bar][1] * level,
                                               kBars[bar][2] * level};
            copper[static_cast<std::size_t>(1 + bar * kGlenzCopperSteps + step)] =
                step == kGlenzCopperSteps - 1 ? mix(lit, {255, 255, 255}, 0.35) : lit;
        }
    for (int index = 0; index < 256; ++index) {
        std::array<double, 3> c = copper[static_cast<std::size_t>(index & kGlenzCopperMask)];
        const bool back = index & (kGlenzBack | kGlenzBackB), front = index & (kGlenzFront | kGlenzFrontB);
        if (back)
            c = mix(c, kBack[(index & kGlenzBackB) ? 1 : 0], 1 - kThroughBack);
        if (front)
            c = mix(c, kFront[(index & kGlenzFrontB) ? 1 : 0], 1 - kThroughFront);
        const auto channel = [&](int k) {
            return static_cast<std::uint8_t>(
                std::clamp(std::lround(c[static_cast<std::size_t>(k)]), 0L, 255L));
        };
        palette[static_cast<std::size_t>(index)] = rgba(channel(0), channel(1), channel(2));
    }
}

void glenzScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const Camera camera = glenzCamera();
    const GlenzVertices world = glenzWorldVertices(frame, retroFrame);
    const GlenzMesh& mesh = glenzMesh();
    // Back faces first, then front faces: nominal, as OR is commutative, but the order the original drew.
    for (const bool frontPass : {false, true})
        for (std::size_t f = 0; f < kGlenzTriangles; ++f) {
            const auto& tri = mesh.triangles[f];
            const Point& a = world[static_cast<std::size_t>(tri[0])];
            const Point& b = world[static_cast<std::size_t>(tri[1])];
            const Point& c = world[static_cast<std::size_t>(tri[2])];
            const double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const double v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const double n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                                 u[0] * v[1] - u[1] * v[0]};
            // The winding (clockwise on the y-down screen) makes the right-handed normal n point inward: a
            // front face's n points away from the eye.
            const double toFace[3] = {a[0] - camera.eye[0], a[1] - camera.eye[1], a[2] - camera.eye[2]};
            const bool front = n[0] * toFace[0] + n[1] * toFace[1] + n[2] * toFace[2] > 0;
            if (front != frontPass)
                continue;
            const bool odd = f % 2 == 1;
            const std::uint8_t code = front
                                          ? static_cast<std::uint8_t>(kGlenzFront | (odd ? kGlenzFrontB : 0))
                                          : static_cast<std::uint8_t>(kGlenzBack | (odd ? kGlenzBackB : 0));
            const double corners[3][3] = {{a[0], a[1], a[2]}, {b[0], b[1], b[2]}, {c[0], c[1], c[2]}};
            const std::uint8_t index[3] = {code, code, code};
            addTriangle(scene, camera, corners, index, kGlenz);
        }
}

} // namespace evo::retro
