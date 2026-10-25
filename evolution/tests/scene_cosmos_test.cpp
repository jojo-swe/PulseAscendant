// The Cosmos scene (step 5, Task 24): GALAXY and ASCENDANT on lavapipe, with the validation layer on; fails on
// any message.
//
// (k) the cards: GALAXY's names 16,777,216 stars and 256x64x256 dust, ASCENDANT's 2,097,152 particles and
//     16,777,216 stars, and those are the full-detail configuration (the frames' counts, the capacities) and
//     the scene's own nowCard at any detail; the galaxy's frames splat 3x3 in the window;
// (a) also: the text waypoint (waypointTargets) holds every particle on its pixel of the line;
// (a) ASCENDANT's flight (the CPU twin): before its lift-off every particle is in the core, at the waypoint and
//     through its hold every particle is on its waypoint formation, on the accent every particle is on its
//     logo target, until the release it stays within the logo's sway, after it it has scattered;
// (m) the particle shader equals the CPU twin within 1e-4 for every particle of a 1/256 field at 9 times, and at
//     2 more with the text waypoint (the shader's font table and pixel walk against text::glyph's);
// (r) REBOOT's crossing: from its D the camera flies on into GALAXY's approach without a jump, and on its accent
//     the 1993 cube's 512 points are stars exactly where REBOOT's CRT drew them, frozen from then on;
// (d) the dust lanes: in GALAXY's edge-on approach the volume layer's transmittance is below 0.9 where the
//     CPU's dust (integrated along the pixel's ray) is thick, and above it where there is none;
// (p) checkChapterPictures for both chapters and (s) checkSceneSweep (scene_fixture.hpp).
//
// `scene_cosmos_test --look <dir> [spp] [detail]` renders one 480x270 still per chapter instead (GALAXY on
// its accent, ASCENDANT just after the landing), plus EVO_LOOK_AT=<chapter-local seconds> frames of either.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene.hpp"
#include "modern/scenes/cosmos.hpp"
#include "modern/scenes/logo_targets.hpp"
#include "modern/scenes/shots.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "rt/particles.hpp"
#include "show/director.hpp"
#include "show/timeline.hpp"
#include "text/font8x8.hpp"
#include "retro/boot.hpp"
#include "retro/dots.hpp"

#include "scene_fixture.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;
using modern::Camera;
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

void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

// --- (k) the cards
// -------------------------------------------------------------------------------------------

bool contains(const char* text, const char* part) {
    return std::string(text).find(part) != std::string::npos;
}

void testCards() {
    const modern::Cosmos full{modern::SceneDetail{1.f}};
    const show::TimelineFrame galaxy = at(chapterOf(show::Effect::Galaxy), 3.0);
    const show::TimelineFrame ascendant = at(chapterOf(show::Effect::Ascendant), 3.0);
    std::printf("(k) GALAXY: \"%s\"\n(k) ASCENDANT: \"%s\"\n", galaxy.nowCard, ascendant.nowCard);
    CHECK(contains(galaxy.nowCard, "16,777,216 STARS") && contains(galaxy.nowCard, "256×64×256 DUST"));
    CHECK(contains(ascendant.nowCard, "2,097,152 PARTICLES") && contains(ascendant.nowCard, "16,777,216 STARS"));
    CHECK(modern::kGalaxyStars == 16'777'216u && modern::kAscendantParticles == 2'097'152u);
    CHECK(modern::kGalaxyDust[0] == 256u && modern::kGalaxyDust[1] == 64u && modern::kGalaxyDust[2] == 256u);
    CHECK(full.starCount() == modern::kGalaxyStars && full.particleCount() == modern::kAscendantParticles);
    CHECK(full.dustSize() == modern::kGalaxyDust);
    const modern::SceneStatic& s = full.staticScene();
    CHECK(s.capacity.splats == modern::kGalaxyStars && s.capacity.particles == modern::kAscendantParticles);
    CHECK(s.capacity.mediumGrid == modern::kGalaxyDust);
    for (const show::TimelineFrame& f : {galaxy, ascendant}) {
        const modern::SceneFrame frame = full.frame(f);
        CHECK(frame.splats && frame.splats->count == modern::kGalaxyStars);
        CHECK(frame.splats->windowFootprint == 3);
        CHECK(frame.medium && frame.medium->grid && frame.medium->grid->size == modern::kGalaxyDust);
    }
    CHECK(full.frame(ascendant).particles->count == modern::kAscendantParticles);
    CHECK(!full.frame(galaxy).particles);
    const modern::Cosmos small{kTestDetail};
    CHECK(small.starCount() == 65536u && small.particleCount() == 8192u);
    CHECK(small.dustSize()[0] == 40u && small.dustSize()[1] == 10u);
    // The scene's own cards (what the screen shows) are the cue lines, their numbers the configuration's at
    // any detail.
    for (const modern::Cosmos* scene : {&full, &small}) {
        CHECK(scene->nowCard(show::Effect::Galaxy) == galaxy.nowCard);
        CHECK(scene->nowCard(show::Effect::Ascendant) == ascendant.nowCard);
        CHECK(scene->nowCard(show::Effect::Dots).empty());
    }
    const std::string stars = modern::groupThousands(modern::kGalaxyStars) + " STARS";
    CHECK(contains(full.nowCard(show::Effect::Galaxy).c_str(), stars.c_str()));
    CHECK(contains(full.nowCard(show::Effect::Ascendant).c_str(), stars.c_str()));
    CHECK(contains(full.nowCard(show::Effect::Ascendant).c_str(),
                   (modern::groupThousands(modern::kAscendantParticles) + " PARTICLES").c_str()));
    const std::string dust = std::to_string(modern::kGalaxyDust[0]) + "×" + std::to_string(modern::kGalaxyDust[1]) +
                             "×" + std::to_string(modern::kGalaxyDust[2]) + " DUST";
    CHECK(contains(full.nowCard(show::Effect::Galaxy).c_str(), dust.c_str()));
}

