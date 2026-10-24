// DOTS, chapter 2: the registry entry, the palette, the shapes and the morph schedule on the CPU (512 points
// every frame, each held shape exactly where its own projection puts it, morphs that flow instead of
// shuffling), and the 320x200 palette index image from the retro stage on the GPU: deterministic per retro
// frame, with many depth shades and a sensible coverage. Runs with the validation layer on and fails on any
// message it reports.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/sync.hpp"
#include "render/retro_stage.hpp"
#include "retro/dots.hpp"
#include "retro/effects.hpp"
#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"
#include "text/font8x8.hpp"
#include "text/text_pass.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::render::RetroStage;
using evo::retro::DotsPoints;
using evo::retro::DotsShape;
using evo::retro::Palette;
using evo::retro::RetroScene;
using evo::show::Effect;
using evo::show::TimelineFrame;

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

constexpr std::uint32_t kWidth = RetroStage::kWidth, kHeight = RetroStage::kHeight;
constexpr std::size_t kPixels = std::size_t{kWidth} * kHeight;
constexpr int kDotsChapter = 2;
constexpr std::uint32_t kRate = evo::retro::kDotsRetroRate;
constexpr std::size_t kCount = evo::retro::kDotsPoints;

// A DOTS frame at `local` seconds into the chapter.
TimelineFrame dots(double local) {
    const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(kDotsChapter) + local);
    if (frame.chapter != kDotsChapter || frame.effect != Effect::Dots)
        throw std::runtime_error("expected a DOTS frame at local time " + std::to_string(local));
    return frame;
}

double duration() {
    return dots(0).chapterDuration;
}

// The number of retro frames the chapter shows (the last one partial).
std::uint32_t frameCount() {
    return static_cast<std::uint32_t>(std::ceil(duration() * kRate));
}

// A time `within` of the way through a retro frame, kept inside the chapter.
double timeIn(std::uint32_t retroFrame, double within = .5) {
    return std::min((retroFrame + within) / kRate, duration() - 1e-6);
}

RetroScene sceneAt(std::uint32_t retroFrame) {
    RetroScene scene;
    evo::retro::dotsScene(scene, dots(timeIn(retroFrame)), retroFrame);
    return scene;
}

bool isRamp(std::uint8_t index) {
    return index >= evo::retro::kDotsRamp && index < evo::retro::kDotsRamp + evo::retro::kDotsShades;
}

int brightness(std::uint32_t entry) {
    return static_cast<int>((entry & 0xff) + (entry >> 8 & 0xff) + (entry >> 16 & 0xff));
}

