// The Tunnel scene: TUNNEL. With the validation layer and its synchronization validation on;
// fails on any message it reports.
//   (a) the map: the chapters that show Tunnel;
//   (b) the cards: TUNNEL's two cards state the same count, 1993's points and 2026's lights, and it is the
//       configuration's (96 rings of 64: 6,144);
//   (c) the 1993 mirror: the scene's copy of 1993's constants, path, slope and twist equal retro/tunnel's;
//       at every retro frame the 96 lit rings are 1993's 96 rings (their window moved 4 units back), and each
//       bead stands where 1993's point of the same ring and index stands on that ring (its angle, 1993's
//       radius, the ring square to the path); until the accent the camera is 1993's flight (on the axis at
//       1993's travel, along the path's tangent);
//   (d) the sweep: checkSceneSweep every 1/60 s; the camera stays within 0.7 of the tunnel's radius of the
//       path at its depth; every frame's tree lights are the beads (one light of kind Tree stands for them,
//       light k is particle k at the mirror's centre) and the field holds them all;
//   (e) the particle shader equals its C++ mirror (tunnelLight) for every bead at full detail at five times;
//   (f) the fixture's chapter pictures (20 %, accent, 80 %);
//   (g) the ignition: the beads idle at the pilot glow until the downbeat before the accent; a front rushes
//       in from the far end between the downbeat and the accent (never moving away, at the camera on the
//       accent), lighting the rings it has passed; after the accent each beat's pulse runs away from the
//       camera;
//   (h) physical: the haze is lit by the beads: at the accent the volume layer's in-scattered light is
//       several times what the pilot glow gives, the haze is neither empty nor opaque, and down the tunnel
//       (a long path through lit haze) it scatters more light in and lets less through than towards the
//       near walls.
// `scene_tunnel_test --look <dir> [spp] [width height] [name]` renders stills of the chapters instead (those
// whose name contains `name`).

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene.hpp"
#include "modern/scenes/tunnel.hpp"
#include "render/renderer.hpp"
#include "retro/tunnel.hpp"
#include "rt/particles.hpp"
#include "show/timeline.hpp"

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
#include <set>
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

constexpr int kTunnel = 4;

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

Vec3 centreOf(const float c[3]) {
    return {c[0], c[1], c[2]};
}

// --- (a) the map, (b) the cards
// -------------------------------------------------------------------------------

void testMap() {
    const std::vector<int> chapters = evo::test::chaptersOf(SceneId::Tunnel);
    CHECK((chapters == std::vector<int>{kTunnel}));
    CHECK(show::sampleTimeline(show::chapterStart(kTunnel)).effect == show::Effect::Tunnel);
    CHECK(std::string(modern::effectSceneName(show::Effect::Tunnel)) == "Tunnel");
}

struct CardNumber {
    long value;
    std::string unit;
};

// The numbers a card states ("6,144"), each with the word after it.
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
    CHECK(modern::kTunnelLights == 6144);
    CHECK(modern::tunnelLightCount({1.f}) == modern::kTunnelLights);
    CHECK(modern::tunnelDotsPerRing({1.f}) == 64 && modern::tunnelDotsPerRing({1.f / 256}) == 4);
    const modern::Tunnel full;
    CHECK(full.staticScene().capacity.treeLights == modern::kTunnelLights);
    CHECK(full.staticScene().capacity.particles == modern::kTunnelLights);
    CHECK(modern::Tunnel({1.f / 256}).staticScene().capacity.treeLights == 384);

    const show::TimelineFrame f = show::sampleTimeline(show::chapterStart(kTunnel));
    // 1993's card counts its points, 2026's its lights: the same number.
    long points = 0, lights = 0;
    for (const CardNumber& n : cardNumbers(f.thenCard)) {
        std::printf("1993 card: %ld %s\n", n.value, n.unit.c_str());
        if (n.unit == "POINTS")
            points = n.value;
    }
    for (const CardNumber& n : cardNumbers(f.nowCard)) {
        std::printf("2026 card: %ld %s\n", n.value, n.unit.c_str());
        if (n.unit == "LIGHTS")
            lights = n.value;
        else
            CHECK(n.value == 2026 && n.unit.empty()); // the year is the only other number
    }
    CHECK(points == long(retro::kTunnelRings * retro::kTunnelRingDots));
    CHECK(lights == long(modern::kTunnelLights));
}

// --- (c) the 1993 mirror
// ------------------------------------------------------------------------------------

