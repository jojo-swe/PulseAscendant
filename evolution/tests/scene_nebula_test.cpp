// The Nebula scene (step 5, Task 18): PLASMA and NEBULA on lavapipe, with the validation layer on; fails on any
// message.
//
// (k) the cards: NEBULA's card names 16,777,216 voxels (256^3, the full-detail grid) and 3 stars, which are
//     the scene's full-detail configuration and nebulaStats(); PLASMA PLANET's the cloud shell's 256x128x32,
//     the low pass's grid; the scene's own cards are the cue lines at any detail; detail scales the grid and
//     the splats;
// (m) the star shader's spheres equal the C++ mirror (nebulaStar) within 1e-4 at 4 times per chapter, and the
//     frame's tree lights are those spheres;
// (b) the bake: two bakes of the same frame are byte-identical, another time differs; NEBULA's emission sits
//     on the ionisation fronts (the side of the centre pillar's head facing star A glows several times more
//     than the far side, in emissivity), its dust is dense and its cavity thin;
// (h) PLASMA's mean emission hue at 3 times is within 15 degrees of the 1993 picture's mean hue at the same
//     1993 frame (both in linear light), and the scene's palette ring is the 1993 palette's;
// (t) at the nebula's projected centre the volume layer's transmittance is below 0.8 and its radiance above 0;
//     PLASMA on its accent has the camera inside the cloud: T < 0.5 at the frame's centre;
// (u) PLASMA PLANET: the sun clears the limb on D (468.55) within a frame, hidden before and up after it, and
//     the shots change on their bar lines;
// (r) the reveal: star A is hidden behind the centre pillar a bar before the downbeat D (the transmittance to
//     it under 0.1) and shows on the accent (above 0.3), as the analytic pillars say (the camera's line to A
//     grazes the centre pillar's edge between them);
// (p) checkChapterPictures for the three chapters and (s) checkSceneSweep (scene_fixture.hpp), the clouds'
//     camera steps at most 0.6 a frame, every view turning under a degree a frame.
//
// `scene_nebula_test --look <dir> [spp] [detail]` renders 480x270 stills of both chapters at several moments
// through the renderer (Export, forced 2026, no HUD) instead; `--volume <spp> <file>` writes one frame's raw
// volume layer (for measuring the integrator's noise).

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/medium_volume.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene.hpp"
#include "modern/scenes/nebula.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "retro/palette.hpp"
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

void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

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

// An RGBA16F image (2D or 3D) or an R32F one read back: its bytes and its values (4 or 1 per texel).
struct Plane {
    std::uint32_t width = 0, height = 0, depth = 1, channels = 4;
    std::vector<std::uint8_t> bytes;
    std::vector<float> values;
    float at(std::uint32_t x, std::uint32_t y, std::uint32_t c) const {
        return values[(std::size_t(y) * width + x) * channels + c];
    }
    float at3(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t c) const {
        return values[((std::size_t(z) * height + y) * width + x) * channels + c];
    }
};

struct PendingRead {
    gfx::Buffer buffer;
    VkFormat format;
    std::uint32_t width, height, depth;
};

PendingRead recordRead(gfx::Context& ctx, VkCommandBuffer cmd, gfx::Image& image) {
    const VkFormat format = image.format();
    const std::uint32_t texel = format == VK_FORMAT_R16G16B16A16_SFLOAT ? 8 : format == VK_FORMAT_R32_SFLOAT ? 4 : 0;
    if (texel == 0)
        throw std::logic_error("recordRead: unexpected format");
    const VkExtent2D extent = image.extent();
    const std::uint32_t depth = image.viewType() == VK_IMAGE_VIEW_TYPE_3D ? image.depth() : 1;
    PendingRead read{gfx::Buffer(ctx, {VkDeviceSize(extent.width) * extent.height * depth * texel,
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "nebula test readback"}),
                     format, extent.width, extent.height, depth};
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, depth};
    vkCmdCopyImageToBuffer(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, read.buffer.handle(), 1,
                           &region);
    gfx::bufferBarrier2(cmd, read.buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    return read;
}

Plane finishRead(const PendingRead& read) {
    read.buffer.invalidate();
    Plane plane;
    plane.width = read.width;
    plane.height = read.height;
    plane.depth = read.depth;
    plane.channels = read.format == VK_FORMAT_R32_SFLOAT ? 1 : 4;
    const auto* bytes = static_cast<const std::uint8_t*>(read.buffer.mapped());
    plane.bytes.assign(bytes, bytes + read.buffer.size());
    const std::size_t count = std::size_t(read.width) * read.height * read.depth * plane.channels;
    plane.values.resize(count);
    if (read.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
        for (std::size_t i = 0; i < count; ++i) {
            std::uint16_t h;
            std::memcpy(&h, bytes + 2 * i, 2);
            plane.values[i] = halfToFloat(h);
        }
    } else {
        std::memcpy(plane.values.data(), bytes, count * sizeof(float));
    }
    return plane;
}

