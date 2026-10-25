// The Particles scene: DOTS and PARTICLE OCEAN on lavapipe, with the validation layer on;
// fails on any message.
//
// (k) the cards: each chapter's 2026 card (Particles::nowCard) is the show bible's line with the numbers from
//     the configuration, and the count before " TRACED PARTICLES" is 2,097,152, the full-detail particle count
//     (the scene's capacity and its frames' count; the DOTS cube a 128^3 lattice);
// (l) the logo table (src/modern/scenes/logo_targets.hpp and its GLSL twin) is the font's "EVOLUTION";
// (t) the 2026 schedule: the first two morphs land on retro::dotsSchedule's frames, the logo on D + 4 bars,
//     printed one row a beat over the 7 beats before; at every arrival each particle lies on its target (the
//     sphere's shells' radii within 1e-3, every shape's turned target exactly), between morphs it holds,
//     inside a morph only a band of the particles is in flight, and every logo row lands on its beat;
// (m) the shader's particles equal the C++ mirror (dotsParticle, oceanParticle) within 1e-4 (centre,
//     prevCenter, radius, colour; emission relative) for every particle of a 1/256 field at 10 times per
//     chapter;
// (v) under a static camera at a morph the motion guide is valid on >= 90 % of the particle pixels;
// (p) checkChapterPictures for both chapters and (s) checkSceneSweep (scene_fixture.hpp).
//
// `scene_particles_test --look <dir> [spp] [detail]` renders 480x270 stills of both chapters at several
// moments through the renderer (Export, forced 2026, no HUD) instead; `--memory` prepares the scene at full
// detail and prints the modern stage's measured bytes.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/card_format.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene.hpp"
#include "modern/scenes/logo_targets.hpp"
#include "modern/scenes/particles.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "retro/dots_schedule.hpp"
#include "rt/particles.hpp"
#include "show/timeline.hpp"
#include "text/font8x8.hpp"

#include "scene_fixture.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;
using modern::Camera;
using modern::DotsShape;
using modern::SceneId;
using modern::Vec3;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

constexpr modern::SceneDetail kTestDetail{1.f / 256};

int chapterOf(show::Effect effect) {
    for (int c = 0; c < show::chapterCount(); ++c)
        if (show::sampleTimeline(show::chapterStart(c)).effect == effect)
            return c;
    throw std::runtime_error("no such chapter");
}

// The chapter's frame at `local` seconds into it, forced into 2026.
show::TimelineFrame at(int chapter, double local) {
    show::TimelineFrame f = show::sampleTimeline(show::chapterStart(chapter) + local);
    f.era = 1.f;
    f.wipe = 1.f;
    f.eraCut = 0.f;
    return f;
}

Vec3 v3(const float (&a)[3]) {
    return {a[0], a[1], a[2]};
}

float distance(Vec3 a, Vec3 b) {
    return modern::length(a - b);
}

// --- (k) the cards
// ------------------------------------------------------------------------------------------

// The card's particle count: the number before " TRACED PARTICLES", thousands separators dropped.
std::uint64_t cardNumber(const std::string& text) {
    const std::size_t at = text.find(" TRACED PARTICLES");
    CHECK(at != std::string::npos);
    std::string digits;
    for (std::size_t i = at; i-- > 0;) {
        const char c = text[i];
        if (c >= '0' && c <= '9')
            digits.insert(digits.begin(), c);
        else if (c != ',')
            break;
    }
    std::uint64_t n = 0;
    for (const char c : digits)
        n = n * 10 + std::uint64_t(c - '0');
    return n;
}

