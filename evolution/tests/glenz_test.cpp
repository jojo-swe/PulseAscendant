// GLENZ, chapter 5: the registry entry, the palette, Future Crew's mesh (14 vertices, 24 triangles, closed
// and wound outwards), the beat bounce on the chapter's tracked beats, the silhouette over the chapter, and
// the 320x200 palette index image from the retro stage on the GPU: copper bars per row, every glenz pixel the
// copper index ORed with the codes of the faces covering it (a CPU reference), front-and-back overlaps at
// code 3, deterministic per retro frame. Runs with the validation layer on and fails on any message it
// reports.

#include "gfx/context.hpp"
#include "retro/effects.hpp"
#include "retro/glenz.hpp"
#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "retro_stage_fixture.hpp"
#include "show/music_curves.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using evo::retro::Palette;
using evo::retro::RetroScene;
using evo::retro::RetroTriangle;
using evo::show::Effect;
using evo::show::TimelineFrame;
using evo::test::RetroStageFixture;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

constexpr int kWidth = static_cast<int>(RetroStageFixture::kWidth);
constexpr int kHeight = static_cast<int>(RetroStageFixture::kHeight);
constexpr int kGlenzChapter = 5;
constexpr std::uint32_t kRate = evo::retro::kGlenzRetroRate;
constexpr std::uint8_t kCodes = evo::retro::kGlenzFront | evo::retro::kGlenzBack;

// A GLENZ frame at `local` seconds into the chapter.
TimelineFrame glenz(double local) {
    const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(kGlenzChapter) + local);
    if (frame.chapter != kGlenzChapter || frame.effect != Effect::Glenz)
        throw std::runtime_error("expected a GLENZ frame at local time " + std::to_string(local));
    return frame;
}

double duration() {
    return glenz(0).chapterDuration;
}

std::uint32_t frameCount() {
    return static_cast<std::uint32_t>(std::ceil(duration() * kRate));
}

double timeIn(std::uint32_t retroFrame, double within = .5) {
    return std::min((retroFrame + within) / kRate, duration() - 1e-6);
}

RetroScene sceneAt(std::uint32_t retroFrame) {
    RetroScene scene;
    evo::retro::glenzScene(scene, glenz(timeIn(retroFrame)), retroFrame);
    return scene;
}

void testRegistry() {
    const evo::retro::EffectInfo& info = evo::retro::effectFor(Effect::Glenz);
    CHECK(info.shader != nullptr && std::string(info.shader) == evo::retro::kGlenzCopperShader);
    CHECK(info.retroRate == 35 && kRate == 35);
    CHECK(info.palette == &evo::retro::glenzPalette);
    CHECK(info.scene == &evo::retro::glenzScene);
    CHECK(info.text == nullptr);
}

std::array<int, 3> rgbOf(std::uint32_t entry) {
    return {static_cast<int>(entry & 0xff), static_cast<int>(entry >> 8 & 0xff),
            static_cast<int>(entry >> 16 & 0xff)};
}

int brightness(std::uint32_t entry) {
    const auto c = rgbOf(entry);
    return c[0] + c[1] + c[2];
}

// Index 0 black; the copper bars brighten step by step; every glenz combination tints the copper under it
// (a different colour from the bare copper, bluer than it), the two checker colours of a face differ, and
// the palette is the same at every frame.
void testPalette() {
    using namespace evo::retro;
    Palette palette{};
    palette.fill(0x12345678u);
    glenzPalette(palette, glenz(1), 10);
    CHECK(palette[0] == rgba(0, 0, 0));
    for (std::uint32_t entry : palette)
        CHECK((entry >> 24) == 0xff);
    CHECK(kGlenzCopperBars * kGlenzCopperSteps + 1 <= 16);
    for (int bar = 0; bar < kGlenzCopperBars; ++bar)
        for (int step = 1; step < kGlenzCopperSteps; ++step)
            CHECK(brightness(palette[1 + bar * kGlenzCopperSteps + step]) >
                  brightness(palette[bar * kGlenzCopperSteps + step]));
    for (int copper = 0; copper < 16; ++copper) {
        const auto bare = rgbOf(palette[copper]);
        for (int glass = 1; glass < 16; ++glass) {
            const std::size_t index = static_cast<std::size_t>(copper | glass << 4);
            const auto tinted = rgbOf(palette[index]);
            CHECK(palette[index] != palette[copper]);
            if (glass & 3) // seen through glass: bluer than the copper itself
                CHECK(tinted[2] - std::max(tinted[0], tinted[1]) > bare[2] - std::max(bare[0], bare[1]));
        }
        // The checker colours of a front face, and of a back face, differ.
        CHECK(palette[copper | kCodes] != palette[copper | kCodes | kGlenzFrontB]);
        CHECK(palette[copper | kCodes] != palette[copper | kCodes | kGlenzBackB]);
    }
    Palette later{};
    glenzPalette(later, glenz(12), 400);
    CHECK(later == palette);
}