// --- (a) the flight
// ------------------------------------------------------------------------------------------

void testFlight() {
    const int chapter = chapterOf(show::Effect::Ascendant);
    const show::TimelineFrame head = at(chapter, 0.0);
    const modern::AscendantTimes t = modern::ascendantTimes(head);
    std::printf("(a) lift-offs %.3f + %.3f s, waypoint %.3f s (+%.4f), landing (accent) %.3f, rows from %.3f "
                "every %.3f s, release %.3f (show %.3f)\n",
                t.liftStart, t.liftSpread, t.waypoint, t.hold, t.accent, t.firstRow, t.bar, t.release,
                show::chapterStart(chapter) + t.release);
    // The waypoint one bar before D (643.96), the release on the chapter's last downbeat of the beat grid
    // (666.43; the directed 666.71 is no downbeat of the grid, whose bars here are 1.73 s).
    CHECK(std::abs(show::chapterStart(chapter) + t.waypoint - 643.96) < 0.05);
    CHECK(std::abs(show::chapterStart(chapter) + t.release - 666.43) < 0.05);
    CHECK(t.liftStart + t.liftSpread < t.waypoint && t.waypoint + t.hold < t.accent && t.accent < t.firstRow &&
          t.firstRow + 6 * t.bar < t.release);

    const std::uint32_t count = modern::kAscendantParticles;
    const modern::ParticleFrame p = modern::ascendantParticleFrame(at(chapter, t.accent), count);
    const float cell = modern::kLogoCell;
    const Vec3 core = modern::kAscendantGalaxyCentre;
    double worstLanding = 0, worstWaypoint = 0, farthestCore = 0;
    std::uint32_t scattered = 0;
    for (std::uint32_t id = 0; id < count; ++id) {
        const Vec3 target = modern::ascendantTarget(id, cell);
        const Vec3 way = modern::ascendantWaypoint(0, id, cell);
        const double landing = distance(v3(modern::ascendantParticle(id, t.accent, p.params).center), target);
        worstLanding = std::max(worstLanding, landing);
        for (const float w : {t.waypoint, t.waypoint + 0.5f * t.hold, t.waypoint + 0.999f * t.hold})
            worstWaypoint = std::max(worstWaypoint,
                                     double(distance(v3(modern::ascendantParticle(id, w, p.params).center), way)));
        if (id % 61 == 0) {
            farthestCore = std::max(
                farthestCore, double(distance(v3(modern::ascendantParticle(id, 0.5f, p.params).center), core)));
            // Between the landing and the release: within the sway (a few degrees) of the target.
            const Vec3 held = v3(modern::ascendantParticle(id, t.release - 0.01f, p.params).center);
            CHECK(distance(held, target) <= 0.12f * modern::length(target) + 1e-4f);
            scattered += distance(v3(modern::ascendantParticle(id, t.release + 0.8f, p.params).center), target) >
                         0.5f;
        }
    }
    std::printf("(a) %u particles: on the accent at most %.2e from their logo targets, at the waypoint %.2e "
                "from their formation, before lift-off within %.2f of the core; %u of %u sampled scattered "
                "after the release\n",
                count, worstLanding, worstWaypoint, farthestCore, scattered, (count + 60) / 61);
    CHECK(worstLanding == 0.0);
    CHECK(worstWaypoint == 0.0);
    CHECK(farthestCore < 40.0);
    CHECK(scattered > (count / 61) * 9 / 10);
    // A text waypoint (formation 1): every particle holds its place in the text, which is the font's.
    const modern::WaypointTargets text = modern::waypointTargets("Hi. yes, you.");
    CHECK(text.text == "HI. YES, YOU.");
    CHECK(modern::decodeWaypointText(text.packed).substr(0, 13) == "HI. YES, YOU.");
    std::uint32_t litExpected = 0;
    for (const char c : text.text)
        for (const std::uint8_t row : text::glyph(c))
            litExpected += std::uint32_t(std::popcount(row));
    CHECK(text.pixels.size() == litExpected);
    const modern::ParticleFrame tp = modern::ascendantParticleFrame(at(chapter, t.accent), count, &text);
    CHECK(tp.params[6] == 1.f);
    double worstText = 0;
    for (std::uint32_t id = 0; id < count; id += 13) {
        const Vec3 way = modern::ascendantWaypoint(1, id, cell, {tp.params[12], tp.params[13], tp.params[14], tp.params[15]});
        CHECK(distance(way, text.target(id, cell) + Vec3{0.f, 0.12f * 71.f * cell, -0.35f * 71.f * cell}) < 1e-6f);
        worstText = std::max(worstText, double(distance(v3(modern::ascendantParticle(id, t.waypoint + 0.5f * t.hold,
                                                                                            tp.params).center), way)));
        CHECK(distance(v3(modern::ascendantParticle(id, t.accent, tp.params).center), modern::ascendantTarget(id, cell)) ==
              0.f);
        // Inside its font pixel of the line.
        const std::uint32_t lit = std::uint32_t(text.pixels.size());
        const modern::LogoPixel px = text.pixels[(id % (lit * modern::kWaypointDepth)) % lit];
        const Vec3 local = text.target(id, cell);
        CHECK(std::abs(local.x / cell + text.centre.x - float(px.column)) <= 0.5001f);
        CHECK(std::abs(text.centre.y - local.y / cell - float(px.row)) <= 0.5001f);
    }
    CHECK(worstText == 0.0);
    std::printf("(a) the text waypoint \"%s\": %zu lit pixels, every particle on it through the hold\n",
                text.text.c_str(), text.pixels.size());
    // Every logo target lies inside its font pixel (the shared table, logo_targets.hpp).
    for (std::uint32_t id = 0; id < 50000; id += 7) {
        const Vec3 target = modern::ascendantTarget(id, cell);
        const modern::LogoPixel px =
            modern::logoPixel((id % (modern::kLogoPixels * modern::kLogoDepth)) % modern::kLogoPixels);
        CHECK(std::abs(target.x / cell + modern::kLogoCentreColumn - float(px.column)) <= 0.5001f);
        CHECK(std::abs(modern::kLogoCentreRow - target.y / cell - float(px.row)) <= 0.5001f);
    }
}