// The pixel (fractional, y down) a world point projects to through a pinhole camera.
std::array<float, 2> project(const Camera& camera, Vec3 p, std::uint32_t width, std::uint32_t height) {
    const modern::CameraBasis b = modern::cameraBasis(camera);
    const Vec3 v = p - camera.eye;
    const float z = modern::dot(v, b.forward);
    const float tanHalf = std::tan(camera.fovYDegrees * 3.14159265f / 360.f);
    const float aspect = float(width) / float(height);
    const float x = modern::dot(v, b.right) / (z * tanHalf * aspect), y = modern::dot(v, b.up) / (z * tanHalf);
    return {(x + 1.f) * 0.5f * float(width), (1.f - y) * 0.5f * float(height)};
}

// --- (k) the cards ---------------------------------------------------------------------------------------------

// The numbers of a card, in order, thousands separators dropped ("16,777,216 VOXELS · 3 STARS" -> 16777216, 3).
std::vector<std::uint64_t> cardNumbers(const char* card) {
    std::vector<std::uint64_t> numbers;
    const std::string text(card);
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] < '0' || text[i] > '9') {
            ++i;
            continue;
        }
        std::uint64_t n = 0;
        while (i < text.size() && ((text[i] >= '0' && text[i] <= '9') ||
                                   (text[i] == ',' && i + 1 < text.size() && text[i + 1] >= '0' && text[i + 1] <= '9'))) {
            if (text[i] != ',')
                n = n * 10 + std::uint64_t(text[i] - '0');
            ++i;
        }
        numbers.push_back(n);
    }
    return numbers;
}

void testCards() {
    const modern::NebulaStats stats = modern::nebulaStats();
    CHECK(stats.gridSide == 256u && stats.voxels == 16'777'216u && stats.stars == 3u);
    const modern::Nebula full{modern::SceneDetail{1.f}};
    const int nebula = chapterOf(show::Effect::Nebula);
    const show::TimelineFrame f = at(nebula, 3.0);
    const std::vector<std::uint64_t> numbers = cardNumbers(f.nowCard);
    std::printf("(k) %s: card \"%s\"\n", f.title, f.nowCard);
    CHECK(numbers.size() == 3 && numbers[0] == 2026u);
    CHECK(numbers[1] == stats.voxels);
    CHECK(numbers[2] == stats.stars);
    const modern::SceneFrame frame = full.frame(f);
    CHECK(frame.medium && frame.medium->grid);
    const auto& size = frame.medium->grid->size;
    CHECK(std::uint64_t(size[0]) * size[1] * size[2] == stats.voxels);
    CHECK(full.staticScene().capacity.mediumGrid == (std::array<std::uint32_t, 3>{288u, 256u, 256u})); // both chapters' largest
    CHECK(frame.particles && frame.particles->count == stats.stars + stats.embeddedStars);
    CHECK(frame.treeLights.size() == frame.particles->count);
    CHECK(frame.splats && frame.splats->count == modern::kNebulaBackgroundStars);
    // PLASMA's card names the technique, as directed.
    const show::TimelineFrame p = at(chapterOf(show::Effect::Plasma), 3.0);
    std::printf("(k) %s: cards \"%s\" / \"%s\"\n", p.title, p.thenCard, p.nowCard);
    CHECK(std::string(p.nowCard).find("MULTIPLE SCATTERING") != std::string::npos);
    const modern::SceneFrame plasma = full.frame(p);
    CHECK(plasma.medium->grid->size == modern::kPlasmaGrid && !plasma.splats);
    CHECK(plasma.particles->count == modern::kPlasmaStars);
    // PLASMA PLANET's card: the cloud shell's grid, as the low pass bakes it; the scene's own cards are the
    // cue lines at any detail.
    const int planetChapter = chapterOf(show::Effect::PlasmaPlanet);
    const show::TimelineFrame planet = at(planetChapter, 3.0);
    std::printf("(k) %s: card \"%s\"\n", planet.title, planet.nowCard);
    const std::vector<std::uint64_t> shell = cardNumbers(planet.nowCard);
    CHECK(shell.size() == 4 && shell[1] == modern::kPlanetCloudGrid[0] && shell[2] == modern::kPlanetCloudGrid[1] &&
          shell[3] == modern::kPlanetCloudGrid[2]);
    const modern::Nebula small{kTestDetail};
    for (const show::Effect e : {show::Effect::Plasma, show::Effect::Nebula, show::Effect::PlasmaPlanet}) {
        const show::TimelineFrame head = at(chapterOf(e), 3.0);
        CHECK(full.nowCard(e) == head.nowCard && small.nowCard(e) == head.nowCard);
    }
    const modern::PlasmaPlanetTimes planetTimes = modern::plasmaPlanetTimes(planet);
    const modern::SceneFrame lowPass = full.frame(at(planetChapter, planetTimes.lowPass + 1.0));
    const auto& cloud = lowPass.medium->grid->size;
    CHECK(cloud[0] == modern::kPlanetCloudGrid[0] && cloud[2] == modern::kPlanetCloudGrid[1] &&
          cloud[1] == modern::kPlanetCloudGrid[2]);
    // Detail scales the grid by its cube root (never below the minimum) and the splats.
    CHECK(modern::nebulaGridSize(show::Effect::Nebula, kTestDetail) == (std::array<std::uint32_t, 3>{40u, 40u, 40u}));
    CHECK(modern::nebulaGridSize(show::Effect::Nebula, {1e-9f})[0] == modern::kMinNebulaGrid);
    CHECK(modern::nebulaSplatCount(kTestDetail) == 512u);
}