// Future Crew's mesh: 14 vertices, 24 triangles, every edge shared by exactly two triangles (closed), each
// directed edge used once (consistent winding), wound clockwise from outside as in the original (every
// face's right-handed normal points towards the centre, which the star shape keeps inside every face's
// plane).
void testMesh() {
    using namespace evo::retro;
    const GlenzMesh& mesh = glenzMesh();
    CHECK(mesh.vertices.size() == 14 && mesh.triangles.size() == 24);
    std::map<std::pair<int, int>, int> edges, directed;
    std::set<int> used;
    for (const auto& t : mesh.triangles)
        for (int k = 0; k < 3; ++k) {
            const int a = t[k], b = t[(k + 1) % 3];
            CHECK(a >= 0 && a < 14 && a != b);
            used.insert(a);
            ++edges[{std::min(a, b), std::max(a, b)}];
            ++directed[{a, b}];
        }
    CHECK(used.size() == 14);
    CHECK(edges.size() == 36); // Euler: V - E + F = 14 - 36 + 24 = 2
    for (const auto& [edge, count] : edges)
        CHECK(count == 2);
    for (const auto& [edge, count] : directed)
        CHECK(count == 1);
    for (const auto& t : mesh.triangles) {
        const auto& a = mesh.vertices[t[0]];
        const auto& b = mesh.vertices[t[1]];
        const auto& c = mesh.vertices[t[2]];
        const double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]},
                     v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const double n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        CHECK(n[0] * a[0] + n[1] * a[1] + n[2] * a[2] < 0);
    }
    // The original's points: the cube's corners at +-1 and the six tips at 1.7 on the axes.
    int corners = 0, tips = 0;
    for (const auto& p : mesh.vertices) {
        const double m = std::max({std::abs(p[0]), std::abs(p[1]), std::abs(p[2])});
        if (std::abs(p[0]) == 1 && std::abs(p[1]) == 1 && std::abs(p[2]) == 1)
            ++corners;
        else if (std::abs(m - 1.7) < 1e-12 && std::abs(p[0]) + std::abs(p[1]) + std::abs(p[2]) == m)
            ++tips;
    }
    CHECK(corners == 8 && tips == 6);
}

// The chapter's beats in chapter-local seconds, from the beat grid of the chapter's track.
std::vector<double> chapterBeats() {
    const double start = evo::show::chapterStart(kGlenzChapter);
    const int track = evo::show::trackAt(start);
    const double offset = start - evo::show::trackStart(track);
    const evo::show::BeatGridView grid = evo::show::beatGrid(track);
    std::vector<double> beats;
    for (int i = 0; i < grid.count(); ++i) {
        const double local = grid.beatTime(i) - offset;
        if (local >= 0 && local < duration())
            beats.push_back(local);
    }
    return beats;
}