// --- (m) the shader against the CPU twin
// ----------------------------------------------------------------------

void testMirror(gfx::Context& ctx) {
    const int chapter = chapterOf(show::Effect::Ascendant);
    const modern::Cosmos scene{kTestDetail};
    const std::uint32_t count = scene.particleCount();
    gfx::FrameRing ring(ctx);
    rt::ParticleField field(ctx, ring, count, modern::kCosmosParticleShader);
    const modern::AscendantTimes t = modern::ascendantTimes(at(chapter, 0.0));
    const double times[] = {1.0,
                            t.liftStart + 0.5 * t.liftSpread,
                            t.waypoint - 0.3,
                            t.waypoint + 0.5 * t.hold,
                            0.5 * (t.waypoint + t.accent),
                            t.accent,
                            t.firstRow + 2.2 * t.bar,
                            t.release + 0.3,
                            t.release + 0.9};
    double worst = 0, worstEmission = 0;
    const modern::WaypointTargets text = modern::waypointTargets("HI. YES, YOU.");
    for (std::size_t pass = 0; pass < std::size(times) + 2; ++pass) {
        // The last two passes: the text waypoint (the shader's font table and pixel walk against the CPU's).
        const bool withText = pass >= std::size(times);
        const double local = withText ? (pass == std::size(times) ? t.waypoint + 0.5 * t.hold : t.waypoint - 0.4)
                                      : times[pass];
        const show::TimelineFrame f = at(chapter, local);
        const modern::SceneFrame frame = scene.frame(f);
        const modern::ParticleFrame p = withText ? modern::ascendantParticleFrame(f, count, &text) : *frame.particles;
        const float seconds = float(f.localSeconds), previous = float(f.localSeconds - 1.0 / 60.0);
        VkCommandBuffer cmd = ring.begin();
        field.record(cmd, p.count, f.localSeconds, f.localSeconds - 1.0 / 60.0, p.params, p.material);
        const VkDeviceSize bytes = VkDeviceSize(count) * sizeof(rt::SphereRecord);
        gfx::Buffer copy(ctx, {bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "cosmos test records"});
        const VkBufferCopy region{0, 0, bytes};
        vkCmdCopyBuffer(cmd, field.records().handle(), copy.handle(), 1, &region);
        gfx::bufferBarrier2(cmd, copy.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        ring.wait(ring.submit());
        idle(ctx);
        copy.invalidate();
        std::vector<rt::SphereRecord> gpu(count);
        std::memcpy(gpu.data(), copy.mapped(), bytes);
        for (std::uint32_t id = 0; id < count; ++id) {
            const rt::SphereRecord cpu = modern::ascendantParticle(id, seconds, p.params);
            const rt::SphereRecord before = modern::ascendantParticle(id, previous, p.params);
            const rt::SphereRecord& g = gpu[id];
            // Relative to the distance from the logo (the core is 320 units away).
            const double size = 1.0 + modern::length(v3(cpu.center)) / 10.0;
            const double e = std::max({double(distance(v3(g.center), v3(cpu.center))),
                                       double(distance(v3(g.prevCenter), v3(before.center))),
                                       std::abs(double(g.radius) - cpu.radius) * 10.0,
                                       double(distance(v3(g.color), v3(cpu.color)))}) /
                             size;
            const double emission = std::abs(double(g.emission) - cpu.emission) / std::max(1.0, double(cpu.emission));
            if (e > 1e-4 || emission > 1e-4)
                std::printf("  id %u at %.4f s: centre (%g %g %g) vs (%g %g %g), emission %g vs %g\n", id, seconds,
                            g.center[0], g.center[1], g.center[2], cpu.center[0], cpu.center[1], cpu.center[2],
                            g.emission, cpu.emission);
            CHECK(e <= 1e-4);
            CHECK(emission <= 1e-4);
            CHECK(g.material == p.material);
            worst = std::max(worst, e);
            worstEmission = std::max(worstEmission, emission);
        }
    }
    std::printf("(m) %u particles at 9 times and 2 with the text waypoint equal the CPU twin: worst %.2e "
                "(relative to the distance), emission %.2e\n",
                count, worst, worstEmission);
}

// --- (r) REBOOT's crossing into the galaxy
// -----------------------------------------------------------------

void testReboot() {
    const int galaxy = chapterOf(show::Effect::Galaxy);
    const double start = show::chapterStart(galaxy);
    const modern::RebootMarks m = modern::rebootMarks(at(galaxy, 0.0));
    std::printf("(r) REBOOT's crossing %.3f, accent %.3f (show seconds)\n", start + m.crossing, start + m.accent);
    CHECK(std::abs(start + m.crossing - 426.32) < 0.03 && std::abs(start + m.accent - 426.75) < 0.03);
    // From the crossing the director shows GALAXY at negative chapter time, and the camera flies on without a
    // jump into the chapter's own approach (shot 0) up to its cut.
    const float cut = modern::shots::barsOf(at(galaxy, 0.0)).at(-4);
    Camera previous{};
    double worstStep = 0;
    int frames = 0;
    for (double t = start + m.crossing; t < start + cut; t += 1.0 / 60.0, ++frames) {
        const show::TimelineFrame f = show::sceneFrame(show::sampleTimeline(t));
        CHECK(f.effect == show::Effect::Galaxy);
        const Camera c = modern::cosmosCamera(f);
        CHECK(c.shot == 0u && c.fovYDegrees == modern::shots::kLens85);
        if (frames > 0)
            worstStep = std::max(worstStep, double(distance(c.eye, previous.eye)));
        previous = c;
    }
    std::printf("(r) %d frames from the crossing to the cut, the eye at most %.2f units a frame\n", frames, worstStep);
    CHECK(worstStep < 2.2 && frames > 600);
    // On the accent the 1993 cube's 512 points are stars where REBOOT's CRT showed them: the 2026 camera sees
    // each star in the direction DOTS' camera saw the point (its view-space x / z and y / z), and from then on
    // the cube is frozen.
    const show::TimelineFrame outer = show::sampleTimeline(start + m.accent + 1e-4);
    const show::TimelineFrame f = show::sceneFrame(outer);
    const modern::Cosmos scene{kTestDetail};
    const modern::SplatFrame splats = *scene.frame(f).splats;
    const Camera c = modern::cosmosCamera(f);
    const modern::CameraBasis basis = modern::cameraBasis(c);
    const auto retroFrame = std::uint32_t(std::floor(outer.localSeconds * 35.0));
    const retro::DotsPoints points =
        retro::dotsShapePoints(retro::DotsShape::Cube, retroFrame - retro::rebootCubeFrame(outer));
    double worst = 0;
    for (std::uint32_t k = 0; k < 512; ++k) {
        const Vec3 d = modern::galaxyCubeStar(k, splats.params) - c.eye;
        const double z = modern::dot(d, basis.forward);
        CHECK(z > 0);
        const double x26 = modern::dot(d, basis.right) / z, y26 = modern::dot(d, basis.up) / z;
        const double z93 = points[k][2] + 6.0;
        const double x93 = points[k][0] / z93, y93 = points[k][1] / z93;
        if (k % 73 == 0 && std::getenv("EVO_CUBE_DEBUG"))
            std::printf("  cube %u: 2026 (%.4f %.4f) 1993 (%.4f %.4f)\n", k, x26, y26, x93, y93);
        worst = std::max({worst, std::abs(x26 - x93), std::abs(y26 - y93)});
    }
    std::printf("(r) the cube's stars on the accent: at most %.2e from the directions the CRT drew them in\n", worst);
    CHECK(worst < 1e-4);
    // ASCENDANT's white-out is post::Look's arc: the scene's exposure is flat.
    const int ascendant = chapterOf(show::Effect::Ascendant);
    for (double t = 0.0; t < at(ascendant, 0.0).chapterDuration; t += 0.25)
        CHECK(modern::cosmosExposureEV(at(ascendant, t)) == modern::cosmosExposureEV(at(ascendant, 0.0)));
    const modern::SplatFrame later = *scene.frame(show::sceneFrame(show::sampleTimeline(start + m.accent + 1.0))).splats;
    CHECK(later.params[14] == splats.params[14] && later.params[15] == splats.params[15]);
}

// --- (d) the dust lanes
// --------------------------------------------------------------------------------------

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

void testDustLanes(gfx::Context& ctx) {
    constexpr std::uint32_t kW = 160, kH = 90;
    const int chapter = chapterOf(show::Effect::Galaxy);
    const double local = 4.0; // the edge-on approach (shot 0)
    const show::TimelineFrame f = at(chapter, local);
    const modern::SceneDetail detail{1.f / 64};
    const modern::Cosmos scene{detail};
    const Camera camera = scene.camera(f);
    CHECK(camera.shot == 0);

    render::RenderSettings settings{kW, kH};
    settings.hud = false;
    settings.mode = render::RenderMode::Export;
    settings.spp = 2;
    settings.detail = detail;
    gfx::FrameRing ring(ctx);
    std::vector<float> transmittance(std::size_t(kW) * kH);
    {
        render::Renderer renderer(ctx, ring, settings);
        const double times[] = {show::chapterStart(chapter) + local};
        renderer.prepare(times);
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, f);
        gfx::Image& volume = renderer.modern().tracer()->gbuffer().volume;
        gfx::Buffer buffer(ctx, {VkDeviceSize(kW) * kH * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "volume"});
        volume.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                          VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {kW, kH, 1};
        vkCmdCopyImageToBuffer(cmd, volume.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle(), 1,
                               &region);
        gfx::bufferBarrier2(cmd, buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        ring.wait(ring.submit());
        idle(ctx);
        buffer.invalidate();
        const auto* half = static_cast<const std::uint16_t*>(buffer.mapped());
        for (std::size_t i = 0; i < transmittance.size(); ++i)
            transmittance[i] = halfToFloat(half[4 * i + 3]);
    }
    // The CPU's optical depth along each pixel's centre ray through the dust's box.
    const float turn = modern::galaxyTurn(f);
    std::uint32_t lane = 0, laneDark = 0, clear = 0, clearBright = 0;
    double laneMean = 0, clearMean = 0;
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            const Vec3 d = pixelDirection(camera, kW, kH, x, y);
            double tau = 0;
            const float step = 0.1f;
            for (float s = 200.f; s < 800.f; s += step) {
                const Vec3 p = camera.eye + d * s;
                if (std::abs(p.y) < 7.5f)
                    tau += double(modern::galaxyDustDensity(p, turn)) * step;
            }
            const float t = transmittance[std::size_t(y) * kW + x];
            if (tau > 1.5) {
                ++lane;
                laneDark += t < 0.9f;
                laneMean += t;
            } else if (tau < 0.01) {
                ++clear;
                clearBright += t > 0.9f;
                clearMean += t;
            }
        }
    laneMean /= std::max(lane, 1u);
    clearMean /= std::max(clear, 1u);
    std::printf("(d) edge-on at %.1f s: %u lane pixels (CPU optical depth > 1.5): T < 0.9 on %u, mean T %.3f; "
                "%u clear pixels: mean T %.3f\n",
                local, lane, laneDark, laneMean, clear, clearMean);
    CHECK(lane >= 40);
    CHECK(laneDark >= lane * 9 / 10);
    CHECK(laneMean < 0.6);
    CHECK(clearBright >= clear * 99 / 100);
}