void testCards() {
    const modern::Particles full{modern::SceneDetail{1.f}};
    const std::string dot = modern::kCardDot;
    const std::string expected[2] = {
        "2026" + dot + "2,097,152 TRACED PARTICLES" + dot + "MOTION BLUR" + dot + "DEPTH OF FIELD",
        "2026" + dot + "2,097,152 TRACED PARTICLES" + dot + "8 GERSTNER WAVES" + dot + "ONE SHOT"};
    int i = 0;
    for (const show::Effect effect : {show::Effect::Dots, show::Effect::Ocean}) {
        const int chapter = chapterOf(effect);
        const show::TimelineFrame f = at(chapter, 3.0);
        const std::string text = full.nowCard(effect);
        const std::uint64_t card = cardNumber(text);
        std::printf("(k) %s: card \"%s\" -> %llu\n", f.title, text.c_str(), static_cast<unsigned long long>(card));
        CHECK(text == expected[i++]);
        CHECK(card == 2'097'152u);
        const modern::SceneFrame frame = full.frame(f);
        CHECK(frame.particles.has_value());
        CHECK(frame.particles->count == card);
        CHECK(full.staticScene().capacity.particles == card);
    }
    CHECK(modern::kDotsParticles == 2'097'152u && modern::kOceanParticles == 2'097'152u);
    CHECK(full.dotsCount() == modern::kDotsParticles && full.oceanCount() == modern::kOceanParticles);
    CHECK(modern::dotsLatticeSide(modern::kDotsParticles) == 128u);
    // Detail scales the counts (never below the minimum).
    const modern::Particles small{kTestDetail};
    CHECK(small.dotsCount() == 8192u && small.oceanCount() == 8192u);
    CHECK(modern::particleCount(modern::kDotsParticles, {1e-6f}) == modern::kMinParticles);
    CHECK(modern::dotsLatticeSide(8192) == 20u && modern::dotsLatticeSide(8000) == 20u &&
          modern::dotsLatticeSide(7999) == 19u);
}

// --- (l) the logo
// -------------------------------------------------------------------------------------------

void testLogo() {
    std::uint32_t lit = 0, minCol = 99, maxCol = 0, minRow = 99, maxRow = 0;
    for (std::uint32_t g = 0; g < modern::kLogoGlyphs; ++g) {
        const text::GlyphRows& rows = text::glyph(modern::kLogoText[g]);
        for (std::uint32_t r = 0; r < modern::kLogoRows; ++r) {
            CHECK(modern::kLogoBitmap[r][g] == rows[r]);
            for (std::uint32_t c = 0; c < 8; ++c)
                if (rows[r] & (0x80u >> c)) {
                    ++lit;
                    minCol = std::min(minCol, g * 8 + c), maxCol = std::max(maxCol, g * 8 + c);
                    minRow = std::min(minRow, r), maxRow = std::max(maxRow, r);
                }
        }
    }
    CHECK(lit == modern::kLogoPixels);
    CHECK(float(minCol + maxCol) / 2.f == modern::kLogoCentreColumn);
    CHECK(float(minRow + maxRow) / 2.f == modern::kLogoCentreRow);
    // Every lit pixel once, in row-major order.
    std::uint32_t previous = 0;
    for (std::uint32_t j = 0; j < modern::kLogoPixels; ++j) {
        const modern::LogoPixel p = modern::logoPixel(j);
        const std::uint32_t key = p.row * modern::kLogoColumns + p.column;
        CHECK(j == 0 || key > previous);
        CHECK(modern::kLogoBitmap[p.row][p.column / 8] & (0x80u >> (p.column % 8)));
        previous = key;
    }
    // Every target inside its voxel.
    const float cell = modern::kDotsLogoCell;
    for (std::uint32_t id = 0; id < 20000; id += 7) {
        const Vec3 t = modern::logoTarget(id, cell);
        const std::uint32_t v = id % (modern::kLogoPixels * modern::kLogoDepth);
        const modern::LogoPixel p = modern::logoPixel(v % modern::kLogoPixels);
        const float x = t.x / cell + modern::kLogoCentreColumn, y = modern::kLogoCentreRow - t.y / cell;
        const float z = 3.f - t.z / cell;
        CHECK(std::abs(x - float(p.column)) <= 0.5001f && std::abs(y - float(p.row)) <= 0.5001f);
        CHECK(z >= float(v / modern::kLogoPixels) - 1e-4f && z <= float(v / modern::kLogoPixels + 1) + 1e-4f);
    }
    std::printf("(l) the logo: %u lit pixels of \"%s\", %u voxels deep\n", lit, modern::kLogoText,
                modern::kLogoDepth);
}