// --- (m) the stars against the C++ mirror ---------------------------------------------------------------------

void testStars(gfx::Context& ctx) {
    gfx::FrameRing ring(ctx);
    const std::uint32_t capacity = modern::kNebulaStars + modern::kNebulaEmbeddedStars;
    rt::ParticleField field(ctx, ring, capacity, modern::kNebulaStarsShader);
    const modern::Nebula scene{kTestDetail};
    double worst = 0;
    for (const show::Effect effect : {show::Effect::Plasma, show::Effect::Nebula}) {
        const int chapter = chapterOf(effect);
        const double duration = at(chapter, 0.0).chapterDuration;
        for (const double local : {0.5, 0.31 * duration, 0.62 * duration, duration - 0.3}) {
            const show::TimelineFrame f = at(chapter, local);
            const modern::SceneFrame frame = scene.frame(f);
            const modern::ParticleFrame& p = *frame.particles;
            VkCommandBuffer cmd = ring.begin();
            field.record(cmd, p.count, f.localSeconds, f.localSeconds - 1.0 / 60.0, p.params, p.material);
            const VkDeviceSize bytes = VkDeviceSize(p.count) * sizeof(rt::SphereRecord);
            gfx::Buffer copy(ctx, {bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "nebula test stars"});
            const VkBufferCopy region{0, 0, bytes};
            vkCmdCopyBuffer(cmd, field.records().handle(), copy.handle(), 1, &region);
            gfx::bufferBarrier2(cmd, copy.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
            ring.wait(ring.submit());
            idle(ctx);
            copy.invalidate();
            std::vector<rt::SphereRecord> gpu(p.count);
            std::memcpy(gpu.data(), copy.mapped(), bytes);
            CHECK(frame.treeLights.size() == p.count);
            for (std::uint32_t id = 0; id < p.count; ++id) {
                const rt::SphereRecord cpu = modern::nebulaStar(id, float(f.localSeconds), p.params);
                const rt::SphereRecord& g = gpu[id];
                const double e = std::max({double(modern::length(v3(g.center) - v3(cpu.center))),
                                           double(modern::length(v3(g.prevCenter) - v3(cpu.center))),
                                           std::abs(double(g.radius) - cpu.radius),
                                           double(modern::length(v3(g.color) - v3(cpu.color))),
                                           std::abs(double(g.emission) - cpu.emission) / std::max(1.0, double(cpu.emission))});
                CHECK(e <= 1e-4);
                CHECK(g.material == p.material);
                worst = std::max(worst, e);
                // The light tree holds exactly these spheres.
                const modern::TreeLightFrame& light = frame.treeLights[id];
                CHECK(light.sphere == id && light.radius == cpu.radius && light.scale == 1.f);
                CHECK(modern::length(light.center - v3(cpu.center)) == 0.f);
                CHECK(modern::length(light.emission - v3(cpu.color) * cpu.emission) <= 1e-3f * cpu.emission);
            }
            if (effect == show::Effect::Nebula) // the embedded stars lie inside their pillars
                for (std::uint32_t id = modern::kNebulaStars; id < p.count; ++id)
                    CHECK(modern::nebulaPillarDistance(v3(gpu[id].center)) < 0.f);
        }
        std::printf("(m) %s: the star field equals the C++ mirror at 4 times\n", at(chapter, 0.0).title);
    }
    std::printf("(m) worst difference %.2e\n", worst);
    idle(ctx);
}

// --- (b) the bake ----------------------------------------------------------------------------------------------

Plane bake(gfx::Context& ctx, gfx::FrameRing& ring, modern::MediumVolume& volume, const modern::GridMediumDesc& desc,
           double seconds) {
    VkCommandBuffer cmd = ring.begin();
    volume.record(cmd, desc, seconds);
    const PendingRead read = recordRead(ctx, cmd, volume.density());
    ring.wait(ring.submit());
    idle(ctx);
    return finishRead(read);
}

// The voxel of a grid nearest a world point inside its box.
std::array<std::uint32_t, 3> voxelAt(Vec3 p, Vec3 lo, Vec3 hi, const std::array<std::uint32_t, 3>& size) {
    const Vec3 u = (p - lo);
    const Vec3 extent = hi - lo;
    const float uvw[3] = {u.x / extent.x, u.y / extent.y, u.z / extent.z};
    std::array<std::uint32_t, 3> v{};
    for (int a = 0; a < 3; ++a)
        v[a] = std::uint32_t(std::clamp(int(std::floor(uvw[a] * float(size[a]))), 0, int(size[a]) - 1));
    return v;
}

void testBake(gfx::Context& ctx) {
    gfx::FrameRing ring(ctx);
    const int chapter = chapterOf(show::Effect::Nebula);
    const show::TimelineFrame f = at(chapter, 9.0);
    const modern::SceneDetail detail{1.f / 64}; // 64^3: a pillar head about 6 voxels across
    modern::GridMediumDesc desc = modern::nebulaMedium(f, detail);
    CHECK(desc.size == (std::array<std::uint32_t, 3>{64u, 64u, 64u}));
    modern::MediumVolume volume(ctx, ring, desc.size, modern::kNebulaMediumShader);
    const auto start = std::chrono::steady_clock::now();
    const Plane a = bake(ctx, ring, volume, desc, f.localSeconds);
    const double firstSeconds = secondsSince(start);
    const auto again = std::chrono::steady_clock::now();
    const Plane b = bake(ctx, ring, volume, desc, f.localSeconds);
    const double bakeSeconds = secondsSince(again);
    CHECK(a.bytes == b.bytes);
    modern::GridMediumDesc later = modern::nebulaMedium(at(chapter, 14.0), detail);
    const Plane c = bake(ctx, ring, volume, later, 14.0);
    CHECK(a.bytes != c.bytes);
    std::printf("(b) two bakes identical (%zu bytes), another time differs; a 64^3 bake takes %.2f s on this "
                "device (the first, with its pipeline, %.2f s)\n",
                a.bytes.size(), bakeSeconds, firstSeconds);
    // Dense dust inside the centre pillar, thin gas in the cavity above the ridge in front of it.
    const modern::NebulaPillar& centre = modern::kNebulaPillars[1];
    const Vec3 lo = modern::kNebulaBoxLo, hi = modern::kNebulaBoxHi;
    const Vec3 inside = centre.base + (centre.top - centre.base) * 0.6f;
    const auto vi = voxelAt(inside, lo, hi, desc.size);
    const auto vc = voxelAt({-20.f, 60.f, 20.f}, lo, hi, desc.size);
    const float dense = a.at3(vi[0], vi[1], vi[2], 0), thin = a.at3(vc[0], vc[1], vc[2], 0);
    std::printf("(b) density inside the centre pillar %.3f, in the cavity %.4f\n", dense, thin);
    CHECK(dense > 0.5f && thin > 0.f && thin < 0.05f);
    // The ionisation front: along the line from star A through the centre pillar's head, the brightest
    // emission within 6 units of the surface on A's side against the same on the far side.
    const Vec3 head = centre.top + (centre.base - centre.top) * 0.08f;
    const Vec3 toward = modern::normalize(modern::kNebulaStarCentres[0] - head);
    float nearSide = 0, farSide = 0;
    for (float s = -16.f; s <= 16.f; s += 0.5f) {
        const Vec3 p = head + toward * s;
        const float distance = modern::nebulaPillarDistance(p);
        if (distance < -6.f || distance > 3.f)
            continue;
        const auto v = voxelAt(p, lo, hi, desc.size);
        // The emissivity (what the tracer emits per unit length, ~ density x stored emission).
        const float e = a.at3(v[0], v[1], v[2], 0) *
                        (a.at3(v[0], v[1], v[2], 1) + a.at3(v[0], v[1], v[2], 2) + a.at3(v[0], v[1], v[2], 3));
        (s > 0 ? nearSide : farSide) = std::max(s > 0 ? nearSide : farSide, e);
    }
    std::printf("(b) the centre pillar's head: emission %.3f on star A's side, %.3f on the far side\n", nearSide,
                farSide);
    CHECK(nearSide > 4.f * farSide && nearSide > 0.f);
    idle(ctx);
}

// --- (h) PLASMA's hue against the 1993 palette's ------------------------------------------------------------

double srgbToLinear(double c) {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// The hue of a linear RGB colour, in degrees.
double hueOf(double r, double g, double b) {
    const double hi = std::max({r, g, b}), lo = std::min({r, g, b}), c = hi - lo;
    if (!(c > 0))
        return 0;
    double h = hi == r ? std::fmod((g - b) / c, 6.0) : hi == g ? (b - r) / c + 2.0 : (r - g) / c + 4.0;
    h *= 60.0;
    return h < 0 ? h + 360.0 : h;
}

double hueDistance(double a, double b) {
    const double d = std::abs(a - b);
    return std::min(d, 360.0 - d);
}

void testPlasmaHue(gfx::Context& ctx) {
    // The scene's ring is the 1993 palette's (at retro frame 0, entry 32 + j shows ring colour j).
    {
        retro::Palette palette{};
        retro::plasmaPalette(palette, show::TimelineFrame{}, 0);
        for (std::uint32_t j = 0; j < 224; ++j) {
            const std::uint32_t c = palette[retro::kPlasmaFirstIndex + j];
            const Vec3 ring = modern::plasmaRingColour(j);
            CHECK(std::lround(ring.x * 255.f) == long(c & 0xFFu) && std::lround(ring.y * 255.f) == long((c >> 8) & 0xFFu) &&
                  std::lround(ring.z * 255.f) == long((c >> 16) & 0xFFu));
        }
    }
    gfx::FrameRing ring(ctx);
    const int chapter = chapterOf(show::Effect::Plasma);
    const modern::SceneDetail detail{1.f / 64};
    std::unique_ptr<modern::MediumVolume> volume;
    for (const double local : {3.3, 8.05, 12.6}) {
        const show::TimelineFrame f = at(chapter, local);
        const modern::GridMediumDesc desc = modern::nebulaMedium(f, detail);
        if (!volume)
            volume = std::make_unique<modern::MediumVolume>(ctx, ring, desc.size, modern::kNebulaMediumShader);
        const Plane voxels = bake(ctx, ring, *volume, desc, f.localSeconds);
        // What the camera sees face-on: each column of the slab inside the 1993 screen's picture (|x| <= 16,
        // |y| <= 12) counts once, with the mean emission of its gas.
        double sum[3] = {0, 0, 0};
        std::size_t counted = 0;
        const Vec3 lo = modern::kPlasmaBoxLo, hi = modern::kPlasmaBoxHi;
        for (std::uint32_t y = 0; y < desc.size[1]; ++y)
            for (std::uint32_t x = 0; x < desc.size[0]; ++x) {
                const float wx = lo.x + (hi.x - lo.x) * (float(x) + 0.5f) / float(desc.size[0]);
                const float wy = lo.y + (hi.y - lo.y) * (float(y) + 0.5f) / float(desc.size[1]);
                if (std::abs(wx) > 16.f || std::abs(wy) > 12.f)
                    continue;
                double column[3] = {0, 0, 0};
                std::size_t gas = 0;
                for (std::uint32_t z = 0; z < desc.size[2]; ++z) {
                    if (!(voxels.at3(x, y, z, 0) > 0.05f))
                        continue;
                    for (int c = 0; c < 3; ++c)
                        column[c] += voxels.at3(x, y, z, 1 + c);
                    ++gas;
                }
                if (gas == 0)
                    continue;
                for (int c = 0; c < 3; ++c)
                    sum[c] += column[c] / double(gas);
                ++counted;
            }
        // The 1993 picture at the same 1993 frame: every pixel's palette colour, in linear light.
        const auto retroFrame = std::uint32_t(std::floor(f.localSeconds * 35.0));
        const float retroSeconds = float(double(retroFrame) / 35.0);
        retro::Palette palette{};
        retro::plasmaPalette(palette, f, retroFrame);
        double then[3] = {0, 0, 0};
        for (int py = 0; py < 200; ++py)
            for (int px = 0; px < 320; ++px) {
                const std::uint32_t index = modern::plasmaRingIndex(modern::plasmaField(float(px), float(py), retroSeconds));
                const std::uint32_t c = palette[retro::kPlasmaFirstIndex + index];
                then[0] += srgbToLinear((c & 0xFFu) / 255.0);
                then[1] += srgbToLinear(((c >> 8) & 0xFFu) / 255.0);
                then[2] += srgbToLinear(((c >> 16) & 0xFFu) / 255.0);
            }
        const double now = hueOf(sum[0], sum[1], sum[2]), before = hueOf(then[0], then[1], then[2]);
        std::printf("(h) PLASMA at %.2f s (1993 frame %u): mean emission hue %.1f deg over %zu columns, the 1993 "
                    "picture's %.1f deg\n",
                    local, retroFrame, now, counted, before);
        CHECK(counted > 400);
        CHECK(hueDistance(now, before) <= 15.0);
    }
    volume.reset();
    idle(ctx);
}

// --- (t), (r) through the renderer ----------------------------------------------------------------------------

struct Rendered {
    Plane volume;
    Camera camera;
};

// Renders chapter frames (Export, forced 2026) and reads the volume layer of each.
std::vector<Rendered> renderVolumes(gfx::Context& ctx, show::Effect effect, const std::vector<double>& locals,
                                    std::uint32_t width, std::uint32_t height, std::uint32_t spp,
                                    modern::SceneDetail detail) {
    render::RenderSettings settings{width, height};
    settings.hud = false;
    settings.mode = render::RenderMode::Export;
    settings.spp = spp;
    settings.detail = detail;
    gfx::FrameRing ring(ctx);
    std::vector<Rendered> out;
    {
        render::Renderer renderer(ctx, ring, settings);
        const int chapter = chapterOf(effect);
        std::vector<double> times;
        for (const double local : locals)
            times.push_back(show::chapterStart(chapter) + local);
        renderer.prepare(times);
        const modern::Nebula scene{detail};
        for (const double time : times) {
            show::TimelineFrame f = show::sampleTimeline(time);
            f.era = 1.f;
            f.wipe = 1.f;
            f.eraCut = 0.f;
            VkCommandBuffer cmd = ring.begin();
            renderer.render(cmd, f);
            const PendingRead read = recordRead(ctx, cmd, renderer.modern().tracer()->gbuffer().volume);
            ring.wait(ring.submit());
            idle(ctx);
            out.push_back({finishRead(read), scene.camera(f)});
        }
        idle(ctx);
    }
    return out;
}

void testProjectedCentre(gfx::Context& ctx) {
    constexpr std::uint32_t kW = 160, kH = 90;
    const int chapter = chapterOf(show::Effect::Nebula);
    const double local = 0.5 * modern::nebulaTimes(at(chapter, 0.0)).approachEnd; // the drift towards the wall
    const std::vector<Rendered> r = renderVolumes(ctx, show::Effect::Nebula, {local}, kW, kH, 2, kTestDetail);
    const Vec3 centre = (modern::kNebulaBoxLo + modern::kNebulaBoxHi) * 0.5f;
    const auto pixel = project(r[0].camera, centre, kW, kH);
    CHECK(pixel[0] >= 0 && pixel[0] < kW && pixel[1] >= 0 && pixel[1] < kH);
    const auto x = std::uint32_t(pixel[0]), y = std::uint32_t(pixel[1]);
    const float t = r[0].volume.at(x, y, 3);
    const float luminance =
        0.2126f * r[0].volume.at(x, y, 0) + 0.7152f * r[0].volume.at(x, y, 1) + 0.0722f * r[0].volume.at(x, y, 2);
    std::printf("(t) NEBULA at %.2f s: the cloud's centre projects to (%u, %u): T %.3f, volume luminance %.4f\n",
                local, x, y, t, luminance);
    CHECK(t < 0.8f);
    CHECK(luminance > 0.f);
}

// PLASMA on its accent: the camera has crossed into the cloud, which covers the frame's centre (T < 0.5).
void testPlasmaInside(gfx::Context& ctx) {
    constexpr std::uint32_t kW = 160, kH = 90;
    const int chapter = chapterOf(show::Effect::Plasma);
    const double accent = at(chapter, 0.0).accentSeconds;
    const std::vector<Rendered> r = renderVolumes(ctx, show::Effect::Plasma, {accent}, kW, kH, 2, kTestDetail);
    double t = 0;
    for (std::uint32_t y = kH / 2 - 1; y <= kH / 2; ++y)
        for (std::uint32_t x = kW / 2 - 1; x <= kW / 2; ++x)
            t += 0.25 * r[0].volume.at(x, y, 3);
    std::printf("(t) PLASMA on its accent (%.2f s): T %.3f at the frame's centre\n", accent, t);
    CHECK(t < 0.5);
}

// --- (u) PLASMA PLANET's sunrise
// -------------------------------------------------------------------------------

void testSunrise() {
    const int chapter = chapterOf(show::Effect::PlasmaPlanet);
    const modern::PlasmaPlanetTimes times = modern::plasmaPlanetTimes(at(chapter, 0.0));
    const double end = at(chapter, 0.0).chapterDuration;
    std::printf("(u) PLASMA PLANET: crane %.3f s, sunrise (D) %.3f s = show %.3f, low pass %.3f s\n",
                times.craneStart, times.sunrise, show::chapterStart(chapter) + times.sunrise, times.lowPass);
    CHECK(std::abs(show::chapterStart(chapter) + times.sunrise - 468.55) < 0.03);
    // The sun stays behind the planet until D and clears the limb on D, within a frame: from then on it is up.
    double rise = -1;
    float previous = -1e9f;
    for (double t = 0.0; t < end; t += 1.0 / 240.0) {
        const float clearance = modern::plasmaPlanetSunClearance(modern::nebulaCamera(at(chapter, t)).eye);
        if (t < times.sunrise - 1.0 / 60.0)
            CHECK(clearance < 0.f);
        if (t >= times.sunrise)
            CHECK(clearance >= 0.f);
        if (previous < 0.f && clearance >= 0.f && rise < 0)
            rise = t;
        previous = clearance;
    }
    std::printf("(u) the sun's disc centre clears the limb at %.4f s (D %.4f s)\n", rise, times.sunrise);
    CHECK(std::abs(rise - times.sunrise) < 1.0 / 60.0);
    // The shots change on the bar lines.
    CHECK(modern::nebulaCamera(at(chapter, times.craneStart - 1e-3)).shot == 0u &&
          modern::nebulaCamera(at(chapter, times.craneStart + 1e-3)).shot == 1u);
    CHECK(modern::nebulaCamera(at(chapter, times.sunrise + 1e-3)).shot == 2u &&
          modern::nebulaCamera(at(chapter, times.lowPass + 1e-3)).shot == 3u);
}

// The smallest clearance between the camera's line to star A and the pillars' analytic capsules (negative:
// the line passes through a pillar).
float heroClearance(const Camera& camera) {
    const Vec3 hero = modern::kNebulaStarCentres[0];
    const Vec3 d = hero - camera.eye;
    const float length = modern::length(d);
    float least = 1e9f;
    for (float s = 0.f; s < length - 3.f; s += 0.25f)
        least = std::min(least, modern::nebulaPillarDistance(camera.eye + d * (s / length)));
    return least;
}

void testReveal(gfx::Context& ctx) {
    constexpr std::uint32_t kW = 160, kH = 90;
    const int chapter = chapterOf(show::Effect::Nebula);
    const show::TimelineFrame head = at(chapter, 0.0);
    const modern::NebulaTimes times = modern::nebulaTimes(head);
    const double before = times.downbeat - times.barSeconds, accent = head.accentSeconds;
    std::printf("(r) NEBULA: approach ends %.3f s, D %.3f s (bar %.3f s), accent %.3f s\n", times.approachEnd,
                times.downbeat, times.barSeconds, accent);
    CHECK(times.approachEnd > 0.0 && times.approachEnd < times.downbeat && times.downbeat <= accent);
    // The analytic pillars: the line to A runs inside the centre pillar a bar before D and clears every
    // pillar by more than their noise on the accent and to the end.
    const modern::Nebula scene{kTestDetail};
    const float hidden = heroClearance(scene.camera(at(chapter, before)));
    const float shown = heroClearance(scene.camera(at(chapter, accent)));
    std::printf("(r) the camera's line to A clears the pillars by %.2f a bar before D, %.2f on the accent\n", hidden,
                shown);
    CHECK(hidden < -modern::kNebulaPillarNoise);
    CHECK(shown > 0.f);
    for (double t = accent; t < head.chapterDuration; t += 0.25)
        CHECK(heroClearance(scene.camera(at(chapter, t))) > 0.f);
    // Through the path tracer: the transmittance to A's disc.
    const modern::SceneDetail detail{1.f / 16};
    const std::vector<Rendered> r = renderVolumes(ctx, show::Effect::Nebula, {before, accent}, kW, kH, 2, detail);
    float transmittance[2];
    for (int i = 0; i < 2; ++i) {
        const auto pixel = project(r[i].camera, modern::kNebulaStarCentres[0], kW, kH);
        CHECK(pixel[0] >= 1 && pixel[0] < kW - 1 && pixel[1] >= 1 && pixel[1] < kH - 1);
        transmittance[i] = r[i].volume.at(std::uint32_t(pixel[0]), std::uint32_t(pixel[1]), 3);
    }
    std::printf("(r) the transmittance to star A: %.3f a bar before D, %.3f on the accent\n", transmittance[0],
                transmittance[1]);
    CHECK(transmittance[0] < 0.1f);
    CHECK(transmittance[1] > 0.3f);
}

// --- Look ------------------------------------------------------------------------------------------------------

void renderLook(gfx::Context& ctx, const std::filesystem::path& dir, std::uint32_t spp, float detail) {
    std::uint32_t kW = 480, kH = 270;
    if (const char* size = std::getenv("EVO_LOOK_SIZE")) // "WxH", a development aid
        std::sscanf(size, "%ux%u", &kW, &kH);
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
    const int plasma = chapterOf(show::Effect::Plasma), nebula = chapterOf(show::Effect::Nebula);
    const show::TimelineFrame plasmaHead = at(plasma, 0.0), nebulaHead = at(nebula, 0.0);
    const modern::NebulaTimes times = modern::nebulaTimes(nebulaHead);
    std::vector<Shot> shots = {
        {show::Effect::Plasma, 7.5, "plasma_1_wipe"},
        {show::Effect::Plasma, plasmaHead.accentSeconds - 0.6, "plasma_2_dolly"},
        {show::Effect::Plasma, plasmaHead.accentSeconds + 0.4, "plasma_3_inside"},
        {show::Effect::Plasma, plasmaHead.chapterDuration - 0.5, "plasma_4_deep"},
        {show::Effect::Nebula, 1.0, "nebula_1_wall"},
        {show::Effect::Nebula, times.approachEnd - 0.5, "nebula_2_closer"},
        {show::Effect::Nebula, times.approachEnd + 3.0, "nebula_3_gap"},
        {show::Effect::Nebula, times.downbeat - 0.4, "nebula_4_front"},
        {show::Effect::Nebula, nebulaHead.accentSeconds + 0.1, "nebula_5_reveal"},
        {show::Effect::Nebula, nebulaHead.accentSeconds + 4.0, "nebula_6_orbit"},
        {show::Effect::Nebula, nebulaHead.chapterDuration - 0.5, "nebula_7_end"},
    };
    if (const char* only = std::getenv("EVO_LOOK_ONLY")) {
        std::vector<Shot> kept;
        for (const Shot& s : shots)
            if (std::string(s.name).find(only) != std::string::npos)
                kept.push_back(s);
        shots = kept;
    }
    if (const char* local = std::getenv("EVO_LOOK_NEBULA_AT")) // a development aid: one NEBULA frame
        shots = {{show::Effect::Nebula, std::stod(local), "nebula_at"}};
    std::vector<double> times2;
    for (const Shot& s : shots)
        times2.push_back(show::chapterStart(s.effect == show::Effect::Plasma ? plasma : nebula) + s.local);
    // A development aid: any show times ("t1,t2,..."), of any chapter, named by their time.
    std::vector<std::string> names;
    if (const char* list = std::getenv("EVO_LOOK_SHOW_TIMES")) {
        shots.clear();
        times2.clear();
        std::string text = list;
        for (std::size_t at = 0; at < text.size();) {
            const std::size_t comma = std::min(text.find(',', at), text.size());
            const std::string item = text.substr(at, comma - at);
            times2.push_back(std::stod(item));
            names.push_back("t" + item);
            shots.push_back({show::Effect::Nebula, 0.0, names.back().c_str()});
            at = comma + 1;
        }
        for (std::size_t i = 0; i < shots.size(); ++i)
            shots[i].name = names[i].c_str();
    }
    renderer.prepare(times2);
    std::filesystem::create_directories(dir);
    for (std::size_t i = 0; i < shots.size(); ++i) {
        const auto start = std::chrono::steady_clock::now();
        show::TimelineFrame f = show::sampleTimeline(times2[i]);
        if (!std::getenv("EVO_LOOK_KEEP_ERA")) {
            f.era = 1.f;
            f.wipe = 1.f;
            f.eraCut = 0.f;
        }
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
        std::fflush(stdout);
    }
    idle(ctx);
}

} // namespace

int main(int argc, char** argv) {
    const auto start = std::chrono::steady_clock::now();
    test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR); // the build's compiled shaders
    try {
        auto ctx = gfx::Context::create({.validation = true});
        if (!ctx.validationEnabled())
            std::printf("warning: validation layer unavailable; only the results are checked\n");
        if (argc >= 4 && std::string(argv[1]) == "--volume") {
            // A development aid for the integrator's noise: NEBULA two seconds after the accent at 160x90,
            // detail 1/16, `spp` samples, its raw volume layer (before the denoiser) written as floats.
            const int chapter = chapterOf(show::Effect::Nebula);
            const double local = at(chapter, 0.0).accentSeconds + 2.0;
            const std::vector<Rendered> r = renderVolumes(ctx, show::Effect::Nebula, {local}, 160, 90,
                                                          std::uint32_t(std::stoul(argv[2])), {1.f / 16});
            std::FILE* out = std::fopen(argv[3], "wb");
            if (!out)
                throw std::runtime_error("cannot write the volume layer");
            std::fwrite(r[0].volume.values.data(), sizeof(float), r[0].volume.values.size(), out);
            std::fclose(out);
            return ctx.validationMessageCount() == 0 ? 0 : 1;
        }
        if (argc >= 3 && std::string(argv[1]) == "--look") {
            renderLook(ctx, argv[2], argc >= 4 ? std::uint32_t(std::stoul(argv[3])) : 16u,
                       argc >= 5 ? std::stof(argv[4]) : 1.f);
            return ctx.validationMessageCount() == 0 ? 0 : 1;
        }
        const char* only = std::getenv("EVO_TEST_ONLY"); // a development aid: run one check by its letter
        const auto timed = [&](const char* name, auto&& run) {
            if (only && std::string(only) != name)
                return;
            const auto t0 = std::chrono::steady_clock::now();
            run();
            std::printf("  [%s %.2f s]\n", name, secondsSince(t0));
            std::fflush(stdout);
        };
        timed("k", [&] { testCards(); });
        timed("m", [&] { testStars(ctx); });
        timed("b", [&] { testBake(ctx); });
        timed("h", [&] { testPlasmaHue(ctx); });
        timed("t", [&] {
            testProjectedCentre(ctx);
            testPlasmaInside(ctx);
        });
        timed("u", [&] { testSunrise(); });
        timed("r", [&] { testReveal(ctx); });
        timed("s", [&] {
            // The sweep at PLASMA PLANET's scale (its orbit is thousands of units out), then the two clouds'
            // own camera steps at theirs, and the planet's view turning smoothly.
            test::SweepLimits limits;
            limits.maxCameraStep = 40.f;
            const test::SweepStats stats = test::checkSceneSweep(SceneId::Nebula, limits);
            CHECK(stats.frames > 3000);
            for (const show::Effect e : {show::Effect::Plasma, show::Effect::Nebula, show::Effect::PlasmaPlanet}) {
                const int chapter = chapterOf(e);
                const double duration = at(chapter, 0.0).chapterDuration;
                double worstStep = 0, worstTurn = 0;
                Camera previous = modern::nebulaCamera(at(chapter, 0.0));
                for (double t = 1.0 / 60.0; t < duration; t += 1.0 / 60.0) {
                    const Camera c = modern::nebulaCamera(at(chapter, t));
                    if (c.shot == previous.shot) {
                        const Vec3 a = modern::normalize(c.target - c.eye), b = modern::normalize(previous.target - previous.eye);
                        worstStep = std::max({worstStep, double(modern::length(c.eye - previous.eye)),
                                              double(modern::length(c.target - previous.target))});
                        worstTurn = std::max(worstTurn, std::acos(std::clamp(double(modern::dot(a, b)), -1.0, 1.0)) * 57.29578);
                    }
                    previous = c;
                }
                std::printf("(s) %s: camera steps at most %.3f units and %.3f degrees a frame within a shot\n",
                            show::effectName(e), worstStep, worstTurn);
                CHECK(worstTurn < 1.0);
                if (e != show::Effect::PlasmaPlanet)
                    CHECK(worstStep <= 0.6);
            }
        });
        timed("p", [&] { test::checkChapterPictures(ctx, SceneId::Nebula); });
        CHECK(ctx.validationMessageCount() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    std::printf("scene_nebula: %d checks passed in %.1f s\n", checksPassed, secondsSince(start));
    return 0;
}