// The bounce: on the floor (height 0, phase 0) on every tracked beat of the chapter, at the top (1) halfway
// to the next; never outside 0..1; the music signals of the frame make no difference; and the mesh's centre
// sits where the bounce of its retro frame puts it.
void testBounce() {
    using namespace evo::retro;
    const std::vector<double> beats = chapterBeats();
    const TimelineFrame frame = glenz(0);
    std::printf("glenz: %zu beats in the chapter, %.3f s apart on average\n", beats.size(),
                (beats.back() - beats.front()) / static_cast<double>(beats.size() - 1));
    CHECK(beats.size() >= 20);
    for (std::size_t i = 0; i < beats.size(); ++i) {
        CHECK(glenzBeatPhase(frame, beats[i]) < 1e-6);
        CHECK(glenzBounce(frame, beats[i]) < 1e-5);
        if (i + 1 < beats.size()) {
            const double middle = (beats[i] + beats[i + 1]) / 2;
            CHECK(std::abs(glenzBeatPhase(frame, middle) - 0.5) < 1e-6);
            CHECK(glenzBounce(frame, middle) > 1 - 1e-6);
            // Just before the next beat: about to land.
            CHECK(glenzBeatPhase(frame, beats[i + 1] - 1e-4) > 0.99);
        }
    }
    TimelineFrame loud = glenz(9);
    loud.kick = 1, loud.energy = 1, loud.beatPhase = .77f;
    for (std::uint32_t f = 0; f < frameCount(); ++f) {
        const double t = static_cast<double>(f) / kRate;
        const double h = glenzBounce(frame, t);
        CHECK(h >= 0 && h <= 1);
        CHECK(glenzBounce(loud, t) == h);
        const GlenzVertices world = glenzWorldVertices(glenz(timeIn(f)), f);
        double y = 0;
        for (const auto& p : world)
            y += p[1];
        CHECK(std::abs(y / 14 - (kGlenzFloor + kGlenzLift * h)) < 1e-9);
    }
}

// Whether a pixel centre is covered by a triangle, with polys.comp's top-left rule (scene.hpp).
bool covers(const RetroTriangle& t, int px, int py) {
    std::int64_t x[3] = {t.x[0], t.x[1], t.x[2]}, y[3] = {t.y[0], t.y[1], t.y[2]};
    const auto edge = [](std::int64_t ax, std::int64_t ay, std::int64_t bx, std::int64_t by, std::int64_t cx,
                         std::int64_t cy) { return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax); };
    std::int64_t area = edge(x[0], y[0], x[1], y[1], x[2], y[2]);
    if (area == 0)
        return false;
    if (area < 0)
        std::swap(x[1], x[2]), std::swap(y[1], y[2]);
    const std::int64_t cx = px * 16 + 8, cy = py * 16 + 8;
    for (int k = 0; k < 3; ++k) {
        const int a = (k + 1) % 3, b = (k + 2) % 3;
        const std::int64_t e = edge(x[a], y[a], x[b], y[b], cx, cy);
        const std::int64_t dy = y[b] - y[a];
        const bool topLeft = dy < 0 || (dy == 0 && x[b] > x[a]);
        if (e < 0 || (e == 0 && !topLeft))
            return false;
    }
    return true;
}

// The OR of the covering glenz triangles' indices at every pixel (0 where none covers it), and which pixels
// a front and a back face both cover.
struct GlenzCover {
    std::vector<std::uint8_t> codes;
    std::vector<bool> frontAndBack;
};

GlenzCover cover(const RetroScene& scene) {
    GlenzCover out{std::vector<std::uint8_t>(std::size_t{kWidth} * kHeight, 0),
                   std::vector<bool>(std::size_t{kWidth} * kHeight, false)};
    std::vector<std::uint8_t> sides(out.codes.size(), 0);
    for (const RetroTriangle& t : scene.triangles) {
        int minX = kWidth, maxX = -1, minY = kHeight, maxY = -1;
        for (int k = 0; k < 3; ++k) {
            minX = std::min(minX, static_cast<int>(std::floor(t.x[k] / 16.0)) - 1);
            maxX = std::max(maxX, static_cast<int>(std::ceil(t.x[k] / 16.0)) + 1);
            minY = std::min(minY, static_cast<int>(std::floor(t.y[k] / 16.0)) - 1);
            maxY = std::max(maxY, static_cast<int>(std::ceil(t.y[k] / 16.0)) + 1);
        }
        for (int py = std::max(0, minY); py <= std::min(kHeight - 1, maxY); ++py)
            for (int px = std::max(0, minX); px <= std::min(kWidth - 1, maxX); ++px)
                if (covers(t, px, py)) {
                    const std::size_t i = static_cast<std::size_t>(py) * kWidth + px;
                    out.codes[i] |= t.index[0];
                    sides[i] |= t.index[0] & kCodes;
                }
    }
    for (std::size_t i = 0; i < sides.size(); ++i)
        out.frontAndBack[i] = sides[i] == kCodes;
    return out;
}