// --- (t) the morph schedule
// ---------------------------------------------------------------------------------

constexpr DotsShape kOrder[7] = {DotsShape::Cube, DotsShape::Sphere, DotsShape::Knot, DotsShape::Logo,
                                 DotsShape::Knot, DotsShape::Sphere, DotsShape::Cube};

void testSchedule() {
    const int chapter = chapterOf(show::Effect::Dots);
    const show::TimelineFrame head = at(chapter, 0.0);
    const auto retroSteps = retro::dotsSchedule(head);
    const auto steps = modern::dotsSchedule2026(head);
    const modern::DotsLanding landing = modern::dotsLanding(head);
    const std::uint32_t count = modern::particleCount(modern::kDotsParticles, kTestDetail);
    // The first two morphs are 1993's; the logo lands on D + 4 bars (40.73 in the show), after 7 beats; the
    // rest never come.
    for (std::size_t m = 0; m < 2; ++m) {
        CHECK(steps[m][0] == float(double(retroSteps[m].first) / retro::kDotsRetroRate));
        CHECK(steps[m][1] == float(double(retroSteps[m].arrival) / retro::kDotsRetroRate));
    }
    const double landingShow = show::chapterStart(chapter) + landing.seconds;
    std::printf("(t) the logo lands at %.3f s in the show (bar %.3f s)\n", landingShow, landing.bar);
    CHECK(std::abs(landingShow - 40.73) < 0.02);
    CHECK(steps[2][1] == landing.seconds && std::abs(steps[2][0] - (landing.seconds - 1.75f * landing.bar)) < 1e-4f);
    for (std::size_t m = 3; m < 6; ++m)
        CHECK(steps[m][0] == modern::kDotsNever && steps[m][1] == modern::kDotsNever);
    // The logo holds to the chapter's end.
    CHECK(steps[2][1] < head.chapterDuration);

    double worstShell = 0, worstOff = 0;
    for (std::size_t m = 0; m < 3; ++m) {
        const float first = steps[m][0], arrival = steps[m][1];
        const modern::ParticleFrame p = modern::dotsParticleFrame(at(chapter, arrival), count);
        CHECK(p.params[2 * m] == first && p.params[2 * m + 1] == arrival);
        const DotsShape shape = kOrder[m + 1];
        float farthest = 0;
        for (std::uint32_t id = 0; id < count; ++id) {
            const rt::SphereRecord r = modern::dotsParticle(id, arrival, p.params);
            const Vec3 target = modern::dotsShapePoint(shape, id, count, arrival);
            const float off = distance(v3(r.center), target);
            worstOff = std::max(worstOff, double(off));
            CHECK(off <= 1e-6f);
            if (shape == DotsShape::Sphere) {
                const std::uint32_t shell = id % modern::kDotsSphereShells;
                const float radius =
                    modern::kDotsSphereRadius * (1.f - modern::kDotsShellStep * float(shell));
                const double error = std::abs(double(modern::length(v3(r.center))) - radius);
                worstShell = std::max(worstShell, error);
                CHECK(error <= 1e-3);
            }
            farthest = std::max(farthest, modern::length(v3(r.center)));
        }
        // Half way through the morph a band is in flight: off both shapes, but far fewer than half.
        const float mid = (first + arrival) / 2;
        std::uint32_t off = 0, sampled = 0;
        for (std::uint32_t id = 0; id < count; id += 3, ++sampled) {
            const rt::SphereRecord r = modern::dotsParticle(id, mid, p.params);
            const float a = distance(v3(r.center), modern::dotsShapePoint(kOrder[m], id, count, mid));
            const float b = distance(v3(r.center), modern::dotsShapePoint(shape, id, count, mid));
            off += a > 0.01f && b > 0.01f;
        }
        std::printf("(t) morph %zu: %.1f %% in flight half way; arrives at %.4f s, farthest %.3f m\n", m,
                    100.0 * off / sampled, arrival, farthest);
        CHECK(off > sampled / 20 && off < sampled * 3 / 10);
        // Between this arrival and the next morph's first frame the shape holds.
        const float next = m + 1 < 3 ? steps[m + 1][0] : float(head.chapterDuration);
        if (next > arrival) {
            // (The logo's sparks, about one particle in 800, drift off it.)
            const float hold = (arrival + next) / 2;
            std::uint32_t away = 0, checked = 0;
            for (std::uint32_t id = 0; id < count; id += 17, ++checked)
                away += distance(v3(modern::dotsParticle(id, hold, p.params).center),
                                 modern::dotsShapePoint(shape, id, count, hold)) > 1e-6f;
            CHECK(m == 2 ? away <= checked / 100 : away == 0);
        }
    }
    // The logo prints one row a beat: row k lands on the k + 1-th beat after the morph's start.
    {
        const float first = steps[2][0], arrival = steps[2][1], beat = (arrival - first) / 7.f;
        const modern::ParticleFrame p = modern::dotsParticleFrame(at(chapter, arrival), count);
        constexpr std::uint32_t voxels = modern::kLogoPixels * modern::kLogoDepth;
        for (std::uint32_t row = 0; row < 7; ++row) {
            const float lands = first + float(row + 1) * beat;
            std::uint32_t landed = 0, flying = 0, members = 0;
            for (std::uint32_t id = 0; id < count; id += 5) {
                if (modern::logoPixel((id % voxels) % modern::kLogoPixels).row != row)
                    continue;
                ++members;
                const float t1 = lands + 0.002f, t0 = lands - 0.5f * beat;
                landed += distance(v3(modern::dotsParticle(id, t1, p.params).center),
                                   modern::dotsShapePoint(DotsShape::Logo, id, count, t1)) <= 1e-4f;
                flying += distance(v3(modern::dotsParticle(id, t0, p.params).center),
                                   modern::dotsShapePoint(DotsShape::Logo, id, count, t0)) > 1e-3f;
            }
            CHECK(members > 0 && landed == members && flying > members * 9 / 10);
        }
        std::printf("(t) the logo prints row by row, one row each %.3f s beat\n", beat);
    }
    std::printf("(t) worst distance from target %.2e, worst sphere shell radius error %.2e\n", worstOff,
                worstShell);
}