double distance(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

double length(const std::array<double, 3>& a) {
    return std::hypot(a[0], a[1], a[2]);
}

void testRegistry() {
    const evo::retro::EffectInfo& info = evo::retro::effectFor(Effect::Dots);
    CHECK(info.shader == nullptr); // points on black
    CHECK(info.background == nullptr);
    CHECK(info.text == nullptr);
    CHECK(info.retroRate == 35 && kRate == 35);
    CHECK(info.palette == &evo::retro::dotsPalette);
    CHECK(info.scene == &evo::retro::dotsScene);
}

// Black at 0, white at 255, a 32-entry ramp from deep blue to white that brightens entry by entry, nothing
// else lit, the same at every frame.
void testPalette() {
    Palette palette{};
    palette.fill(0x12345678u);
    evo::retro::dotsPalette(palette, dots(1), 10);
    CHECK(palette[0] == evo::retro::rgba(0, 0, 0));
    CHECK(palette[255] == evo::retro::rgba(255, 255, 255));
    for (std::uint32_t entry : palette)
        CHECK((entry >> 24) == 0xff);
    const std::size_t first = evo::retro::kDotsRamp, last = first + evo::retro::kDotsShades - 1;
    CHECK(evo::retro::kDotsShades == 32);
    for (std::size_t i = first; i < last; ++i)
        CHECK(brightness(palette[i + 1]) > brightness(palette[i]));
    const std::uint32_t far = palette[first];
    CHECK(brightness(far) > 0 && (far >> 16 & 0xff) > (far & 0xff) && (far >> 16 & 0xff) > (far >> 8 & 0xff));
    CHECK(palette[last] == evo::retro::rgba(255, 255, 255));
    for (std::size_t i = 1; i < 255; ++i)
        if (i < first || i > last)
            CHECK(palette[i] == evo::retro::rgba(0, 0, 0));
    Palette later{};
    evo::retro::dotsPalette(later, dots(15), 500);
    CHECK(later == palette);
}

// The shapes in model space: the cube a full 8x8x8 lattice, the sphere on its radius, the torus on its
// surface, the year on the set pixels of "1993" in the demo's font with every set pixel used.
void testShapes() {
    using namespace evo::retro;
    const DotsPoints& cube = dotsModel(DotsShape::Cube);
    std::set<std::array<long, 3>> lattice;
    const double step = kDotsCubeSide / 7;
    for (const auto& p : cube) {
        std::array<long, 3> cell{};
        for (int c = 0; c < 3; ++c) {
            const double u = (p[c] + kDotsCubeSide / 2) / step;
            cell[c] = std::lround(u);
            CHECK(std::abs(u - cell[c]) < 1e-9 && cell[c] >= 0 && cell[c] < 8);
        }
        lattice.insert(cell);
    }
    CHECK(lattice.size() == kCount);

    std::set<std::array<double, 3>> distinct;
    for (const auto& p : dotsModel(DotsShape::Sphere)) {
        CHECK(std::abs(length(p) - kDotsSphereRadius) < 1e-9);
        distinct.insert(p);
    }
    CHECK(distinct.size() == kCount);
    // Spread evenly: every octant holds 64 +- 8 points.
    std::map<int, int> octants;
    for (const auto& p : dotsModel(DotsShape::Sphere))
        ++octants[(p[0] > 0) + 2 * (p[1] > 0) + 4 * (p[2] > 0)];
    for (const auto& [octant, count] : octants)
        CHECK(count >= 56 && count <= 72);

    distinct.clear();
    for (const auto& p : dotsModel(DotsShape::Torus)) {
        const double ring = std::hypot(p[0], p[2]) - kDotsTorusMajor;
        CHECK(std::abs(std::hypot(ring, p[1]) - kDotsTorusMinor) < 1e-9);
        distinct.insert(p);
    }
    CHECK(distinct.size() == kCount);

    // The year: each point lies in the cell of a set pixel of "1993" (column, row of the 8x8 glyphs), and
    // every set pixel holds 5 or 6 of the 512 points.
    std::map<std::pair<int, int>, int> cells;
    std::set<std::pair<int, int>> pixels;
    int minCol = 99, maxCol = -1, minRow = 99, maxRow = -1;
    for (int g = 0; g < 4; ++g) {
        const auto& rows = evo::text::glyph(kDotsYearText[g]);
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col)
                if (rows[row] & (0x80u >> col)) {
                    pixels.insert({g * 8 + col, row});
                    minCol = std::min(minCol, g * 8 + col), maxCol = std::max(maxCol, g * 8 + col);
                    minRow = std::min(minRow, row), maxRow = std::max(maxRow, row);
                }
    }
    const double centreCol = (minCol + maxCol) / 2.0, centreRow = (minRow + maxRow) / 2.0;
    for (const auto& p : dotsModel(DotsShape::Year)) {
        const double col = p[0] / kDotsYearCell + centreCol, row = centreRow - p[1] / kDotsYearCell;
        const std::pair<int, int> cell{static_cast<int>(std::lround(col)),
                                       static_cast<int>(std::lround(row))};
        CHECK(std::abs(col - cell.first) < 0.5 && std::abs(row - cell.second) < 0.5);
        CHECK(pixels.count(cell) == 1);
        CHECK(p[2] <= -kDotsYearForward + 2 * kDotsYearLayer + 1e-9 && p[2] >= -kDotsYearForward - 1e-9);
        ++cells[cell];
    }
    CHECK(cells.size() == pixels.size());
    for (const auto& [cell, count] : cells)
        CHECK(count == static_cast<int>(kCount / pixels.size()) ||
              count == static_cast<int>(kCount / pixels.size()) + 1);
    std::printf("dots: \"1993\" has %zu set pixels in the font, %zu..%zu points each\n", pixels.size(),
                kCount / pixels.size(), kCount / pixels.size() + 1);

    // dotsShapePoints turns the model rigidly: distances from the centre are kept.
    for (DotsShape shape : {DotsShape::Cube, DotsShape::Sphere, DotsShape::Torus, DotsShape::Year}) {
        const DotsPoints world = dotsShapePoints(shape, 123);
        for (std::size_t i = 0; i < kCount; ++i)
            CHECK(std::abs(length(world[i]) - length(dotsModel(shape)[i])) < 1e-9);
    }
}

