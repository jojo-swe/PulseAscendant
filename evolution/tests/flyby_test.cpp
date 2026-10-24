// FLYBY, chapter 1: the registry entry, the palette and the scene on the CPU (the ship's model and shading
// indices, the camera's swoop, the starfield, the engines' flicker), and the 320x200 palette index image from
// the retro stage on the GPU: deterministic per retro frame, the ship covering 5..40 % of the frame at three
// moments of the chapter and drawn only inside its projected bounding box, mostly in the upper 70 % of the
// frame, over a field of stars. Runs with the validation layer on and fails on any message it reports.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/sync.hpp"
#include "render/retro_stage.hpp"
#include "retro/effects.hpp"
#include "retro/flyby.hpp"
#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "retro/scroller.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::render::RetroStage;
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
constexpr int kFlybyChapter = 1;
constexpr std::uint32_t kRate = evo::retro::kFlybyRetroRate;

// A FLYBY frame at `local` seconds into the chapter.
TimelineFrame flyby(double local) {
    const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(kFlybyChapter) + local);
    if (frame.chapter != kFlybyChapter || frame.effect != Effect::Flyby)
        throw std::runtime_error("expected a FLYBY frame at local time " + std::to_string(local));
    return frame;
}

double duration() {
    return flyby(0).chapterDuration;
}

// The last retro frame of the chapter.
std::uint32_t lastFrame() {
    return static_cast<std::uint32_t>(std::floor(duration() * kRate)) - 1;
}

// The retro frame `fraction` of the way through the chapter, and a time `within` of the way through it.
std::uint32_t frameAt(double fraction) {
    return static_cast<std::uint32_t>(fraction * lastFrame());
}
double timeIn(std::uint32_t retroFrame, double within = .5) {
    return (retroFrame + within) / kRate;
}

bool isShip(std::uint8_t index) {
    return index >= evo::retro::kFlybyShipFirst && index <= evo::retro::kFlybyShipLast;
}
bool isStar(std::uint8_t index) {
    return index >= evo::retro::kFlybyStar && index < evo::retro::kFlybyStar + evo::retro::kFlybyShades;
}

RetroScene sceneAt(std::uint32_t retroFrame) {
    RetroScene scene;
    evo::retro::flybyScene(scene, flyby(timeIn(retroFrame)), retroFrame);
    return scene;
}

int brightness(std::uint32_t entry) {
    return static_cast<int>((entry & 0xff) + (entry >> 8 & 0xff) + (entry >> 16 & 0xff));
}

void testRegistry() {
    const evo::retro::EffectInfo& info = evo::retro::effectFor(Effect::Flyby);
    CHECK(info.shader == nullptr); // a scene on black
    CHECK(info.background == nullptr);
    CHECK(info.retroRate == 35 && kRate == evo::retro::kScrollerRetroRate);
    CHECK(info.palette == &evo::retro::flybyPalette);
    CHECK(info.text == &evo::retro::flybyText);
    CHECK(info.scene == &evo::retro::flybyScene);
    CHECK(evo::retro::kFlybyIndex == evo::retro::kFlybyWhite);
}

// Black space and shadow at 0, the scroller's white at 255, five ramps that brighten entry by entry, the
// stars grey, and nothing else lit.
void testPalette() {
    Palette palette{};
    palette.fill(0x12345678u);
    evo::retro::flybyPalette(palette, flyby(1), 10);
    CHECK(palette[0] == evo::retro::rgba(0, 0, 0));
    CHECK(palette[255] == evo::retro::rgba(255, 255, 255));
    for (std::uint32_t entry : palette)
        CHECK((entry >> 24) == 0xff);
    for (std::size_t base : {16u, 32u, 48u, 64u, 128u}) {
        for (std::size_t i = base; i + 1 < base + 16; ++i)
            CHECK(brightness(palette[i + 1]) > brightness(palette[i]));
        CHECK(brightness(palette[base]) > 0);
    }
    // Hull grey-blue (blue strongest), wings teal (green and blue over red), cockpit amber (red over
    // green over blue), engines orange to white, stars grey.
    for (std::size_t i = 0; i < 16; ++i) {
        const std::uint32_t hull = palette[16 + i], wing = palette[32 + i], cockpit = palette[48 + i];
        const std::uint32_t star = palette[128 + i];
        CHECK((hull >> 16 & 0xff) >= (hull & 0xff));
        CHECK((wing >> 8 & 0xff) > (wing & 0xff) && (wing >> 16 & 0xff) > (wing & 0xff));
        CHECK((cockpit & 0xff) > (cockpit >> 8 & 0xff) && (cockpit >> 8 & 0xff) >= (cockpit >> 16 & 0xff));
        CHECK(std::abs(static_cast<int>(star & 0xff) - static_cast<int>(star >> 8 & 0xff)) <= 4);
    }
    CHECK(palette[64] == evo::retro::rgba(150, 34, 0) && palette[79] == evo::retro::rgba(255, 255, 255));
    for (std::size_t i = 1; i < 256; ++i)
        if (!(i >= 16 && i < 80) && !(i >= 128 && i < 144) && i != 255)
            CHECK(palette[i] == evo::retro::rgba(0, 0, 0));
    // Fixed: the same at any retro frame.
    Palette later{};
    evo::retro::flybyPalette(later, flyby(15), 600);
    CHECK(later == palette);
}