// --- (m) the shader against the C++ mirror
// ------------------------------------------------------------------

void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

void testMirror(gfx::Context& ctx) {
    const std::uint32_t count = modern::particleCount(modern::kDotsParticles, kTestDetail);
    gfx::FrameRing ring(ctx);
    rt::ParticleField field(ctx, ring, count, modern::kParticlesShader);
    double worst = 0, worstEmission = 0;
    for (const show::Effect effect : {show::Effect::Dots, show::Effect::Ocean}) {
        const int chapter = chapterOf(effect);
        const show::TimelineFrame head = at(chapter, 0.0);
        std::vector<double> times;
        if (effect == show::Effect::Dots) {
            // Morph middles and late flights (the streaks), arrivals, the sparks and the end.
            const auto steps = modern::dotsSchedule2026(head);
            for (std::size_t m = 0; m < 3; ++m) {
                const double first = steps[m][0], arrival = steps[m][1];
                times.push_back((first + arrival) / 2);
                times.push_back(first + 0.8 * (arrival - first));
                if (m < 2)
                    times.push_back(arrival + 0.3);
            }
            times.push_back(steps[2][1] + 0.7);
            times.push_back(head.chapterDuration - 0.2);
        } else {
            for (int i = 0; i < 10; ++i)
                times.push_back(head.chapterDuration * (0.03 + 0.1 * i));
        }
        CHECK(times.size() == 10);
        const modern::Particles scene{kTestDetail};
        for (const double local : times) {
            const show::TimelineFrame f = at(chapter, local);
            const modern::SceneFrame frame = scene.frame(f);
            const modern::ParticleFrame& p = *frame.particles;
            CHECK(p.count == count);
            const float seconds = float(f.localSeconds), previous = float(f.localSeconds - 1.0 / 60.0);
            VkCommandBuffer cmd = ring.begin();
            field.record(cmd, p.count, f.localSeconds, f.localSeconds - 1.0 / 60.0, p.params, p.material);
            const VkDeviceSize bytes = VkDeviceSize(count) * sizeof(rt::SphereRecord);
            gfx::Buffer copy(ctx, {bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "particles test records"});
            const VkBufferCopy region{0, 0, bytes};
            vkCmdCopyBuffer(cmd, field.records().handle(), copy.handle(), 1, &region);
            gfx::bufferBarrier2(cmd, copy.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                                VK_ACCESS_2_HOST_READ_BIT);
            ring.wait(ring.submit());
            idle(ctx);
            copy.invalidate();
            std::vector<rt::SphereRecord> gpu(count);
            std::memcpy(gpu.data(), copy.mapped(), bytes);
            for (std::uint32_t id = 0; id < count; ++id) {
                const rt::SphereRecord cpu = modern::particlesParticle(id, seconds, p.params);
                const rt::SphereRecord before = modern::particlesParticle(id, previous, p.params);
                const rt::SphereRecord& g = gpu[id];
                const double e = std::max({double(distance(v3(g.center), v3(cpu.center))),
                                           double(distance(v3(g.prevCenter), v3(before.center))),
                                           std::abs(double(g.radius) - cpu.radius),
                                           double(distance(v3(g.color), v3(cpu.color)))});
                const double emission =
                    std::abs(double(g.emission) - cpu.emission) / std::max(1.0, double(cpu.emission));
                if (e > 1e-4 || emission > 1e-4)
                    std::printf("  id %u at %.4f s: centre (%g %g %g) vs (%g %g %g), emission %g vs %g\n", id,
                                seconds, g.center[0], g.center[1], g.center[2], cpu.center[0], cpu.center[1],
                                cpu.center[2], g.emission, cpu.emission);
                CHECK(e <= 1e-4);
                CHECK(emission <= 1e-4);
                CHECK(g.material == p.material);
                worst = std::max(worst, e);
                worstEmission = std::max(worstEmission, emission);
            }
        }
        std::printf("(m) %s: %u particles at 10 times equal the C++ mirror\n", head.title, count);
    }
    std::printf("(m) worst difference %.2e m, emission %.2e relative\n", worst, worstEmission);
}

