// The Spheres scene (step 5, Task 22): RAYTRACE, SPHERE STORM and RAYTRACE INFINITY. With the validation
// layer and its synchronization validation on; fails on any message it reports.
//   (a) the map: exactly the three chapters show Spheres;
//   (b) the cards: every count the three 2026 cards state is the configuration's (RAYTRACE's
//       "60 FPS" is a frame rate, which --bench measures on the target GPU); the configuration is 48 balls (6
//       from 1993) and a colossal one, 32,768 storm particles of which 4,096 glow, and the 32,768 lattice
//       spheres round the camera;
//   (c) the 1993 mirror: the first six balls equal retro::raytraceSpheres at the same time (the scene mirrors
//       it: evo_modern cannot link the retro code), ball 3 is the glass one, and until the accent the camera
//       is retro::raytraceCamera;
//   (d) the sweep: checkSceneSweep over the three chapters every 1/60 s; RAYTRACE's balls never sink into the
//       board, overlap or come near the camera; the camera never comes near a lattice sphere or (sampled
//       every tenth frame at full detail) a storm particle;
//   (e) the particle shader equals its C++ mirror (spheresParticle) for every particle: the storm at five
//       times, a kick and an energy among them, and the lattice at three cameras;
//   (f) the fixture's chapter pictures (20 %, accent, 80 %);
//   (g) physical: in RAYTRACE before the accent, the central chrome sphere's projected disc reflects the
//       board, both its red and its white squares;
//   (i) the drop (the bar line at or before each accent): RAYTRACE's 42 extra balls rest, hop higher and
//       higher over the four bars before it, leap together on it and land together on the next bar line; the
//       storm's funnel lies flat until then and has risen a bar later; the lattice's lanterns are dark before
//       it and lit after; every cut falls on a bar line;
//   (h) SPHERE STORM at full detail (32,768 particles, 4,096 glowing): the glowing ones are the frame's 4,096
//       tree lights (one light of kind Tree stands for them), each at its particle; the picture glows.
// `spheres_scene_test --look <dir> [spp] [width height] [name]` renders stills of the three chapters instead
// (those whose name contains `name`).

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/scene.hpp"
#include "modern/scenes/spheres.hpp"
#include "render/renderer.hpp"
#include "retro/raytrace.hpp"
#include "rt/particles.hpp"
#include "show/timeline.hpp"

#include "scene_fixture.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace gfx = evo::gfx;
namespace modern = evo::modern;
namespace render = evo::render;
namespace retro = evo::retro;
namespace rt = evo::rt;
namespace show = evo::show;
using modern::SceneId;
using modern::Vec3;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr int kRaytrace = 7, kStorm = 17, kInfinity = 26;

void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

double chapterTime(int chapter, double fraction) {
    const double start = show::chapterStart(chapter);
    return start + fraction * (show::chapterStart(chapter + 1) - start);
}

double accentTime(int chapter) {
    const double start = show::chapterStart(chapter);
    return start + show::sampleTimeline(start).accentSeconds;
}

show::TimelineFrame forced2026(double seconds) {
    show::TimelineFrame frame = show::sampleTimeline(seconds);
    frame.era = 1.f;
    frame.wipe = 1.f;
    frame.eraCut = 0.f;
    return frame;
}

Vec3 centreOf(const rt::Mat34& m) {
    return {m.m[0][3], m.m[1][3], m.m[2][3]};
}

// --- (a) the map, (b) the cards
// ------------------------------------------------------------------------------

void testMap() {
    const std::vector<int> chapters = evo::test::chaptersOf(SceneId::Spheres);
    CHECK((chapters == std::vector<int>{kRaytrace, kStorm, kInfinity}));
    CHECK(show::sampleTimeline(show::chapterStart(kRaytrace)).effect == show::Effect::Raytrace);
    CHECK(show::sampleTimeline(show::chapterStart(kStorm)).effect == show::Effect::SphereStorm);
    CHECK(show::sampleTimeline(show::chapterStart(kInfinity)).effect == show::Effect::RaytraceInfinity);
    CHECK(std::string(modern::effectSceneName(show::Effect::SphereStorm)) == "Spheres");
}