std::size_t area(const GlenzCover& c) {
    return static_cast<std::size_t>(
        std::count_if(c.codes.begin(), c.codes.end(), [](std::uint8_t v) { return v; }));
}

// Every retro frame: 24 glenz triangles at most (the mesh, none clipped), the back faces before the front
// faces, indices only from the glenz fields, front and back as the view says; and over the chapter the
// silhouette's area stays within 30 % of its mean while the mesh turns through every orientation it shows.
void testScene() {
    using namespace evo::retro;
    std::vector<std::size_t> areas;
    std::size_t fronts = 0, backs = 0;
    for (std::uint32_t f = 0; f < frameCount(); ++f) {
        const RetroScene scene = sceneAt(f);
        CHECK(scene.points.empty());
        CHECK(scene.triangles.size() == kGlenzTriangles);
        bool front = false;
        std::size_t frontCount = 0;
        double frontDepth = 0, backDepth = 0;
        int screenWinding[2] = {0, 0}; // per side: the sign of the screen area every face of it shares
        for (std::size_t k = 0; k < scene.triangles.size(); ++k) {
            const RetroTriangle& t = scene.triangles[k];
            const bool isFront = (t.index[0] & kCodes) == kGlenzFront;
            (isFront ? frontDepth : backDepth) += scene.triangleDepth[k];
            const std::int64_t screenArea = std::int64_t{t.x[1] - t.x[0]} * (t.y[2] - t.y[0]) -
                                            std::int64_t{t.y[1] - t.y[0]} * (t.x[2] - t.x[0]);
            // A face seen almost edge-on (under a pixel of area) may flip when its corners are rounded to
            // 1/16 px; every other face winds its side's way.
            const int sign = screenArea > 0 ? 1 : -1;
            if (std::abs(screenArea) >= 2 * 256) {
                CHECK(screenWinding[isFront] == 0 || screenWinding[isFront] == sign);
                screenWinding[isFront] = sign;
            }
            CHECK(t.mode == kGlenz);
            const std::uint8_t code = t.index[0] & kCodes;
            CHECK(code == kGlenzFront || code == kGlenzBack);
            CHECK((t.index[0] & ~(kCodes | kGlenzFrontB | kGlenzBackB)) == 0);
            CHECK(code == kGlenzFront ? (t.index[0] & kGlenzBackB) == 0 : (t.index[0] & kGlenzFrontB) == 0);
            if (code == kGlenzFront)
                front = true, ++frontCount;
            else
                CHECK(!front); // all back faces first
        }
        fronts += frontCount;
        backs += kGlenzTriangles - frontCount;
        CHECK(frontCount >= 3 && frontCount <= 21);
        // Front and back faces wind opposite ways on the screen, and the front faces are the nearer ones.
        CHECK(screenWinding[0] == -screenWinding[1]);
        CHECK(frontDepth / static_cast<double>(frontCount) <
              backDepth / static_cast<double>(kGlenzTriangles - frontCount));
        if (f % 5 == 0)
            areas.push_back(area(cover(scene)));
    }
    double mean = 0;
    for (std::size_t a : areas)
        mean += static_cast<double>(a);
    mean /= static_cast<double>(areas.size());
    const auto [low, high] = std::minmax_element(areas.begin(), areas.end());
    std::printf(
        "glenz: %zu front and %zu back faces drawn; silhouette %zu..%zu px over %zu frames, mean %.0f "
        "(%.0f %% .. %.0f %%)\n",
        fronts, backs, *low, *high, areas.size(), mean, 100.0 * static_cast<double>(*low) / mean,
        100.0 * static_cast<double>(*high) / mean);
    CHECK(static_cast<double>(*low) >= 0.7 * mean && static_cast<double>(*high) <= 1.3 * mean);
    CHECK(mean > 0.05 * kWidth * kHeight && mean < 0.4 * kWidth * kHeight);
}