// Projects world points through the DOTS camera to 28.4 screen positions.
std::vector<std::array<std::int32_t, 2>> projected(const DotsPoints& points) {
    const evo::retro::Camera camera = evo::retro::dotsCamera();
    std::vector<std::array<std::int32_t, 2>> out;
    for (const auto& p : points) {
        const double world[3] = {p[0], p[1], p[2]};
        const evo::retro::Projected s = evo::retro::project(camera, world);
        out.push_back({evo::retro::toFixed(s.x), evo::retro::toFixed(s.y)});
    }
    return out;
}

// Whether every expected position has its own scene point within one pixel (16 units of 28.4), one to one:
// equal positions pair first, then each left-over expected position takes its nearest free point.
bool matchesWithinAPixel(const RetroScene& scene, const std::vector<std::array<std::int32_t, 2>>& expected) {
    if (scene.points.size() != expected.size())
        return false;
    std::multiset<std::array<std::int32_t, 2>> free;
    for (const evo::retro::RetroPoint& p : scene.points)
        free.insert({p.x, p.y});
    std::vector<std::array<std::int32_t, 2>> left;
    for (const auto& e : expected) {
        const auto it = free.find(e);
        if (it != free.end())
            free.erase(it);
        else
            left.push_back(e);
    }
    for (const auto& e : left) {
        auto best = free.end();
        std::int32_t bestDistance = 17;
        for (auto it = free.lower_bound({e[0] - 16, INT32_MIN}); it != free.end() && (*it)[0] <= e[0] + 16;
             ++it) {
            const std::int32_t d = std::max(std::abs((*it)[0] - e[0]), std::abs((*it)[1] - e[1]));
            if (d < bestDistance)
                bestDistance = d, best = it;
        }
        if (best == free.end())
            return false;
        free.erase(best);
    }
    return true;
}