// The numbers a card states ("2,097,152", "60"), each with the word after it.
struct CardNumber {
    long value;
    std::string unit;
};

std::vector<CardNumber> cardNumbers(const std::string& card) {
    std::vector<CardNumber> numbers;
    for (std::size_t i = 0; i < card.size();) {
        if (card[i] < '0' || card[i] > '9') {
            ++i;
            continue;
        }
        long value = 0;
        while (i < card.size() &&
               ((card[i] >= '0' && card[i] <= '9') ||
                (card[i] == ',' && i + 1 < card.size() && card[i + 1] >= '0' && card[i + 1] <= '9'))) {
            if (card[i] != ',')
                value = 10 * value + (card[i] - '0');
            ++i;
        }
        while (i < card.size() && card[i] == ' ')
            ++i;
        std::string unit;
        while (i < card.size() && card[i] >= 'A' && card[i] <= 'Z')
            unit += card[i++];
        numbers.push_back({value, unit});
    }
    return numbers;
}

void testCards() {
    // The configuration.
    const modern::Spheres full;
    CHECK(modern::kRaytraceSphereCount == 48 && modern::kRaytraceRetroSpheres == retro::kRaytraceSpheres);
    CHECK(modern::stormCount({1.f}) == 32768 && modern::stormEmissiveCount({1.f}) == 4096);
    CHECK(modern::stormCount({1.f}) == modern::kStormSpheres && modern::kStormEmissive == 4096);
    CHECK(full.staticScene().capacity.particles == modern::kStormSpheres);
    CHECK(modern::kInfinitySpheres == 32768 && modern::infinitySide({1.f}) == 32);
    const auto& ranges = full.staticScene().sphereRanges;
    const std::uint32_t staticSpheres = modern::kRaytraceSphereCount + modern::kRaytraceGiants;
    CHECK(staticSpheres == 49 && ranges.size() == staticSpheres);
    CHECK(full.staticScene().spheres.size() == staticSpheres);
    for (std::uint32_t i = 0; i < staticSpheres; ++i)
        CHECK(ranges[i].first == i && ranges[i].count == 1);
    // The test detail shrinks the particles only.
    CHECK(modern::stormCount({1.f / 256}) == 128 && modern::stormEmissiveCount({1.f / 256}) == 16);
    CHECK(modern::infinitySide({1.f / 256}) == 5);
    CHECK(modern::Spheres({1.f / 256}).staticScene().capacity.particles == 128);

    // The cards: a count must be the configuration's; a frame rate is measured with --bench.
    const std::vector<long> counts = {modern::kRaytraceSphereCount + modern::kRaytraceGiants,
                                      modern::kStormSpheres, modern::kStormEmissive,
                                      modern::kInfinitySpheres};
    int stated = 0;
    int frameRates = 0;
    for (const int chapter : {kRaytrace, kStorm, kInfinity}) {
        const std::string card = show::sampleTimeline(show::chapterStart(chapter)).nowCard;
        CHECK(!card.empty());
        for (const CardNumber& n : cardNumbers(card)) {
            if (n.value == 2026 && n.unit.empty())
                continue; // the year
            if (n.unit == "FPS") {
                ++frameRates;
                continue;
            }
            std::printf("card \"%s\": %ld %s\n", card.c_str(), n.value, n.unit.c_str());
            CHECK(std::find(counts.begin(), counts.end(), n.value) != counts.end());
            ++stated;
        }
    }
    CHECK(frameRates == 1); // RAYTRACE's "60 FPS"
    CHECK(stated == 4);     // 49 spheres; 32,768 and 4,096; 32,768
    // The parser itself, on a card with a count.
    const std::vector<CardNumber> dots = cardNumbers("2026 · 2,097,152 PARTICLES");
    CHECK(dots.size() == 2 && dots[1].value == 2097152 && dots[1].unit == "PARTICLES");
}

// --- (c) the 1993 mirror
// -------------------------------------------------------------------------------------