// The scene on the CPU: the ship's faces (flat, in its ramps, at most its model's faces after culling), the
// stars (in their greys, one or two pixels), the camera's swoop and the engines' flicker.
void testScene() {
    CHECK(evo::retro::kFlybyShipTriangles >= 40 && evo::retro::kFlybyShipTriangles <= 80);
    std::size_t fewestTriangles = 9999, fewestPoints = 9999, mostPoints = 0;
    std::set<std::uint8_t> engineIndices;
    for (std::uint32_t retroFrame = 0; retroFrame <= lastFrame(); retroFrame += 7) {
        const RetroScene scene = sceneAt(retroFrame);
        CHECK(scene.triangles.size() <= evo::retro::kFlybyShipTriangles);
        CHECK(scene.points.size() <= evo::retro::kFlybyStars);
        for (const evo::retro::RetroTriangle& t : scene.triangles) {
            CHECK(t.mode == evo::retro::kFlat && isShip(t.index[0]));
            if (t.index[0] >= evo::retro::kFlybyEngine)
                engineIndices.insert(t.index[0]);
        }
        for (const evo::retro::RetroPoint& p : scene.points)
            CHECK(isStar(p.index) && (p.size == 1 || p.size == 2));
        fewestTriangles = std::min(fewestTriangles, scene.triangles.size());
        fewestPoints = std::min(fewestPoints, scene.points.size());
        mostPoints = std::max(mostPoints, scene.points.size());
    }
    std::printf(
        "flyby: at least %zu ship triangles drawn, %zu..%zu stars, %zu engine shades over the chapter\n",
        fewestTriangles, fewestPoints, mostPoints, engineIndices.size());
    CHECK(fewestTriangles >= 20);     // a closed ship shows about half its faces
    CHECK(fewestPoints >= 300);       // most of the 512 stars, the ones behind the ship and off screen aside
    CHECK(engineIndices.size() >= 6); // the nozzles and flames flicker

    // The swoop: from behind and below the ship (nose along +z) to a three-quarter view of its right side,
    // above it.
    const evo::retro::Camera start = evo::retro::flybyCamera(flyby(0), 0);
    const evo::retro::Camera end = evo::retro::flybyCamera(flyby(duration() - .01), lastFrame());
    CHECK(start.eye[2] < 0 && start.eye[1] < 0 && std::abs(start.eye[0]) < -start.eye[2] / 4);
    CHECK(end.eye[0] > 0 && end.eye[0] > std::abs(end.eye[2]) / 2 && end.eye[1] > 0);
}