// --- (v) the motion guide
// -----------------------------------------------------------------------------------

// The Particles scene under a fixed camera.
class StaticCamera final : public modern::ModernScene {
public:
    StaticCamera(modern::SceneDetail detail, const Camera& camera) : inner_(detail), camera_(camera) {}
    const modern::SceneStatic& staticScene() const override { return inner_.staticScene(); }
    modern::SceneFrame frame(const show::TimelineFrame& f) const override {
        modern::SceneFrame frame = inner_.frame(f);
        frame.camera = camera_;
        return frame;
    }
    Camera camera(const show::TimelineFrame&) const override { return camera_; }
    bool samplesRetro() const override { return false; }

private:
    modern::Particles inner_;
    Camera camera_;
};

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1Fu, mantissa = h & 0x3FFu;
    float value;
    if (exponent == 0)
        value = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31)
        value = mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else
        value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    return sign ? -value : value;
}

// An RGBA16F or R32F image read back as floats (4 or 1 per pixel), recorded into cmd.
struct Read {
    gfx::Buffer buffer;
    VkFormat format;
    std::uint32_t width, height;
    std::vector<float> values() const {
        buffer.invalidate();
        const std::size_t pixels = std::size_t(width) * height;
        std::vector<float> out;
        if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
            out.resize(pixels * 4);
            const auto* half = static_cast<const std::uint16_t*>(buffer.mapped());
            for (std::size_t i = 0; i < out.size(); ++i)
                out[i] = halfToFloat(half[i]);
        } else {
            out.resize(pixels * (format == VK_FORMAT_R32_SFLOAT ? 1 : 4));
            std::memcpy(out.data(), buffer.mapped(), out.size() * sizeof(float));
        }
        return out;
    }
};