void testRetroMirror() {
    CHECK(modern::kTunnelRetroRings == retro::kTunnelRings &&
          modern::kTunnelRetroDots == retro::kTunnelRingDots);
    CHECK(modern::kTunnelRetroRadius == retro::kTunnelRadius);
    CHECK(modern::kTunnelRingSpacing == retro::kTunnelRingSpacing &&
          modern::kTunnelSpeed == retro::kTunnelSpeed);
    CHECK(modern::kTunnelRetroNear == retro::kTunnelNear);
    CHECK(modern::kTunnelTwistRate == retro::kTunnelTwistRate &&
          modern::kTunnelTwistPerDepth == retro::kTunnelTwistPerDepth);
    CHECK(modern::kTunnelRetroSegments == retro::kTunnelSegments &&
          modern::kTunnelBandLength == retro::kTunnelBandLength);
    for (int i = 0; i < 2; ++i) {
        CHECK(modern::kTunnelWavesX[i].amplitude == retro::kTunnelPathX[i].amplitude &&
              modern::kTunnelWavesX[i].frequency == retro::kTunnelPathX[i].frequency &&
              modern::kTunnelWavesX[i].phase == retro::kTunnelPathX[i].phase);
        CHECK(modern::kTunnelWavesY[i].amplitude == retro::kTunnelPathY[i].amplitude &&
              modern::kTunnelWavesY[i].frequency == retro::kTunnelPathY[i].frequency &&
              modern::kTunnelWavesY[i].phase == retro::kTunnelPathY[i].phase);
    }
    for (int k = 0; k <= 2000; ++k) {
        const double w = -10.0 + 0.0613 * k;
        CHECK(modern::tunnelPath1993(w) == retro::tunnelPath(w));
        CHECK(modern::tunnelSlope1993(w) == retro::tunnelPathSlope(w));
    }

    // Every retro frame of the chapter: the lit rings are 1993's, 4 units back, and every bead stands on its
    // ring where 1993's point of the same index does.
    const modern::SceneDetail full{1.f};
    const std::array<float, 16> params = modern::tunnelLightParams(full);
    const double duration = show::chapterStart(kTunnel + 1) - show::chapterStart(kTunnel);
    double worst = 0;
    const auto frames = std::uint32_t(duration * retro::kTunnelRetroRate);
    for (std::uint32_t rf = 0; rf < frames; ++rf) {
        const double seconds = double(rf) / retro::kTunnelRetroRate;
        CHECK(std::abs(modern::tunnelTwist1993(seconds, 7.25) - retro::tunnelTwist(rf, 7.25)) < 1e-12);
        CHECK(std::abs(modern::tunnelTravel(seconds) - retro::tunnelTravel(rf)) < 1e-12);
        std::set<std::int64_t> retroRings, rings;
        for (const retro::TunnelRing& r : retro::tunnelRings(rf))
            retroRings.insert(std::llround(r.w / retro::kTunnelRingSpacing) - 16);
        for (std::uint32_t slot = 0; slot < modern::kTunnelRetroRings; ++slot)
            rings.insert(modern::tunnelRingOfSlot(slot, modern::tunnelTravel(seconds)));
        CHECK(rings == retroRings);
        if (rf % 7 != 0)
            continue;
        for (std::uint32_t id = 0; id < modern::kTunnelLights; id += 13) {
            const rt::SphereRecord bead = modern::tunnelLight(id, float(seconds), params);
            const std::uint32_t slot = id / 64, dot = id % 64;
            const double w = double(modern::tunnelRingOfSlot(slot, modern::tunnelTravel(seconds))) *
                             retro::kTunnelRingSpacing;
            const double a = 2 * 3.14159265358979323846 * dot / 64 + retro::tunnelTwist(rf, w);
            const modern::TunnelRing ring = modern::tunnelRing(w);
            const Vec3 expected =
                ring.centre + (ring.right * float(std::cos(a)) + ring.up * float(std::sin(a))) *
                                  float(retro::kTunnelRadius);
            const Vec3 d = centreOf(bead.center) - expected;
            worst = std::max(worst, double(modern::length(d)));
            CHECK(bead.radius == modern::kTunnelBeadRadius);
        }
    }
    std::printf("1993 mirror: %u retro frames, worst bead offset %.2e\n", frames, worst);
    CHECK(worst < 2e-4);
    // The ring's frame: square to the path, right-handed, the axis 1993's (x mirrored).
    for (double w : {0.0, 3.3, 17.9, 41.0}) {
        const modern::TunnelRing r = modern::tunnelRing(w);
        const auto p = retro::tunnelPath(w), s = retro::tunnelPathSlope(w);
        CHECK(r.centre.x == float(-p[0]) && r.centre.y == float(p[1]) && r.centre.z == float(w));
        const Vec3 t = modern::normalize(Vec3{float(-s[0]), float(s[1]), 1.f});
        CHECK(modern::length(r.tangent - t) < 1e-6f);
        CHECK(std::abs(modern::dot(r.up, r.tangent)) < 1e-6f &&
              std::abs(modern::dot(r.right, r.tangent)) < 1e-6f);
        CHECK(modern::length(modern::cross(r.tangent, r.up) - r.right) < 1e-6f);
    }
    // Until the accent the camera is 1993's flight.
    for (double fraction : {0.0, 0.2, 0.5, 0.7}) {
        const show::TimelineFrame f = show::sampleTimeline(chapterTime(kTunnel, fraction));
        CHECK(f.localSeconds < f.accentSeconds);
        const modern::Camera c = modern::tunnelCamera(f);
        const modern::Camera echo = modern::tunnelEchoCamera(modern::tunnelTravel(f.localSeconds));
        const modern::TunnelRing r = modern::tunnelRing(modern::tunnelTravel(f.localSeconds));
        CHECK(modern::length(c.eye - r.centre) < 1e-5f);
        CHECK(modern::length(modern::normalize(c.target - c.eye) - r.tangent) < 1e-5f);
        CHECK(modern::length(c.up - echo.up) < 1e-5f && c.shot == 0);
    }
}