// The schedule: 512 points on the screen every frame, all in the ramp, sized 1 or 2; every shape held at
// some frame and the year held on the accent; at every held frame the points are exactly the shape's own
// projection (within a pixel); morphs blend in (0, 1) and flow.
void testSchedule() {
    using namespace evo::retro;
    const std::uint32_t count = frameCount();
    const TimelineFrame start = dots(0);
    std::map<DotsShape, int> held;
    std::size_t morphFrames = 0, sizeTwo = 0;
    DotsPoints previous{};
    double fastest = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> morphs; // first and last frame of each morph
    for (std::uint32_t retroFrame = 0; retroFrame < count; ++retroFrame) {
        const TimelineFrame frame = dots(timeIn(retroFrame));
        const RetroScene scene = sceneAt(retroFrame);
        CHECK(scene.points.size() == kCount);
        CHECK(scene.triangles.empty());
        for (const RetroPoint& p : scene.points) {
            CHECK(isRamp(p.index) && (p.size == 1 || p.size == 2));
            sizeTwo += p.size == 2;
        }
        // Back to front.
        for (std::size_t i = 1; i < scene.pointDepth.size(); ++i)
            CHECK(scene.pointDepth[i - 1] >= scene.pointDepth[i]);

        const DotsMorph morph = dotsMorph(frame, retroFrame);
        const DotsPoints positions = dotsPositions(frame, retroFrame);
        CHECK(matchesWithinAPixel(scene, projected(positions)));
        if (morph.from == morph.to) {
            CHECK(morph.blend == 0);
            ++held[morph.from];
            CHECK(matchesWithinAPixel(scene, projected(dotsShapePoints(morph.from, retroFrame))));
        } else {
            CHECK(morph.blend > 0 && morph.blend < 1);
            ++morphFrames;
            if (morphs.empty() || morphs.back().second + 1 != retroFrame)
                morphs.push_back({retroFrame, retroFrame});
            else
                morphs.back().second = retroFrame;
        }
        // No point jumps: on the screen (about 50 px a unit) none moves more than 12 px a frame.
        if (retroFrame > 0)
            for (std::size_t i = 0; i < kCount; ++i)
                fastest = std::max(fastest, distance(positions[i], previous[i]));
        previous = positions;
    }
    std::printf("dots: %u frames, %zu morphing in %zu morphs; held frames cube %d, sphere %d, torus %d, year "
                "%d; fastest point %.3f units a frame; %.0f %% of points 2x2\n",
                count, morphFrames, morphs.size(), held[DotsShape::Cube], held[DotsShape::Sphere],
                held[DotsShape::Torus], held[DotsShape::Year], fastest, 100.0 * sizeTwo / (count * kCount));
    CHECK(morphs.size() == 6);
    CHECK(held.size() == 4);
    for (const auto& [shape, frames] : held)
        CHECK(frames >= 20);
    CHECK(fastest < 0.24);

    // The year is held on the chapter's strongest hit; the chapter starts and ends on the cube.
    const auto accent = static_cast<std::uint32_t>(std::floor(start.accentSeconds * kRate));
    const DotsMorph onAccent = dotsMorph(dots(timeIn(accent)), accent);
    CHECK(onAccent.from == DotsShape::Year && onAccent.to == DotsShape::Year);
    CHECK(dotsMorph(start, 0).from == DotsShape::Cube && dotsMorph(start, 0).to == DotsShape::Cube);
    const DotsMorph last = dotsMorph(dots(timeIn(count - 1)), count - 1);
    CHECK(last.from == DotsShape::Cube && last.to == DotsShape::Cube);
    // The order: cube, sphere, torus, year, torus, sphere, cube.
    const DotsShape order[] = {DotsShape::Cube,  DotsShape::Sphere, DotsShape::Torus, DotsShape::Year,
                               DotsShape::Torus, DotsShape::Sphere, DotsShape::Cube};
    for (std::size_t m = 0; m < morphs.size(); ++m) {
        const DotsMorph a = dotsMorph(dots(timeIn(morphs[m].first)), morphs[m].first);
        CHECK(a.from == order[m] && a.to == order[m + 1]);
        // Smoothstep: the blend rises through the morph.
        double blend = 0;
        for (std::uint32_t f = morphs[m].first; f <= morphs[m].second; ++f) {
            const double b = dotsMorph(dots(timeIn(f)), f).blend;
            CHECK(b > blend);
            blend = b;
        }
    }

    // Morphs flow: each point's start (on the first shape, held the frame before the morph) and end (on the
    // second, held the frame after) are found among the shapes' points; at the middle of the morph, with
    // both shapes turned as they are there, the pairs lie much closer on average than under a random pairing
    // of the same points.
    std::mt19937 random(512);
    const auto labels = [](const DotsPoints& positions, const DotsPoints& shape) {
        std::map<std::array<double, 3>, std::size_t> where;
        for (std::size_t i = 0; i < kCount; ++i)
            where[shape[i]] = i;
        std::vector<std::size_t> out;
        for (const auto& p : positions) {
            const auto it = where.find(p);
            if (it == where.end())
                throw std::runtime_error("a held point is not on its shape");
            out.push_back(it->second);
        }
        return out;
    };
    for (std::size_t m = 0; m < morphs.size(); ++m) {
        const auto [first, lastFrame] = morphs[m];
        const std::vector<std::size_t> from =
            labels(dotsPositions(dots(timeIn(first - 1)), first - 1), dotsShapePoints(order[m], first - 1));
        const std::vector<std::size_t> to = labels(dotsPositions(dots(timeIn(lastFrame + 1)), lastFrame + 1),
                                                   dotsShapePoints(order[m + 1], lastFrame + 1));
        CHECK(std::set<std::size_t>(from.begin(), from.end()).size() == kCount);
        CHECK(std::set<std::size_t>(to.begin(), to.end()).size() == kCount);
        const std::uint32_t middle = (first + lastFrame) / 2;
        const DotsPoints a = dotsShapePoints(order[m], middle), b = dotsShapePoints(order[m + 1], middle);
        std::vector<std::size_t> permutation(to);
        std::shuffle(permutation.begin(), permutation.end(), random);
        double travel = 0, shuffled = 0;
        for (std::size_t k = 0; k < kCount; ++k) {
            travel += distance(a[from[k]], b[to[k]]);
            shuffled += distance(a[from[k]], b[permutation[k]]);
        }
        std::printf("dots: morph %zu (frames %u..%u): pairs %.3f units apart on average, %.3f under a random "
                    "pairing\n",
                    m, first, lastFrame, travel / kCount, shuffled / kCount);
        // A solid flows to a solid closely; to or from the year, a flat strip about 4 units wide and 1 high,
        // the pairs lie further apart, but still well below a shuffle.
        const bool year = order[m] == DotsShape::Year || order[m + 1] == DotsShape::Year;
        CHECK(travel < (year ? 0.8 : 0.5) * shuffled);
    }
}