Read recordRead(gfx::Context& ctx, VkCommandBuffer cmd, gfx::Image& image) {
    const VkFormat format = image.format();
    const std::uint32_t bytesPerPixel = format == VK_FORMAT_R32G32B32A32_SFLOAT   ? 16
                                        : format == VK_FORMAT_R16G16B16A16_SFLOAT ? 8
                                        : format == VK_FORMAT_R32_SFLOAT          ? 4
                                                                                  : 0;
    if (bytesPerPixel == 0)
        throw std::logic_error("recordRead: unexpected format");
    const VkExtent2D extent = image.extent();
    Read read{gfx::Buffer(ctx, {VkDeviceSize(extent.width) * extent.height * bytesPerPixel,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "particles test readback"}),
              format, extent.width, extent.height};
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, read.buffer.handle(), 1,
                           &region);
    gfx::bufferBarrier2(cmd, read.buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                        VK_ACCESS_2_HOST_READ_BIT);
    return read;
}

// The camera ray through a pixel centre (pinhole), as pathtrace.rgen's cameraDirection.
Vec3 pixelDirection(const Camera& camera, std::uint32_t width, std::uint32_t height, std::uint32_t x,
                    std::uint32_t y) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const float tanHalf = std::tan(camera.fovYDegrees * 3.14159265f / 360.f);
    const float aspect = float(width) / float(height);
    const float sx = (2.f * (float(x) + 0.5f) / float(width) - 1.f) * tanHalf * aspect;
    const float sy = (1.f - 2.f * (float(y) + 0.5f) / float(height)) * tanHalf;
    return modern::normalize(b.forward + b.right * sx + b.up * sy);
}

void testMotionGuide(gfx::Context& ctx) {
    constexpr std::uint32_t kW = 160, kH = 90;
    Camera camera;
    camera.eye = modern::kDotsCentre + Vec3{0.6f, 0.35f, 3.f};
    camera.target = modern::kDotsCentre;
    camera.up = {0.f, 1.f, 0.f};
    camera.fovYDegrees = 38.f;
    const int chapter = chapterOf(show::Effect::Dots);
    const auto steps = retro::dotsSchedule(at(chapter, 0.0));
    const double first = double(steps[0].first) / retro::kDotsRetroRate;
    const double arrival = double(steps[0].arrival) / retro::kDotsRetroRate;
    const double t0 = (first + arrival) / 2;

    render::RenderSettings settings{kW, kH};
    settings.hud = false;
    settings.mode = render::RenderMode::Interactive;
    settings.spp = 1;
    settings.detail = kTestDetail;
    settings.sceneFactory = [camera](SceneId id,
                                     modern::SceneDetail detail) -> std::unique_ptr<modern::ModernScene> {
        if (id == SceneId::Particles)
            return std::make_unique<StaticCamera>(detail, camera);
        return modern::makeScene(id, detail);
    };
    gfx::FrameRing ring(ctx);
    std::vector<float> motion, depth;
    {
        render::Renderer renderer(ctx, ring, settings);
        const double times[] = {show::chapterStart(chapter) + t0,
                                show::chapterStart(chapter) + t0 + 1.0 / 60.0};
        renderer.prepare(times);
        for (int i = 0; i < 2; ++i) {
            VkCommandBuffer cmd = ring.begin();
            show::TimelineFrame f = show::sampleTimeline(times[i]);
            f.era = 1.f;
            f.wipe = 1.f;
            f.eraCut = 0.f;
            renderer.render(cmd, f);
            if (i == 1) {
                modern::GBuffer& g = renderer.modern().tracer()->gbuffer();
                Read m = recordRead(ctx, cmd, g.motion), d = recordRead(ctx, cmd, g.depth);
                ring.wait(ring.submit());
                idle(ctx);
                motion = m.values();
                depth = d.values();
            } else {
                ring.wait(ring.submit());
            }
        }
        idle(ctx);
    }
    const std::size_t depthChannels = depth.size() / (std::size_t(kW) * kH);
    const modern::CameraBasis basis = modern::cameraBasis(camera);
    std::uint32_t particlePixels = 0, valid = 0;
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = std::size_t(y) * kW + x;
            const float z = depth[i * depthChannels];
            if (!(z > 0.f))
                continue;
            const Vec3 d = pixelDirection(camera, kW, kH, x, y);
            const Vec3 p = camera.eye + d * (z / modern::dot(d, basis.forward));
            if (distance(p, modern::kDotsCentre) > 1.6f || p.y < 0.05f)
                continue; // the floor or the lights
            ++particlePixels;
            const float* mv = &motion[i * 4];
            valid += mv[2] == 1.f && std::isfinite(mv[0]) && std::isfinite(mv[1]);
        }
    const double share = particlePixels ? double(valid) / particlePixels : 0.0;
    std::printf("(v) static camera at %.3f s (cube -> sphere): %u particle pixels, motion valid on %.1f %%\n",
                t0, particlePixels, 100.0 * share);
    CHECK(particlePixels > kW * kH / 20);
    CHECK(share >= 0.9);
}