void testRetroMirror() {
    for (int k = 0; k <= 400; ++k) {
        const double seconds = 0.0853 * k;
        const auto retroBalls = retro::raytraceSpheres(seconds);
        const auto balls = modern::raytraceRetroBalls(seconds);
        for (int i = 0; i < retro::kRaytraceSpheres; ++i) {
            CHECK(balls[i].centre.x == float(retroBalls[i].centre[0]) &&
                  balls[i].centre.y == float(retroBalls[i].centre[1]) &&
                  balls[i].centre.z == float(retroBalls[i].centre[2]));
            CHECK(balls[i].radius == float(retroBalls[i].radius));
        }
    }
    CHECK(modern::kRaytraceGlassBall == std::uint32_t(retro::kRaytraceGlassSphere));
    const retro::Camera r = retro::raytraceCamera();
    const modern::Camera c = modern::raytraceRetroCamera();
    CHECK(c.eye.x == float(r.eye[0]) && c.eye.y == float(r.eye[1]) && c.eye.z == float(r.eye[2]));
    CHECK(c.target.x == float(r.target[0]) && c.target.y == float(r.target[1]) &&
          c.target.z == float(r.target[2]));
    CHECK(c.up.x == float(r.up[0]) && c.up.y == float(r.up[1]) && c.up.z == float(r.up[2]));
    CHECK(c.fovYDegrees == float(r.fovYDegrees) && c.aperture == 0.f);

    // In the scene: before the accent the camera is 1993's and the balls 1993's (instances 1..6 after the
    // floor); ball 3 is glass, the rest of the six chrome.
    const modern::Spheres scene({1.f / 256});
    const modern::SceneStatic& statics = scene.staticScene();
    CHECK(statics.materials[statics.spheres[3].material].materialClass ==
          std::uint32_t(rt::MaterialClass::Glass));
    for (const std::uint32_t i : {0u, 1u, 2u, 4u, 5u})
        CHECK(statics.spheres[i].material == modern::Spheres::kChrome);
    const double start = show::chapterStart(kRaytrace);
    const double accent = accentTime(kRaytrace);
    const double drop = start + modern::spheresDrop(show::sampleTimeline(start)).seconds;
    CHECK(drop <= accent && drop > start);
    int frames = 0;
    for (double t = start; t < drop; t += 0.37) {
        const show::TimelineFrame f = show::sampleTimeline(t);
        const modern::SceneFrame frame = scene.frame(f);
        CHECK(frame.camera.shot == 0);
        CHECK(frame.camera.eye.x == c.eye.x && frame.camera.eye.y == c.eye.y &&
              frame.camera.eye.z == c.eye.z);
        CHECK(frame.camera.fovYDegrees == c.fovYDegrees);
        const auto retroBalls = retro::raytraceSpheres(f.localSeconds);
        for (std::uint32_t i = 0; i < modern::kRaytraceRetroSpheres; ++i) {
            const modern::SceneInstance& inst = frame.instances[1 + i];
            CHECK(inst.kind == rt::GeometryKind::Spheres && inst.firstSphere == i && inst.sphereCount == 1);
            const Vec3 at = centreOf(inst.transform);
            CHECK(at.x == float(retroBalls[i].centre[0]) && at.y == float(retroBalls[i].centre[1]) &&
                  at.z == float(retroBalls[i].centre[2]));
            CHECK(statics.spheres[i].radius == float(retroBalls[i].radius));
        }
        ++frames;
    }
    CHECK(frames > 20);
    CHECK(scene.frame(show::sampleTimeline(accent + 0.01)).camera.shot == 1);
}

// --- (i) the drop
// -------------------------------------------------------------------------------------------

bool onBarLine(int chapter, double seconds) {
    const show::TimelineFrame f = show::sampleTimeline(seconds);
    const double local = seconds - show::chapterStart(chapter);
    return std::abs(modern::nextBarLine(f, local - 1e-4) - local) < 2e-4;
}

float extraHeight(const modern::Spheres&, double seconds, std::uint32_t i) {
    const auto balls = modern::raytraceBalls(show::sampleTimeline(seconds));
    return balls[modern::kRaytraceRetroSpheres + i].centre.y -
           balls[modern::kRaytraceRetroSpheres + i].radius;
}