// --- (d) the sweep
// --------------------------------------------------------------------------------------------

void testSweep() {
    const auto start = std::chrono::steady_clock::now();
    const evo::test::SweepStats stats = evo::test::checkSceneSweep(SceneId::Tunnel, {0.5f});
    std::printf("sweep: %u frames, %u shots, largest %u instances / %u lights, camera step %.3f (%.1f s)\n",
                stats.frames, stats.shots, stats.maxInstances, stats.maxLights, stats.maxCameraStep,
                secondsSince(start));
    CHECK(stats.frames > 1000);
    // The camera in the tube, and the beads the tree's lights, every 1/60 s.
    const modern::Tunnel scene({1.f / 256});
    const double from = show::chapterStart(kTunnel), to = show::chapterStart(kTunnel + 1);
    float farthest = 0;
    for (double t = from; t < to; t += 1.0 / 60) {
        const show::TimelineFrame f = show::sampleTimeline(t);
        const modern::Camera c = modern::tunnelCamera(f);
        const modern::TunnelRing r = modern::tunnelRing(modern::tunnelTravel(f.localSeconds));
        const Vec3 off = c.eye - r.centre;
        CHECK(std::abs(modern::dot(off, r.tangent)) < 1e-4f); // in the ring's plane
        farthest = std::max(farthest, modern::length(off));
        CHECK(modern::length(off) <= 0.7f * float(modern::kTunnelRetroRadius));
        if (std::llround((t - from) * 60) % 10 != 0)
            continue;
        const modern::SceneFrame frame = scene.frame(f);
        CHECK(frame.particles && frame.particles->count == scene.staticScene().capacity.particles);
        CHECK(frame.treeLights.size() == frame.particles->count);
        for (std::uint32_t k = 0; k < frame.treeLights.size(); k += 11) {
            const rt::SphereRecord bead =
                modern::tunnelLight(k, float(f.localSeconds), frame.particles->params);
            CHECK(frame.treeLights[k].sphere == k && frame.treeLights[k].radius == bead.radius);
            CHECK(modern::length(frame.treeLights[k].center - centreOf(bead.center)) == 0.f);
        }
    }
    std::printf("sweep: the eye stays within %.3f of the axis\n", farthest);
    CHECK(farthest > 0.2f); // it does break free after the accent
}

// --- (e) the particle shader against its mirror
// ---------------------------------------------------------------