// --- Look and memory
// ----------------------------------------------------------------------------------------

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp, float detail) {
    constexpr std::uint32_t kW = 480, kH = 270;
    render::RenderSettings settings{kW, kH};
    settings.hud = false;
    settings.mode = render::RenderMode::Export;
    settings.spp = spp;
    settings.detail = {detail};
    gfx::FrameRing ring(ctx);
    gfx::Readback readback(ctx, kW, kH);
    render::Renderer renderer(ctx, ring, settings);
    struct Shot {
        show::Effect effect;
        double local;
        const char* name;
    };
    const int dots = chapterOf(show::Effect::Dots), ocean = chapterOf(show::Effect::Ocean);
    const show::TimelineFrame dotsHead = at(dots, 0.0), oceanHead = at(ocean, 0.0);
    const auto steps = modern::dotsSchedule2026(dotsHead);
    const auto mid = [&](std::size_t m, double s) { return steps[m][0] + s * (steps[m][1] - steps[m][0]); };
    std::vector<Shot> shots = {
        {show::Effect::Dots, 0.9, "dots_1_cube"},
        {show::Effect::Dots, mid(0, 0.5), "dots_2_cube_to_sphere"},
        {show::Effect::Dots, steps[0][1] + 0.6, "dots_3_sphere"},
        {show::Effect::Dots, mid(1, 0.55), "dots_4_sphere_to_knot"},
        {show::Effect::Dots, steps[1][1] + 3.5, "dots_5_knot_orbit"},
        {show::Effect::Dots, mid(2, 0.6), "dots_6_knot_to_logo"},
        {show::Effect::Dots, steps[2][1] + 0.4, "dots_7_logo_ring"},
        {show::Effect::Dots, dotsHead.chapterDuration - 0.3, "dots_8_logo_sparks"},
        {show::Effect::Ocean, 2.0, "ocean_1_skim"},
        {show::Effect::Ocean, 0.4 * oceanHead.accentSeconds + 3.0, "ocean_2_skim"},
        {show::Effect::Ocean, oceanHead.accentSeconds + 2.5, "ocean_3_rise"},
        {show::Effect::Ocean, oceanHead.chapterDuration - 1.0, "ocean_4_reveal"},
    };
    if (const char* local = std::getenv("EVO_LOOK_DOTS_AT")) // a development aid: one DOTS frame
        shots = {{show::Effect::Dots, std::stod(local), "dots_at"}};
    if (const char* only = std::getenv("EVO_LOOK_ONLY")) {
        std::vector<Shot> kept;
        for (const Shot& s : shots)
            if (std::string(s.name).find(only) != std::string::npos)
                kept.push_back(s);
        shots = kept;
    }
    std::vector<double> times;
    for (const Shot& s : shots)
        times.push_back(show::chapterStart(s.effect == show::Effect::Dots ? dots : ocean) + s.local);
    renderer.prepare(times);
    std::filesystem::create_directories(dir);
    for (std::size_t i = 0; i < shots.size(); ++i) {
        const auto start = std::chrono::steady_clock::now();
        show::TimelineFrame f = show::sampleTimeline(times[i]);
        f.era = 1.f;
        f.wipe = 1.f;
        f.eraCut = 0.f;
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, f);
        readback.record(cmd, renderer.output());
        ring.wait(ring.submit());
        const std::vector<std::uint8_t> rgb = readback.rgb();
        double mean = 0;
        for (std::size_t p = 0; p < rgb.size(); p += 3)
            mean += (0.2126 * rgb[p] + 0.7152 * rgb[p + 1] + 0.0722 * rgb[p + 2]) / 255.0;
        mean /= double(rgb.size() / 3);
        const std::filesystem::path path = dir / (std::string(shots[i].name) + ".png");
        image::writePng(path, int(kW), int(kH), rgb.data());
        std::printf("look: %s (%.2f s into %s, mean %.3f) in %.1f s\n", path.string().c_str(), shots[i].local,
                    f.title, mean, secondsSince(start));
    }
    idle(ctx);
}