void testDrop() {
    const modern::Spheres scene({1.f / 256});
    constexpr std::uint32_t kExtras = modern::kRaytraceSphereCount - modern::kRaytraceRetroSpheres;
    for (const int chapter : {kRaytrace, kStorm, kInfinity}) {
        const double start = show::chapterStart(chapter);
        const modern::SpheresDrop drop = modern::spheresDrop(show::sampleTimeline(start));
        CHECK(drop.beat >= 0 && drop.barSeconds > 1.0 && drop.barSeconds < 3.0);
        CHECK(onBarLine(chapter, start + drop.seconds));
        // The drop is the same from every frame of the chapter.
        for (const double f : {0.1, 0.5, 0.95}) {
            const modern::SpheresDrop d = modern::spheresDrop(show::sampleTimeline(chapterTime(chapter, f)));
            CHECK(d.beat == drop.beat && d.seconds == drop.seconds);
        }
        // Every cut is on a bar line.
        std::uint32_t shot = 0;
        for (double t = start; t < show::chapterStart(chapter + 1) - 1e-3; t += 1.0 / 240.0) {
            const std::uint32_t now = scene.camera(show::sampleTimeline(t)).shot;
            if (now != shot) {
                CHECK(onBarLine(chapter, t) || onBarLine(chapter, t - 1.0 / 240.0) ||
                      modern::nextBarLine(show::sampleTimeline(t), t - start - 1.0 / 240.0) <= t - start);
                shot = now;
            }
        }
        CHECK(shot >= 1);
        const double at = start + drop.seconds;
        if (chapter == kRaytrace) {
            const double nextBar = start + modern::nextBarLine(show::sampleTimeline(at), drop.seconds + 0.1);
            float resting = 0, hop = 0, leap = 1e9f, landed = 0;
            for (std::uint32_t i = 0; i < kExtras; ++i) {
                resting = std::max(resting, extraHeight(scene, at - 5.0 * drop.barSeconds, i));
                hop = std::max(hop, extraHeight(scene, at - 0.125 * drop.barSeconds, i));
                leap = std::min(leap, extraHeight(scene, at + 0.5 * drop.barSeconds, i));
                landed = std::max(landed, extraHeight(scene, nextBar - 1e-4, i));
            }
            std::printf("drop: RAYTRACE at %.2f s: resting %.3f, last hop %.2f, every ball %.2f up mid-leap, "
                        "%.4f on the bar line\n",
                        drop.seconds, resting, hop, leap, landed);
            CHECK(resting == 0.f);
            CHECK(hop > 0.2f);
            CHECK(leap > 2.4f);
            CHECK(landed < 0.02f);
        } else if (chapter == kStorm) {
            CHECK(modern::stormRise(show::sampleTimeline(at - 4.5 * drop.barSeconds)) == 0.f);
            CHECK(std::abs(modern::stormRise(show::sampleTimeline(at)) - 0.15f) < 1e-3f);
            CHECK(modern::stormRise(show::sampleTimeline(at + 1.01 * drop.barSeconds)) == 1.f);
        } else {
            for (const double t : {at - 1.0, at + 3.0}) {
                const modern::SceneFrame frame = scene.frame(show::sampleTimeline(t));
                int lit = 0;
                for (std::uint32_t id = 0; id < frame.particles->count; ++id)
                    lit += modern::spheresParticle(id, 0.f, frame.particles->params).emission > 0.f;
                CHECK((t < at) == (lit == 0));
            }
        }
    }
}

// --- (d) the sweep
// -------------------------------------------------------------------------------------------