void testParticleMirror(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    const modern::SceneDetail full{1.f};
    const std::uint32_t count = modern::tunnelLightCount(full);
    gfx::FrameRing ring(ctx);
    rt::ParticleField field(ctx, ring, count, "modern_particles_tunnel.comp.spv");
    const std::array<float, 16> params = modern::tunnelLightParams(full);
    double worst = 0;
    for (const double fraction : {0.0, 0.31, 0.62, 0.75, 0.999}) {
        const show::TimelineFrame f = show::sampleTimeline(chapterTime(kTunnel, fraction));
        const auto seconds = float(f.localSeconds), previous = std::max(0.f, seconds - 1.f / 60);
        VkCommandBuffer cmd = ring.begin();
        field.record(cmd, count, seconds, previous, params, 5);
        const VkDeviceSize bytes = count * sizeof(rt::SphereRecord);
        gfx::Buffer copy(ctx, {bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "tunnel scene test records"});
        const VkBufferCopy region{0, 0, bytes};
        vkCmdCopyBuffer(cmd, field.records().handle(), copy.handle(), 1, &region);
        gfx::bufferBarrier2(cmd, copy.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
        ring.wait(ring.submit());
        idle(ctx);
        const auto* gpu = static_cast<const rt::SphereRecord*>(copy.mapped());
        for (std::uint32_t id = 0; id < count; ++id) {
            const rt::SphereRecord cpu = modern::tunnelLight(id, seconds, params);
            const rt::SphereRecord before = modern::tunnelLight(id, previous, params);
            const rt::SphereRecord& g = gpu[id];
            for (int c = 0; c < 3; ++c) {
                worst = std::max(worst, double(std::abs(g.center[c] - cpu.center[c])));
                worst = std::max(worst, double(std::abs(g.prevCenter[c] - before.center[c])));
                worst = std::max(worst, double(std::abs(g.color[c] - cpu.color[c])));
            }
            worst = std::max(worst, double(std::abs(g.radius - cpu.radius)));
            worst = std::max(worst, double(std::abs(g.emission - cpu.emission)) / 100.0);
            CHECK(g.material == 5u);
        }
    }
    std::printf("particle mirror: %u beads at 5 times, worst difference %.2e (%.1f s)\n", count, worst,
                secondsSince(start));
    CHECK(worst < 1e-4);
}

// --- (g) the ignition and the pulses
// -------------------------------------------------------------------------

void testIgnition() {
    const show::TimelineFrame at = show::sampleTimeline(show::chapterStart(kTunnel));
    const modern::TunnelMoments m = modern::tunnelMoments(at);
    const double start = show::chapterStart(kTunnel);
    std::printf("moments: downbeat %.3f (show %.3f), accent %.3f (show %.3f), bar %.3f s\n", m.downbeat,
                start + m.downbeat, m.accent, start + m.accent, m.barSeconds);
    CHECK(m.downbeat <= m.accent && m.accent - m.downbeat < m.barSeconds + 1e-6);
    CHECK(std::abs(start + m.downbeat - 74.28) < 0.03 && std::abs(start + m.accent - 75.35) < 0.03);
    // Before the downbeat: the pilot glow everywhere in the window.
    for (double fraction : {0.05, 0.4, 0.66}) {
        const show::TimelineFrame f = show::sampleTimeline(chapterTime(kTunnel, fraction));
        CHECK(f.localSeconds < m.downbeat);
        for (double ahead = 0.0; ahead < 15.0; ahead += 0.5)
            CHECK(std::abs(modern::tunnelLightScale(f, ahead) - modern::kTunnelPilot) < 1e-6f);
    }
    // The front: from beyond the far end on the downbeat to the camera on the accent, never moving away.
    double last = 1e9;
    for (int k = 0; k <= 60; ++k) {
        const show::TimelineFrame f =
            show::sampleTimeline(start + m.downbeat + (m.accent + 0.2 - m.downbeat) * k / 60.0);
        const double front = modern::tunnelIgnitionFront(f);
        CHECK(front <= last);
        last = front;
    }
    CHECK(modern::tunnelIgnitionFront(show::sampleTimeline(start + m.downbeat + 1e-4)) >
          modern::kTunnelLitFar);
    CHECK(std::abs(modern::tunnelIgnitionFront(show::sampleTimeline(start + m.accent))) < 0.3);
    CHECK(modern::tunnelIgnitionFront(show::sampleTimeline(start + m.accent + 0.2)) < modern::kTunnelLitNear);
    // On the way: lit beyond the front, the pilot glow on this side of it.
    {
        const show::TimelineFrame f =
            show::sampleTimeline(start + m.downbeat + 0.7 * (m.accent - m.downbeat));
        const double front = modern::tunnelIgnitionFront(f);
        CHECK(front > 6.0 && front < modern::kTunnelLitFar - 4.0);
        CHECK(modern::tunnelLightScale(f, front + 2.5) >= 0.9f);
        CHECK(std::abs(modern::tunnelLightScale(f, front - 2.5) - modern::kTunnelPilot) < 0.05f);
    }
    // After the accent: lit everywhere, and each beat's pulse leaves the camera.
    const show::BeatGridView grid = show::beatGrid(at.track);
    const double startTrack = at.trackSeconds - at.localSeconds;
    int beat = grid.beatAt(startTrack + m.accent + 0.3) + 1; // the first beat a little after the accent
    const double beatTime = grid.beatTime(beat) - startTrack;
    double previousPeak = -1e9;
    for (const double since : {0.05, 0.15, 0.25, 0.35}) {
        const show::TimelineFrame f = show::sampleTimeline(start + beatTime + since);
        double peak = 0, best = 0;
        for (double ahead = -2.0; ahead < 18.0; ahead += 0.05) {
            const float s = modern::tunnelLightScale(f, ahead);
            if (ahead > 0 && ahead < 15)
                CHECK(s >= 0.95f);
            if (s > best) {
                best = s;
                peak = ahead;
            }
        }
        std::printf("pulse %.2f s after a beat: peak %.2f units ahead, scale %.2f\n", since, peak, best);
        CHECK(peak > previousPeak + 1.0);
        CHECK(best > 1.4);
        previousPeak = peak;
    }
}

// --- Rendering through the show's renderer
// --------------------------------------------------------------------

class Player {
public:
    Player(gfx::Context& ctx, std::uint32_t width, std::uint32_t height, std::uint32_t spp,
           modern::SceneDetail detail, modern::SceneFactory factory = {})
        : readback_(ctx, width, height), ring_(ctx),
          renderer_(ctx, ring_, settings(width, height, spp, detail, std::move(factory))) {}

    std::vector<std::uint8_t> render(const show::TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        readback_.record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        return readback_.rgb();
    }

    render::Renderer& renderer() { return renderer_; }
    gfx::FrameRing& ring() { return ring_; }

private:
    static render::RenderSettings settings(std::uint32_t width, std::uint32_t height, std::uint32_t spp,
                                           modern::SceneDetail detail, modern::SceneFactory factory) {
        render::RenderSettings s{width, height};
        s.hud = false;
        s.mode = render::RenderMode::Export;
        s.spp = spp;
        s.detail = detail;
        s.sceneFactory = std::move(factory);
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

// The volume layer (RGBA16F: in-scattered radiance, transmittance) of the frame the player traced last.
struct Volume {
    std::uint32_t width = 0, height = 0;
    std::vector<float> values;
    float at(std::uint32_t x, std::uint32_t y, std::uint32_t c) const {
        return values[(std::size_t(y) * width + x) * 4 + c];
    }
};

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1fu, mantissa = h & 0x3ffu;
    float v;
    if (exponent == 0)
        v = std::ldexp(float(mantissa), -24);
    else if (exponent == 31)
        v = mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    else
        v = std::ldexp(float(mantissa | 0x400u), int(exponent) - 25);
    return sign ? -v : v;
}

Volume readVolume(gfx::Context& ctx, Player& player) {
    gfx::Image& image = player.renderer().modern().tracer()->gbuffer().volume;
    const VkExtent2D extent = image.extent();
    CHECK(image.format() == VK_FORMAT_R16G16B16A16_SFLOAT);
    gfx::Buffer buffer(ctx, {VkDeviceSize(extent.width) * extent.height * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             true, "tunnel test volume readback"});
    VkCommandBuffer cmd = player.ring().begin();
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle(), 1,
                           &region);
    gfx::bufferBarrier2(cmd, buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    player.ring().wait(player.ring().submit());
    idle(ctx);
    buffer.invalidate();
    Volume v;
    v.width = extent.width;
    v.height = extent.height;
    const auto* halves = static_cast<const std::uint16_t*>(buffer.mapped());
    v.values.resize(std::size_t(extent.width) * extent.height * 4);
    for (std::size_t i = 0; i < v.values.size(); ++i)
        v.values[i] = halfToFloat(halves[i]);
    return v;
}

// --- (h) the haze is lit by the beads
// --------------------------------------------------------------------------

void testLitHaze(gfx::Context& ctx) {
    const auto start = std::chrono::steady_clock::now();
    constexpr std::uint32_t kW = 64, kH = 36;
    Player player(ctx, kW, kH, 2, {1.f / 64});
    struct Haze {
        double inscatter = 0, transmittance = 0, wall = 0, middle = 0, wallT = 0, middleT = 0;
    };
    auto haze = [&](double seconds) {
        player.render(forced2026(seconds));
        const Volume v = readVolume(ctx, player);
        Haze h;
        int walls = 0, middles = 0;
        for (std::uint32_t y = 0; y < kH; ++y)
            for (std::uint32_t x = 0; x < kW; ++x) {
                const double l = 0.2126 * v.at(x, y, 0) + 0.7152 * v.at(x, y, 1) + 0.0722 * v.at(x, y, 2);
                CHECK(std::isfinite(l) && l >= 0.0);
                h.inscatter += l;
                h.transmittance += v.at(x, y, 3);
                // The frame's outer band (the walls, near) against its middle (down the tunnel).
                const double dx = (double(x) + 0.5) / kW - 0.5, dy = ((double(y) + 0.5) / kH - 0.5) * kH / kW;
                const double r = std::hypot(dx, dy);
                if (r > 0.36) {
                    h.wall += l;
                    h.wallT += v.at(x, y, 3);
                    ++walls;
                } else if (r < 0.1) {
                    h.middle += l;
                    h.middleT += v.at(x, y, 3);
                    ++middles;
                }
            }
        h.inscatter /= double(kW * kH);
        h.transmittance /= double(kW * kH);
        h.wall /= std::max(walls, 1);
        h.middle /= std::max(middles, 1);
        h.wallT /= std::max(walls, 1);
        h.middleT /= std::max(middles, 1);
        return h;
    };
    const Haze pilot = haze(chapterTime(kTunnel, 0.2));
    const Haze lit = haze(accentTime(kTunnel) + 0.05);
    std::printf(
        "haze: pilot L_v %.4f T %.3f; lit L_v %.4f T %.3f; walls L_v %.4f T %.3f, down the tunnel L_v "
        "%.4f T %.3f (%.1f s)\n",
        pilot.inscatter, pilot.transmittance, lit.inscatter, lit.transmittance, lit.wall, lit.wallT,
        lit.middle, lit.middleT, secondsSince(start));
    CHECK(lit.inscatter > 4.0 * pilot.inscatter);
    CHECK(lit.transmittance > 0.05 && lit.transmittance < 0.95);
    // Down the tunnel a ray crosses far more lit haze than towards the near walls: more light scattered in,
    // less let through.
    CHECK(lit.middle > 1.5 * lit.wall);
    CHECK(lit.middleT < lit.wallT - 0.03);
}

// --- Look
// -----------------------------------------------------------------------------------------------------

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp, std::uint32_t width,
                std::uint32_t height, const std::string& only) {
    std::filesystem::create_directories(dir);
    Player player(ctx, width, height, spp, {1.f});
    struct Still {
        const char* name;
        int chapter;
        double at; // a fraction of the chapter, or (>= 10) seconds after the accent plus 10
    };
    const Still stills[] = {{"tunnel_20", kTunnel, 0.2},       {"tunnel_ignite", kTunnel, 9.5},
                            {"tunnel_ignite2", kTunnel, 9.85}, {"tunnel_acc", kTunnel, 10.0},
                            {"tunnel_acc+1", kTunnel, 11.0},   {"tunnel_80", kTunnel, 0.8},
                            {"tunnel_95", kTunnel, 0.95}};
    for (const Still& s : stills) {
        if (std::string(s.name).find(only) == std::string::npos)
            continue;
        const auto start = std::chrono::steady_clock::now();
        const double t = s.at < 5 ? chapterTime(s.chapter, s.at) : accentTime(s.chapter) + (s.at - 10.0);
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
            renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 16u,
                       argc >= 6 ? std::uint32_t(std::stoul(argv[4])) : 480u,
                       argc >= 6 ? std::uint32_t(std::stoul(argv[5])) : 270u, argc >= 7 ? argv[6] : "");
            CHECK(ctx.validationMessageCount() == 0);
            return 0;
        }
        const auto start = std::chrono::steady_clock::now();
        testMap();
        testCards();
        testRetroMirror();
        testIgnition();
        testSweep();
        {
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            else if (!ctx.syncValidationEnabled())
                std::printf("SKIP sync validation: unavailable\n");
            testParticleMirror(ctx);
            idle(ctx);
            evo::test::checkChapterPictures(ctx, SceneId::Tunnel);
            idle(ctx);
            testLitHaze(ctx);
            idle(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
        std::printf("scene_tunnel_test: %.1f s\n", secondsSince(start));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("scene_tunnel_test: %d checks passed\n", checksPassed);
    return 0;
}