void printMemory(gfx::Context& ctx, float detail) {
    render::RenderSettings settings{640, 360};
    settings.hud = false;
    settings.detail = {detail};
    gfx::FrameRing ring(ctx);
    render::Renderer renderer(ctx, ring, settings);
    const double times[] = {show::chapterStart(chapterOf(show::Effect::Dots)) + 1.0};
    renderer.prepare(times);
    const rt::ParticleField* field = renderer.modern().particles();
    std::printf("memory at detail %g: modern stage %.1f MB, of which the particle field (%u particles, two "
                "slots) %.1f MB\n",
                detail, double(renderer.modern().gpuBytes()) / 1e6, field ? field->capacity() : 0u,
                field ? double(field->bytes()) / 1e6 : 0.0);
}

} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
    try {
        auto ctx = gfx::Context::create({.validation = true});
        if (!ctx.validationEnabled())
            std::printf("warning: validation layer unavailable; only the results are checked\n");
        if (argc >= 3 && std::string(argv[1]) == "--look") {
            renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 16u,
                       argc >= 5 ? std::stof(argv[4]) : 1.f);
            return ctx.validationMessageCount() == 0 ? 0 : 1;
        }
        if (argc >= 2 && std::string(argv[1]) == "--memory") {
            printMemory(ctx, argc >= 3 ? std::stof(argv[2]) : 1.f);
            return ctx.validationMessageCount() == 0 ? 0 : 1;
        }
        const auto timed = [&](const char* name, auto&& run) {
            const auto t0 = std::chrono::steady_clock::now();
            run();
            std::printf("  [%s %.2f s]\n", name, secondsSince(t0));
        };
        timed("k", [&] { testCards(); });
        timed("l", [&] { testLogo(); });
        timed("t", [&] { testSchedule(); });
        timed("m", [&] { testMirror(ctx); });
        timed("v", [&] { testMotionGuide(ctx); });
        timed("s", [&] {
            test::SweepLimits limits;
            limits.maxCameraStep = 0.5f;
            const test::SweepStats stats = test::checkSceneSweep(SceneId::Particles, limits);
            CHECK(stats.frames > 2000);
        });
        timed("p", [&] { test::checkChapterPictures(ctx, SceneId::Particles); });
        CHECK(ctx.validationMessageCount() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    std::printf("scene_particles: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