void testSweep() {
    const auto start = std::chrono::steady_clock::now();
    const evo::test::SweepStats stats = evo::test::checkSceneSweep(SceneId::Spheres, {.maxCameraStep = 0.3f});
    CHECK(stats.frames > 5000 && stats.shots == 3 + 2 + 3);
    CHECK(stats.maxLights == 2);

    // Physical sanity, frame by frame.
    const modern::Spheres scene({1.f / 256});
    const modern::Spheres full;
    const modern::SceneStatic& statics = scene.staticScene();
    float closestBall = 1e9f, closestLattice = 1e9f, closestParticle = 1e9f, lowestBall = 1e9f;
    for (const int chapter : {kRaytrace, kStorm, kInfinity}) {
        const double begin = show::chapterStart(chapter), end = show::chapterStart(chapter + 1);
        int n = 0;
        for (double t = begin; t < end - 1e-6; t += 1.0 / 60.0, ++n) {
            const show::TimelineFrame f = show::sampleTimeline(t);
            const modern::SceneFrame frame = scene.frame(f);
            const Vec3 eye = frame.camera.eye;
            if (chapter == kRaytrace) {
                const auto balls = modern::raytraceBalls(f);
                for (std::uint32_t i = 0; i < balls.size(); ++i) {
                    lowestBall = std::min(lowestBall, balls[i].centre.y - balls[i].radius);
                    closestBall =
                        std::min(closestBall, modern::length(balls[i].centre - eye) - balls[i].radius);
                    for (std::uint32_t k = modern::kRaytraceRetroSpheres; k < i; ++k)
                        CHECK(modern::length(balls[i].centre - balls[k].centre) >
                              balls[i].radius + balls[k].radius);
                    CHECK(balls[i].radius == statics.spheres[i].radius);
                }
            } else if (chapter == kInfinity) {
                // The nearest lattice point (every multiple of the spacing holds a sphere).
                const float s = modern::kInfinitySpacing;
                const Vec3 nearest{s * std::round(eye.x / s), s * std::round(eye.y / s),
                                   s * std::round(eye.z / s)};
                closestLattice =
                    std::min(closestLattice, modern::length(nearest - eye) - modern::kInfinityRadius);
                CHECK(frame.particles && frame.particles->count == 125u);
            } else if (n % 10 == 0) {
                const std::array<float, 16> params = modern::stormParams(f, {1.f});
                const auto seconds = float(f.localSeconds);
                for (std::uint32_t id = 0; id < modern::kStormSpheres; ++id) {
                    const rt::SphereRecord p = modern::spheresParticle(id, seconds, params);
                    const Vec3 c{p.center[0], p.center[1], p.center[2]};
                    closestParticle = std::min(closestParticle, modern::length(c - eye) - p.radius);
                }
            }
        }
    }
    std::printf(
        "sweep: closest ball %.3f, lowest ball bottom %.5f, closest lattice sphere %.3f, closest storm "
        "particle %.3f (%.1f s)\n",
        closestBall, lowestBall, closestLattice, closestParticle, secondsSince(start));
    CHECK(lowestBall > -1e-4f);
    CHECK(closestBall > 0.4f);
    CHECK(closestLattice > 0.4f);
    CHECK(closestParticle > 0.15f);
}

// --- (e) the storm's mirror
// ----------------------------------------------------------------------------------