// The stage's picture: every row's copper is one index of the copper range wherever no face covers it, and
// every covered pixel is that index ORed with the covering faces' codes, as the CPU reference has it; where
// a front and a back face overlap, the code is 3. Copper bars of all three colours appear and move.
void testImages(evo::gfx::Context& ctx) {
    RetroStageFixture fixture(ctx);
    const std::uint32_t count = frameCount();
    const std::uint32_t frames[] = {count / 5, count / 2, count * 4 / 5};
    std::vector<std::vector<std::uint8_t>> rowsOfFrame;
    for (std::uint32_t retroFrame : frames) {
        bool rendered = false;
        const std::vector<std::uint8_t> image = fixture.render(glenz(timeIn(retroFrame, .1)), &rendered);
        CHECK(rendered && fixture.stage().retroFrame() == retroFrame);
        const GlenzCover c = cover(sceneAt(retroFrame));
        std::vector<std::uint8_t> rows(kHeight, 0);
        std::set<std::uint8_t> indices, bars;
        std::size_t overlaps = 0, covered = 0;
        for (int y = 0; y < kHeight; ++y) {
            int copper = -1;
            for (int x = 0; x < kWidth; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * kWidth + x;
                if (c.codes[i] == 0) {
                    CHECK(image[i] <= evo::retro::kGlenzCopperMask);
                    CHECK(copper < 0 || image[i] == copper);
                    copper = image[i];
                }
            }
            CHECK(copper >= 0); // the glenz never spans a whole row
            rows[y] = static_cast<std::uint8_t>(copper);
            if (copper > 0)
                bars.insert(static_cast<std::uint8_t>((copper - 1) / evo::retro::kGlenzCopperSteps));
            for (int x = 0; x < kWidth; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * kWidth + x;
                indices.insert(image[i]);
                if (c.codes[i] == 0)
                    continue;
                ++covered;
                CHECK(image[i] == (copper | c.codes[i]));
                if (c.frontAndBack[i]) {
                    CHECK((image[i] & kCodes) == kCodes &&
                          (image[i] & evo::retro::kGlenzCopperMask) == copper);
                    ++overlaps;
                }
            }
        }
        std::printf(
            "glenz: retro frame %u: %zu px glenz (%zu front and back), %zu indices, copper bars %zu\n",
            retroFrame, covered, overlaps, indices.size(), bars.size());
        CHECK(overlaps > 0 && overlaps * 10 >= covered * 9);
        CHECK(indices.size() >= 20);
        CHECK(bars.size() == evo::retro::kGlenzCopperBars);
        rowsOfFrame.push_back(rows);

        // Deterministic: the same retro frame at another moment within it, with the music signals pushed,
        // after another frame, gives the identical image.
        fixture.render(glenz(timeIn(retroFrame + 30)), &rendered);
        CHECK(rendered);
        TimelineFrame again = glenz(timeIn(retroFrame, .9));
        again.kick = 1;
        again.energy = 1;
        again.beatPhase = .5f;
        CHECK(fixture.render(again, &rendered) == image);
        CHECK(rendered);
    }
    CHECK(rowsOfFrame[0] != rowsOfFrame[1] && rowsOfFrame[1] != rowsOfFrame[2]); // the bars move
    // A second stage, fresh, renders the same retro frame identically.
    RetroStageFixture other(ctx);
    CHECK(other.render(glenz(timeIn(frames[1]))) == fixture.render(glenz(timeIn(frames[1], .3))));
}

} // namespace

int main() {
    try {
        setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testPalette();
        testMesh();
        testBounce();
        testScene();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testImages(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("glenz_test: %d checks passed\n", checksPassed);
    return 0;
}