// The stage's index image of a FLYBY frame (scene and greeting).
class Fixture {
public:
    explicit Fixture(evo::gfx::Context& ctx)
        : ring_(ctx), text_(ctx, ring_), stage_(ctx, ring_, text_),
          buffer_(ctx, {kPixels, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "flyby test index readback"}) {}

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

// The share of the frame the ship covers.
double coverage(const std::vector<std::uint8_t>& indices) {
    return static_cast<double>(std::count_if(indices.begin(), indices.end(), isShip)) / kPixels;
}

void testImages(evo::gfx::Context& ctx) {
    Fixture fixture(ctx);

    // Over the whole chapter, every second of it (diagnostic: the range of the ship's coverage).
    double least = 1, most = 0;
    for (std::uint32_t retroFrame = 0; retroFrame <= lastFrame(); retroFrame += kRate) {
        const double share = coverage(fixture.render(flyby(timeIn(retroFrame))));
        least = std::min(least, share);
        most = std::max(most, share);
    }
    std::printf("flyby: the ship covers %.1f..%.1f %% of the frame, sampled every second\n", 100 * least,
                100 * most);

    for (double fraction : {.12, .5, .88}) {
        const std::uint32_t retroFrame = frameAt(fraction);
        bool rendered = false;
        const std::vector<std::uint8_t> image = fixture.render(flyby(timeIn(retroFrame, .1)), &rendered);
        CHECK(rendered && fixture.stage().retroFrame() == retroFrame);

        // Only black, the ship's ramps, the stars' greys and the greeting's white.
        for (std::uint8_t index : image)
            CHECK(index == 0 || index == 255 || isShip(index) || isStar(index));

        // The ship covers 5..40 % of the frame, mostly in its upper 70 % (clear of the epoch card).
        const double share = coverage(image);
        std::size_t ship = 0, upper = 0, stars = 0;
        for (std::uint32_t y = 0; y < kHeight; ++y)
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const std::uint8_t index = image[y * kWidth + x];
                ship += isShip(index);
                upper += isShip(index) && y < kHeight * 7 / 10;
                stars += isStar(index);
            }

        // No ship colour outside the ship's projected bounding box: the extent of its triangles' vertices.
        const RetroScene scene = sceneAt(retroFrame);
        CHECK(!scene.triangles.empty());
        std::int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
        for (const evo::retro::RetroTriangle& t : scene.triangles)
            for (int v = 0; v < 3; ++v) {
                minX = std::min(minX, t.x[v]), maxX = std::max(maxX, t.x[v]);
                minY = std::min(minY, t.y[v]), maxY = std::max(maxY, t.y[v]);
            }
        std::size_t outside = 0;
        for (std::uint32_t y = 0; y < kHeight; ++y)
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const std::int32_t cx = static_cast<std::int32_t>(x) * 16 + 8;
                const std::int32_t cy = static_cast<std::int32_t>(y) * 16 + 8;
                outside +=
                    isShip(image[y * kWidth + x]) && (cx < minX || cx > maxX || cy < minY || cy > maxY);
            }
        std::printf(
            "flyby: retro frame %u (%.0f %% through): ship %.1f %% of the frame (%.0f %% of it in the "
            "upper 70 %%), box (%d..%d, %d..%d) px, %zu star pixels of %zu stars\n",
            retroFrame, 100 * fraction, 100 * share, 100.0 * upper / std::max<std::size_t>(ship, 1),
            minX / 16, maxX / 16, minY / 16, maxY / 16, stars, scene.points.size());
        CHECK(share >= .05 && share <= .40);
        CHECK(upper >= ship * 9 / 10);
        CHECK(outside == 0);
        CHECK(stars >= 300);
        std::set<std::uint8_t> starShades, shipShades;
        for (std::uint8_t index : image) {
            if (isStar(index))
                starShades.insert(index);
            if (isShip(index))
                shipShades.insert(index);
        }
        CHECK(starShades.size() >= 6); // stars at many depths
        CHECK(shipShades.size() >= 6); // faces in several shades and ramps

        // Deterministic: the same retro frame at another moment within it, with the music signals pushed,
        // after another frame, gives the identical image.
        fixture.render(flyby(timeIn(retroFrame + 40)), &rendered);
        CHECK(rendered);
        TimelineFrame again = flyby(timeIn(retroFrame, .9));
        again.kick = 1;
        again.energy = 1;
        again.beatPhase = .5f;
        CHECK(fixture.render(again, &rendered) == image);
        CHECK(rendered);
    }

    // A second stage, fresh, renders the same retro frame identically.
    Fixture other(ctx);
    const std::uint32_t retroFrame = frameAt(.5);
    CHECK(other.render(flyby(timeIn(retroFrame))) == fixture.render(flyby(timeIn(retroFrame, .3))));
}

} // namespace

int main() {
    try {
        setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRegistry();
        testPalette();
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
    std::printf("flyby_test: %d checks passed\n", checksPassed);
    return 0;
}