void testParticleMirror(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    constexpr modern::SceneDetail kDetail{1.f / 8};
    const std::uint32_t count = modern::stormCount(kDetail);
    CHECK(count == 4096 && modern::infinitySide(kDetail) == 16);
    gfx::FrameRing ring(ctx);
    rt::ParticleField field(ctx, ring, count, "modern_particles_spheres.comp.spv");
    struct Sample {
        double seconds;
        std::array<float, 16> params;
        gfx::Buffer copy;
    };
    std::vector<Sample> samples;
    auto record = [&](double seconds, const std::array<float, 16>& params) {
        VkCommandBuffer cmd = ring.begin();
        field.record(cmd, count, seconds, seconds, params, 7);
        const VkDeviceSize bytes = count * sizeof(rt::SphereRecord);
        gfx::Buffer copy(ctx, {bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "spheres scene test records"});
        // (record() already orders its compute writes before copies.)
        const VkBufferCopy region{0, 0, bytes};
        vkCmdCopyBuffer(cmd, field.records().handle(), copy.handle(), 1, &region);
        gfx::bufferBarrier2(cmd, copy.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        ring.wait(ring.submit());
        samples.push_back({seconds, params, std::move(copy)});
    };
    // SPHERE STORM at five times, one with a full kick and energy.
    for (const double fraction : {0.0, 0.13, 0.5, 0.77, 0.999}) {
        const show::TimelineFrame f = show::sampleTimeline(chapterTime(kStorm, fraction));
        std::array<float, 16> params = modern::stormParams(f, kDetail);
        if (fraction == 0.5) {
            params[4] = 1.f;
            params[5] = 1.f;
        }
        record(f.localSeconds, params);
    }
    // RAYTRACE INFINITY at three frames' cameras.
    const modern::Spheres scene(kDetail);
    for (const double fraction : {0.1, 0.5, 0.9}) {
        const modern::SceneFrame frame = scene.frame(show::sampleTimeline(chapterTime(kInfinity, fraction)));
        CHECK(frame.particles && frame.particles->count == count);
        record(0.0, frame.particles->params);
    }
    ring.waitAll();
    idle(ctx);
    double worst = 0;
    int glowing = 0, lanterns = 0, faded = 0;
    for (const Sample& s : samples) {
        const bool lattice = s.params[3] > 0.5f;
        const auto* gpu = static_cast<const rt::SphereRecord*>(s.copy.mapped());
        for (std::uint32_t id = 0; id < count; ++id) {
            const rt::SphereRecord cpu = modern::spheresParticle(id, float(s.seconds), s.params);
            const rt::SphereRecord& g = gpu[id];
            for (int c = 0; c < 3; ++c) {
                worst = std::max(worst, double(std::abs(g.center[c] - cpu.center[c])));
                worst = std::max(worst, double(std::abs(g.prevCenter[c] - cpu.center[c])));
                CHECK(g.color[c] == cpu.color[c]);
            }
            worst = std::max(worst, double(std::abs(g.radius - cpu.radius)));
            worst = std::max(worst, double(std::abs(g.emission - cpu.emission)) / 16.0);
            CHECK(g.material == 7u);
            if (lattice) {
                lanterns += g.emission > 0.f;
                faded += g.radius <= 0.f;
            } else {
                CHECK((g.emission > 0.f) == (id < count / 8));
                glowing += g.emission > 0.f;
            }
        }
    }
    std::printf(
        "particle mirror: %u particles at %zu frames, worst difference %.2e, %d lanterns, %d faded (%.1f "
        "s)\n",
        count, samples.size(), worst, lanterns, faded, secondsSince(start));
    CHECK(worst < 1e-4);
    CHECK(glowing == int(5 * count / 8));
    CHECK(lanterns > 3 * 50 && lanterns < 3 * 300); // about one in 26
    CHECK(faded > 3 * 400);                         // the cube's corners lie beyond fadeEnd
}

// --- Rendering through the show's renderer ----------------------------------------------------------------

class Player {
public:
    Player(gfx::Context& ctx, std::uint32_t width, std::uint32_t height, std::uint32_t spp,
           modern::SceneDetail detail)
        : readback_(ctx, width, height), ring_(ctx),
          renderer_(ctx, ring_, settings(width, height, spp, detail)) {}

    std::vector<std::uint8_t> render(const show::TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        readback_.record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        return readback_.rgb();
    }

private:
    static render::RenderSettings settings(std::uint32_t width, std::uint32_t height, std::uint32_t spp,
                                           modern::SceneDetail detail) {
        render::RenderSettings s{width, height};
        s.hud = false;
        s.mode = render::RenderMode::Export;
        s.spp = spp;
        s.detail = detail;
        return s;
    }

    gfx::Readback readback_;
    gfx::FrameRing ring_;
    render::Renderer renderer_;
};

double luminance(const std::vector<std::uint8_t>& rgb) {
    double sum = 0;
    for (std::size_t i = 0; i < rgb.size(); i += 3)
        sum += (0.2126 * rgb[i] + 0.7152 * rgb[i + 1] + 0.0722 * rgb[i + 2]) / 255.0;
    return sum / double(rgb.size() / 3);
}

// --- (g) the chrome sphere reflects the board -----------------------------------------------------------

void testChromeReflectsBoard(gfx::Context& ctx) {
    constexpr std::uint32_t kW = 160, kH = 90;
    // A time before the accent when no orbiting ball covers the central sphere's disc.
    const modern::Camera camera = modern::raytraceRetroCamera();
    const modern::CameraBasis basis = modern::cameraBasis(camera);
    const float tanHalf = std::tan(camera.fovYDegrees * 3.14159265f / 360.f), aspect = float(kW) / float(kH);
    // A point's pixel and the radius of a sphere's disc (its silhouette's angular radius, in pixels).
    auto project = [&](Vec3 p) {
        const Vec3 d = p - camera.eye;
        const float z = modern::dot(d, basis.forward);
        return std::array<float, 3>{(modern::dot(d, basis.right) / (z * tanHalf * aspect) + 1.f) * 0.5f * kW,
                                    (1.f - modern::dot(d, basis.up) / (z * tanHalf)) * 0.5f * kH, z};
    };
    const Vec3 centre{0.f, 1.3f, 0.f};
    const float distance = modern::length(centre - camera.eye);
    const float discRadius = std::tan(std::asin(1.3f / distance)) / tanHalf * 0.5f * kH;
    const auto c = project(centre);
    double seconds = 0;
    for (double fraction = 0.1; fraction < 0.98; fraction += 0.005) {
        const double t = show::chapterStart(kRaytrace) +
                         fraction * show::sampleTimeline(show::chapterStart(kRaytrace)).accentSeconds;
        const auto balls = modern::raytraceRetroBalls(show::sampleTimeline(t).localSeconds);
        bool clear = true;
        for (int i = 1; i < 6; ++i) {
            const auto b = project(balls[i].centre);
            const float r = balls[i].radius / b[2] / tanHalf * 0.5f * kH * 1.2f;
            const bool behind = b[2] > c[2]; // hidden by the central sphere where they overlap
            clear = clear && (behind || std::hypot(b[0] - c[0], b[1] - c[1]) > r + discRadius + 2.f);
        }
        if (clear) {
            seconds = t;
            break;
        }
    }
    CHECK(seconds > 0);
    Player player(ctx, kW, kH, 4, {1.f / 256});
    const std::vector<std::uint8_t> rgb = player.render(forced2026(seconds));
    int red = 0, white = 0, inside = 0;
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            const float dx = float(x) + 0.5f - c[0], dy = float(y) + 0.5f - c[1];
            if (std::hypot(dx, dy) > 0.85f * discRadius || dy < 0.f)
                continue; // the lower half of the disc sees the board
            ++inside;
            const std::uint8_t* p = &rgb[3 * (std::size_t(y) * kW + x)];
            red += p[0] > 70 && p[0] > 2 * p[1] && p[0] > 2 * p[2];
            white += p[0] > 140 && p[1] > 140 && p[2] > 140 && std::abs(p[0] - p[1]) < 40;
        }
    std::printf(
        "chrome disc at %.2f s: centre (%.1f, %.1f), radius %.1f px, %d pixels below its middle: %d red, "
        "%d white\n",
        seconds, c[0], c[1], discRadius, inside, red, white);
    evo::image::writePng(std::filesystem::path(EVO_TEST_OUT_DIR) / "spheres_chrome_disc.png", kW, kH,
                         rgb.data());
    CHECK(inside > 300);
    CHECK(red >= 8);
    CHECK(white >= 8);
}