// --- Look
// ---------------------------------------------------------------------------------------------------

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
    const int galaxy = chapterOf(show::Effect::Galaxy), ascendant = chapterOf(show::Effect::Ascendant);
    struct Shot {
        int chapter;
        double local;
        std::string name;
    };
    std::vector<Shot> shots = {{galaxy, at(galaxy, 0).accentSeconds, "galaxy_accent"},
                               {ascendant, at(ascendant, 0).accentSeconds + 1.5, "ascendant_logo"}};
    if (const char* list = std::getenv("EVO_LOOK_AT")) { // "g:4.0,a:8.2": chapter-local times of either
        shots.clear();
        std::string s(list);
        std::size_t pos = 0;
        while (pos < s.size()) {
            const std::size_t comma = std::min(s.find(',', pos), s.size());
            const std::string item = s.substr(pos, comma - pos);
            const int c = item[0] == 'a' ? ascendant : galaxy;
            shots.push_back({c, std::stod(item.substr(2)), std::string(item[0] == 'a' ? "ascendant_" : "galaxy_") +
                                                               item.substr(2)});
            pos = comma + 1;
        }
    }
    std::vector<double> times;
    for (const Shot& s : shots)
        times.push_back(show::chapterStart(s.chapter) + s.local);
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
        const std::filesystem::path path = dir / (shots[i].name + ".png");
        image::writePng(path, int(kW), int(kH), rgb.data());
        std::printf("look: %s (%.2f s into %s, mean %.3f) in %.1f s\n", path.string().c_str(), shots[i].local,
                    f.title, mean, secondsSince(start));
    }
    idle(ctx);
}

} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
    try {
        auto ctx = gfx::Context::create({.validation = true});
        if (!ctx.validationEnabled())
            std::printf("warning: validation layer unavailable; only the results are checked\n");
        if (argc >= 3 && std::string(argv[1]) == "--look") {
            renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 16u,
                       argc >= 5 ? std::stof(argv[4]) : 1.f);
            return ctx.validationMessageCount() == 0 ? 0 : 1;
        }
        const auto timed = [&](const char* name, auto&& run) {
            const auto t0 = std::chrono::steady_clock::now();
            run();
            std::printf("  [%s %.2f s]\n", name, secondsSince(t0));
        };
        timed("k", [&] { testCards(); });
        timed("a", [&] { testFlight(); });
        timed("m", [&] { testMirror(ctx); });
        timed("r", [&] { testReboot(); });
        timed("d", [&] { testDustLanes(ctx); });
        timed("s", [&] {
            test::SweepLimits limits;
            limits.maxCameraStep = 3.f;
            const test::SweepStats stats = test::checkSceneSweep(SceneId::Cosmos, limits);
            CHECK(stats.frames > 3000);
        });
        timed("p", [&] { test::checkChapterPictures(ctx, SceneId::Cosmos); });
        CHECK(ctx.validationMessageCount() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    std::printf("scene_cosmos: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