// The stage's index image of a DOTS frame.
class Fixture {
public:
    explicit Fixture(evo::gfx::Context& ctx)
        : ring_(ctx), text_(ctx, ring_), stage_(ctx, ring_, text_),
          buffer_(ctx, {kPixels, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "dots test index readback"}) {}

    std::vector<std::uint8_t> render(const TimelineFrame& frame, bool* rendered = nullptr) {
        VkCommandBuffer cmd = ring_.begin();
        const bool recorded = stage_.render(cmd, frame);
        if (rendered)
            *rendered = recorded;
        evo::gfx::Image& index = stage_.indexImage();
        index.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kWidth, kHeight, 1};
        vkCmdCopyImageToBuffer(cmd, index.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer_.handle(), 1,
                               &region);
        evo::gfx::bufferBarrier2(cmd, buffer_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                                 VK_ACCESS_2_HOST_READ_BIT);
        ring_.wait(ring_.submit());
        buffer_.invalidate();
        const auto* data = static_cast<const std::uint8_t*>(buffer_.mapped());
        return {data, data + kPixels};
    }

    RetroStage& stage() { return stage_; }

private:
    evo::gfx::FrameRing ring_;
    evo::text::TextPass text_;
    RetroStage stage_;
    evo::gfx::Buffer buffer_;
};

// The first retro frame at or after `local` seconds where the schedule holds `shape`.
std::uint32_t heldFrame(DotsShape shape, double local) {
    for (auto f = static_cast<std::uint32_t>(local * kRate); f < frameCount(); ++f) {
        const evo::retro::DotsMorph m = evo::retro::dotsMorph(dots(timeIn(f)), f);
        if (m.from == shape && m.to == shape)
            return f;
    }
    throw std::runtime_error("shape never held");
}

void testImages(evo::gfx::Context& ctx) {
    Fixture fixture(ctx);
    // A cube, a morph, the torus and the year: only black and the ramp, at least 12 shades, 400..2000 pixels
    // lit (512 points, some 2x2, some overlapping); deterministic.
    std::uint32_t morphFrame = 0;
    for (std::uint32_t f = 0; f < frameCount(); ++f)
        if (evo::retro::dotsMorph(dots(timeIn(f)), f).blend > .45) {
            morphFrame = f;
            break;
        }
    const std::uint32_t frames[] = {heldFrame(DotsShape::Cube, 0), morphFrame, heldFrame(DotsShape::Torus, 0),
                                    heldFrame(DotsShape::Year, 0)};
    for (std::uint32_t retroFrame : frames) {
        bool rendered = false;
        const std::vector<std::uint8_t> image = fixture.render(dots(timeIn(retroFrame, .1)), &rendered);
        CHECK(rendered && fixture.stage().retroFrame() == retroFrame);
        std::set<std::uint8_t> shades;
        std::size_t lit = 0;
        for (std::uint8_t index : image) {
            CHECK(index == 0 || isRamp(index));
            if (index != 0) {
                shades.insert(index);
                ++lit;
            }
        }
        std::printf("dots: retro frame %u: %zu pixels lit in %zu shades\n", retroFrame, lit, shades.size());
        CHECK(lit >= 400 && lit <= 2000);
        CHECK(shades.size() >= (retroFrame == frames[3] ? 2u : 12u)); // the year stands almost flat

        // Deterministic: the same retro frame at another moment within it, with the music signals pushed,
        // after another frame, gives the identical image.
        fixture.render(dots(timeIn(retroFrame + 40)), &rendered);
        CHECK(rendered);
        TimelineFrame again = dots(timeIn(retroFrame, .9));
        again.kick = 1;
        again.energy = 1;
        again.beatPhase = .5f;
        CHECK(fixture.render(again, &rendered) == image);
        CHECK(rendered);
    }
    // A second stage, fresh, renders the same retro frame identically.
    Fixture other(ctx);
    CHECK(other.render(dots(timeIn(frames[1]))) == fixture.render(dots(timeIn(frames[1], .3))));
}

} // namespace

int main() {
    try {
        setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testPalette();
        testShapes();
        testSchedule();
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
    std::printf("dots_test: %d checks passed\n", checksPassed);
    return 0;
}