// --- (h) the storm at full detail
// ----------------------------------------------------------------------------

void testFullStorm(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    const modern::Spheres scene;
    const show::TimelineFrame at = show::sampleTimeline(accentTime(kStorm) + 0.5);
    const modern::SceneFrame frame = scene.frame(at);
    CHECK(frame.treeLights.size() == modern::kStormEmissive);
    int trees = 0;
    for (const rt::RectLight& l : frame.lights)
        trees += l.kind == std::uint32_t(rt::LightKind::Tree);
    CHECK(trees == 1);
    for (std::uint32_t k = 0; k < frame.treeLights.size(); k += 97) {
        const rt::SphereRecord p =
            modern::spheresParticle(k, float(at.localSeconds), frame.particles->params);
        const modern::TreeLightFrame& l = frame.treeLights[k];
        CHECK(l.sphere == k && l.center.x == p.center[0] && l.radius == p.radius && p.emission > 0.f);
    }
    constexpr std::uint32_t kW = 160, kH = 90;
    Player player(ctx, kW, kH, 1, {1.f});
    const std::vector<std::uint8_t> rgb = player.render(forced2026(accentTime(kStorm) + 0.5));
    const double mean = luminance(rgb);
    int glowing = 0;
    for (std::size_t i = 0; i < rgb.size(); i += 3) {
        const int hi = std::max({rgb[i], rgb[i + 1], rgb[i + 2]}),
                  lo = std::min({rgb[i], rgb[i + 1], rgb[i + 2]});
        glowing += hi > 200 && hi - lo > 90; // a saturated, bright pixel: a glowing sphere
    }
    std::printf("full storm: mean %.3f, %d glowing pixels (%.1f s)\n", mean, glowing, secondsSince(start));
    CHECK(mean > 0.03 && mean < 0.85);
    CHECK(glowing >= 10);
}

// --- Look
// ----------------------------------------------------------------------------------------------------

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp, std::uint32_t width,
                std::uint32_t height, const std::string& only) {
    std::filesystem::create_directories(dir);
    Player player(ctx, width, height, spp, {1.f});
    struct Still {
        const char* name;
        int chapter;
        double fraction; // of the chapter; < 0: seconds after the accent
    };
    const Still stills[] = {{"raytrace_20", kRaytrace, 0.2},   {"raytrace_acc", kRaytrace, -0.6},
                            {"raytrace_80", kRaytrace, 0.8},   {"raytrace_95", kRaytrace, 0.95},
                            {"storm_20", kStorm, 0.2},         {"storm_acc", kStorm, -0.6},
                            {"storm_80", kStorm, 0.8},         {"infinity_20", kInfinity, 0.2},
                            {"infinity_acc", kInfinity, -0.6}, {"infinity_80", kInfinity, 0.8}};
    for (const Still& s : stills) {
        if (std::string(s.name).find(only) == std::string::npos)
            continue;
        const auto start = std::chrono::steady_clock::now();
        const double t =
            s.fraction >= 0 ? chapterTime(s.chapter, s.fraction) : accentTime(s.chapter) - s.fraction;
        const std::vector<std::uint8_t> rgb = player.render(forced2026(t));
        const std::filesystem::path path = dir / (std::string(s.name) + ".png");
        evo::image::writePng(path, int(width), int(height), rgb.data());
        std::printf("look: %s (%.2f s) mean %.3f in %.1f s\n", path.string().c_str(), t, luminance(rgb),
                    secondsSince(start));
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        if (argc >= 3 && std::string(argv[1]) == "--look") {
            auto ctx = gfx::Context::create({.validation = true});
            renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 64u,
                       argc >= 6 ? std::uint32_t(std::stoul(argv[4])) : 640u,
                       argc >= 6 ? std::uint32_t(std::stoul(argv[5])) : 360u, argc >= 7 ? argv[6] : "");
            CHECK(ctx.validationMessageCount() == 0);
            return 0;
        }
        const auto start = std::chrono::steady_clock::now();
        testMap();
        testCards();
        testRetroMirror();
        testDrop();
        testSweep();
        {
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            else if (!ctx.syncValidationEnabled())
                std::printf("SKIP sync validation: unavailable\n");
            testParticleMirror(ctx);
            idle(ctx);
            evo::test::checkChapterPictures(ctx, SceneId::Spheres);
            idle(ctx);
            testChromeReflectsBoard(ctx);
            idle(ctx);
            testFullStorm(ctx);
            idle(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
        std::printf("spheres_scene_test: %.1f s\n", secondsSince(start));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("spheres_scene_test: %d checks passed\n", checksPassed);
    return 0;
}
