// The Glass scene (GLENZ, GLASS CATHEDRAL and CRYSTAL CITY; step 5 Task 20), with synchronization validation:
//   - the cards' numbers against the full-detail configuration (photons, crystal towers, panes);
//   - the glenz: the 2026 star's vertices are the 1993 star's (retro::glenzWorldVertices) scaled into metres,
//     at every retro frame of the chapter, and its core is the same star scaled 0.82;
//   - the CPU sweep over every 1/60 s of the three chapters, with every frame's caustics checked;
//   - the music: GLENZ's tubes go dark for the bar after the accent while the key flares; the cathedral's
//     sun breaks through on its accent; the city's sun clears the haze on its;
//   - Beer-Lambert inside glass, and thin-walled panes, in the camera's paths and in the photons, against
//     closed forms in a small lab scene;
//   - the standard chapter pictures (20 %, the accent, 80 %; the accent twice, byte for byte);
//   - GLENZ at the accent: the floor's caustics hold red (r > 1.3 b) and blue (b > 1.3 r) pixels within 6 px
//     of each other with the flint's dispersion, and neither with Abbe 0;
//   - the cathedral after its accent: the floor's caustic map holds coloured pools of sunlight from the
//     stained glass (red and blue among them), at least 5 times what it held under the clouds.

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/sync.hpp"
#include "image/png.hpp"
#include "modern/caustics.hpp"
#include "modern/mesh_builder.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene.hpp"
#include "modern/scene_gpu.hpp"
#include "modern/scenes/glass.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "retro/glenz.hpp"
#include "show/timeline.hpp"

#include "scene_fixture.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
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

void idle(gfx::Context& ctx) {
    vkQueueWaitIdle(ctx.queue());
}

int chapterOf(show::Effect effect) {
    for (const int c : evo::test::chaptersOf(SceneId::Glass))
        if (show::sampleTimeline(show::chapterStart(c)).effect == effect)
            return c;
    throw std::runtime_error("the Glass scene does not show this effect");
}

show::TimelineFrame forced2026(double seconds) {
    show::TimelineFrame frame = show::sampleTimeline(seconds);
    frame.era = 1.f;
    frame.wipe = 1.f;
    frame.eraCut = 0.f;
    return frame;
}

show::TimelineFrame atLocal(show::Effect effect, double local) {
    return forced2026(show::chapterStart(chapterOf(effect)) + local);
}

// --- The cards and the glenz (CPU) ---------------------------------------------------------------------------

void testCards() {
    const modern::Glass full({1.f});
    const modern::GlassCard glenz = modern::glassCard(show::Effect::Glenz);
    CHECK(glenz.exportPhotons == 2097152u && glenz.exportPhotons == modern::kGlenzPhotons * modern::kExportPhotonFactor);
    CHECK(glenz.line == "2026 · DISPERSION · 2,097,152 PHOTONS · CAUSTICS");
    CHECK(glenz.abbe == modern::kFlintAbbe && glenz.glassObjects == 2);
    const modern::GlassCard cathedral = modern::glassCard(show::Effect::Cathedral);
    CHECK(cathedral.exportPhotons == 4194304u);
    CHECK(cathedral.glassObjects == 64 && modern::glassPillarCount() == 64);
    CHECK(cathedral.line == "2026 · GLASS CATHEDRAL · 64 GLASS PILLARS · 4,194,304 PHOTONS");
    const modern::GlassCard city = modern::glassCard(show::Effect::CrystalCity);
    CHECK(city.glassObjects == full.crystalTowers() && full.crystalTowers() == 400);
    CHECK(city.line == "2026 · CRYSTAL CITY · 400 PRISMS · DISPERSION · CAUSTICS");
    // The photons a frame shoots at full detail are the card's over the export factor.
    const show::TimelineFrame f = atLocal(show::Effect::Glenz, 3.0);
    CHECK(full.frame(f).caustics->photons == modern::kGlenzPhotons);
    const modern::Glass small({1.f / 256});
    CHECK(small.frame(f).caustics->photons == modern::kGlenzPhotons / 256);
    CHECK(small.crystalTowers() == full.crystalTowers());
    std::printf("cards: \"%s\" / \"%s\" / \"%s\"; %u stained panes\n", glenz.line.c_str(), cathedral.line.c_str(),
                city.line.c_str(), full.stainedPanes());
}

void testGlenzMatches1993() {
    const int chapter = chapterOf(show::Effect::Glenz);
    const double start = show::chapterStart(chapter);
    const show::TimelineFrame head = show::sampleTimeline(start);
    const auto frames = static_cast<std::uint32_t>(head.chapterDuration * retro::kGlenzRetroRate);
    const modern::Glass glass({1.f / 256});
    const retro::GlenzMesh& mesh = retro::glenzMesh();
    float worst = 0;
    std::uint32_t compared = 0;
    for (std::uint32_t r = 0; r < frames; r += 3) {
        const double local = double(r) / retro::kGlenzRetroRate;
        const show::TimelineFrame f = show::sampleTimeline(start + local);
        const rt::Mat34 star = modern::glenzTransform(f, local);
        const retro::GlenzVertices want = modern::glenzVerticesIn2026(f, r);
        const retro::GlenzVertices old = retro::glenzWorldVertices(f, r);
        for (std::size_t i = 0; i < retro::kGlenzVertices; ++i) {
            const Vec3 p = modern::transformPoint(
                star, {float(mesh.vertices[i][0]), float(mesh.vertices[i][1]), float(mesh.vertices[i][2])});
            const Vec3 w{float(want[i][0]), float(want[i][1]), float(want[i][2])};
            worst = std::max(worst, modern::length(p - w));
            // The fixed scale: 2026 = 0.35 * 1993 + the lift.
            CHECK(std::abs(w.x - modern::kGlenzWorldScale * float(old[i][0])) < 1e-6f);
            CHECK(std::abs(w.y - modern::kGlenzWorldScale * float(old[i][1]) - modern::glenzWorldLift()) < 1e-5f);
            CHECK(p.y >= modern::kGlenzFloorGap * 0.99f); // never through the floor
        }
        // The frame places the star (instance 0) and its core (instance 1) so.
        const modern::SceneFrame frame = glass.frame(f);
        const rt::Mat34& a = frame.instances[0].transform;
        const rt::Mat34& b = frame.instances[1].transform;
        const rt::Mat34 core = modern::compose(
            star, modern::compose(modern::rotate({0, 1, 0}, modern::kGlenzCoreTurn),
                                  modern::scale(Vec3{1, 1, 1} * modern::kGlenzCoreScale)));
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 4; ++col) {
                CHECK(std::abs(a.m[row][col] - star.m[row][col]) < 1e-6f);
                CHECK(std::abs(b.m[row][col] - core.m[row][col]) < 1e-5f);
            }
        ++compared;
    }
    CHECK(worst < 2e-5f);
    CHECK(compared > 200);
    std::printf("glenz: the 2026 star = the 1993 star x %.2f m at %u retro frames (worst %.2g m)\n",
                modern::kGlenzWorldScale, compared, worst);
}

void testSweep() {
    evo::test::SweepLimits limits;
    limits.maxCameraStep = 0.6f;
    const evo::test::SweepStats stats = evo::test::checkSceneSweep(SceneId::Glass, limits);
    CHECK(stats.shots == 10);
    CHECK(stats.frames > 4000);
    // Every frame's caustics are valid (every 1/10 s of the three chapters).
    const modern::Glass glass({1.f / 256});
    int frames = 0;
    for (const int chapter : evo::test::chaptersOf(SceneId::Glass)) {
        const double start = show::chapterStart(chapter);
        const show::TimelineFrame head = show::sampleTimeline(start);
        for (double t = 0; t < head.chapterDuration; t += 0.1) {
            const modern::SceneFrame f = glass.frame(show::sampleTimeline(start + t));
            CHECK(f.caustics.has_value());
            modern::checkCausticFrame(glass.staticScene(), f, glass.staticScene().capacity.causticResolution);
            ++frames;
        }
    }
    std::printf("sweep: %u frames, %u shots, at most %u instances, %u lights; caustics valid in %d frames\n",
                stats.frames, stats.shots, stats.maxInstances, stats.maxLights, frames);
}

float emission(const modern::SceneFrame& f, std::uint32_t light) {
    const rt::RectLight& l = f.lights[light];
    return std::max({l.emission[0], l.emission[1], l.emission[2]});
}

void testMusic() {
    const modern::Glass glass({1.f / 256});
    // GLENZ: lights 0 (the hard key), 1 (the soft key) and 5.. (the tubes).
    const show::TimelineFrame head = atLocal(show::Effect::Glenz, 0.0);
    const double accent = head.accentSeconds;
    const modern::SceneFrame before = glass.frame(atLocal(show::Effect::Glenz, accent - 0.3));
    const modern::SceneFrame on = glass.frame(atLocal(show::Effect::Glenz, accent + 0.05));
    // The soft key until the accent, the hard spot from it (lights 1 and 0), and the photons with them.
    CHECK(emission(before, 0) == 0.f && emission(before, 1) > 0.f && before.caustics->light == 1);
    CHECK(emission(on, 0) > 0.f && emission(on, 1) == 0.f && on.caustics->light == 0);
    // Warm and cool on alternate beats after the accent's downbeat.
    const modern::SceneFrame beat1 = glass.frame(atLocal(show::Effect::Glenz, accent + 0.6 * head.beatSeconds));
    const modern::SceneFrame beat2 = glass.frame(atLocal(show::Effect::Glenz, accent + 1.6 * head.beatSeconds));
    const auto warmth = [](const modern::SceneFrame& fr) { return fr.lights[0].emission[0] / fr.lights[0].emission[2]; };
    CHECK(std::abs(warmth(beat1) - warmth(beat2)) > 0.2f);
    // The cathedral's shutters: the hero window opens on the accent's downbeat, then one bay per beat.
    const show::TimelineFrame c0 = atLocal(show::Effect::Cathedral, 0.0);
    auto open = [&](double local) {
        int n = 0;
        for (int k = 0; k < 15; ++k)
            n += modern::cathedralShutter(atLocal(show::Effect::Cathedral, local), k) > 0.99f;
        return n;
    };
    const int shut = open(c0.accentSeconds - 2.0), atAccent = open(c0.accentSeconds - 0.05),
              later = open(c0.accentSeconds + 6.0 * c0.beatSeconds + 0.4);
    CHECK(shut == 2 && atAccent == 3 && later >= 8);
    // The city's sun clears the haze.
    const show::TimelineFrame k0 = atLocal(show::Effect::CrystalCity, 0.0);
    const modern::SceneFrame hazy = glass.frame(atLocal(show::Effect::CrystalCity, k0.accentSeconds - 0.5));
    const modern::SceneFrame clear = glass.frame(atLocal(show::Effect::CrystalCity, k0.accentSeconds + 1.0));
    CHECK(clear.environment.discRadiance.x > 1.8f * hazy.environment.discRadiance.x);
    // GLENZ holds the 1993 camera through its accent (the caustic snaps into focus in a held frame), and
    // drops to the floor two bars later.
    const modern::Camera held = glass.camera(atLocal(show::Effect::Glenz, accent + 0.01));
    CHECK(held.shot == 0 && glass.camera(atLocal(show::Effect::Glenz, accent - 0.01)).shot == 0);
    CHECK(held.fovYDegrees == float(retro::glenzCamera().fovYDegrees) && held.eye.x == 0.f &&
          std::abs(held.eye.z + float(retro::kGlenzCameraDistance) * modern::kGlenzWorldScale) < 1e-6f);
    CHECK(glass.camera(atLocal(show::Effect::Glenz, accent + 4.0)).shot == 1);
    std::printf("music: GLENZ's key turns hard on the accent; the cathedral's shutters open %d, %d, %d\n", shut,
                atAccent, later);
}

// --- Reading the tracer's images ---------------------------------------------------------------------------

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

struct Plane {
    std::uint32_t width = 0, height = 0;
    std::vector<float> rgba;
    float at(int x, int y, int c) const { return rgba[(std::size_t(y) * width + std::size_t(x)) * 4 + c]; }
    Vec3 rgb(int x, int y) const { return {at(x, y, 0), at(x, y, 1), at(x, y, 2)}; }
    // The mean over a square window around (x, y).
    Vec3 mean(int cx, int cy, int half) const {
        Vec3 sum{};
        int n = 0;
        for (int y = cy - half; y <= cy + half; ++y)
            for (int x = cx - half; x <= cx + half; ++x) {
                sum = sum + rgb(x, y);
                ++n;
            }
        return sum / float(n);
    }
};

struct Pending {
    gfx::Buffer buffer;
    VkExtent2D extent;
    bool half;
};

Pending recordRead(gfx::Context& ctx, VkCommandBuffer cmd, gfx::Image& image) {
    const bool half = image.format() == VK_FORMAT_R16G16B16A16_SFLOAT;
    if (!half && image.format() != VK_FORMAT_R32G32B32A32_SFLOAT)
        throw std::logic_error("recordRead: expected RGBA16F or RGBA32F");
    const VkExtent2D extent = image.extent();
    const VkDeviceSize texel = half ? 8 : 16;
    gfx::Buffer buffer(ctx, {VkDeviceSize(extent.width) * extent.height * texel, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             true, "glass test readback"});
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle(), 1, &region);
    gfx::bufferBarrier2(cmd, buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    return {std::move(buffer), extent, half};
}

Plane finishRead(const Pending& p) {
    p.buffer.invalidate();
    Plane plane;
    plane.width = p.extent.width;
    plane.height = p.extent.height;
    const std::size_t values = std::size_t(p.extent.width) * p.extent.height * 4;
    plane.rgba.resize(values);
    const auto* bytes = static_cast<const std::uint8_t*>(p.buffer.mapped());
    for (std::size_t i = 0; i < values; ++i) {
        if (p.half) {
            std::uint16_t h;
            std::memcpy(&h, bytes + 2 * i, 2);
            plane.rgba[i] = halfToFloat(h);
        } else {
            std::memcpy(&plane.rgba[i], bytes + 4 * i, 4);
        }
    }
    return plane;
}

struct Traced {
    Plane lighting, emission, volume;
};

// An export renderer kept across frames (one pipeline to build for many pictures): each render() gives the
// tracer's lighting (RGBA32F) and emission (RGBA16F; at a receiver, its caustic).
class Rig {
public:
    Rig(gfx::Context& ctx, std::uint32_t w, std::uint32_t h, std::uint32_t spp, modern::SceneDetail detail,
        modern::SceneFactory factory, modern::SceneMap map = {})
        : ctx_(ctx), ring_(ctx),
          renderer_(ctx, ring_, settings(w, h, spp, detail, std::move(factory), std::move(map))) {}
    ~Rig() { idle(ctx_); }

    Traced render(const show::TimelineFrame& frame) {
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        modern::GBuffer& g = renderer_.modern().tracer()->gbuffer();
        const Pending lighting = recordRead(ctx_, cmd, g.lighting);
        const Pending emission = recordRead(ctx_, cmd, g.emission);
        const Pending volume = recordRead(ctx_, cmd, g.volume);
        ring_.wait(ring_.submit());
        idle(ctx_);
        return {finishRead(lighting), finishRead(emission), finishRead(volume)};
    }

private:
    static render::RenderSettings settings(std::uint32_t w, std::uint32_t h, std::uint32_t spp,
                                           modern::SceneDetail detail, modern::SceneFactory factory,
                                           modern::SceneMap map) {
        render::RenderSettings s{w, h};
        s.hud = false;
        s.mode = render::RenderMode::Export;
        s.spp = spp;
        s.detail = detail;
        s.sceneFactory = std::move(factory);
        s.sceneMap = std::move(map);
        return s;
    }

    gfx::Context& ctx_;
    gfx::FrameRing ring_;
    render::Renderer renderer_; // last: its destructor waits for the ring
};

struct Pixel {
    float x, y;
    bool visible;
};

Pixel project(const modern::Camera& c, Vec3 p, std::uint32_t w, std::uint32_t h) {
    const modern::CameraBasis b = modern::cameraBasis(c);
    const Vec3 v = p - c.eye;
    const float z = dot(v, b.forward);
    const float t = std::tan(0.5f * c.fovYDegrees * 3.14159265f / 180.f);
    const float aspect = float(w) / float(h);
    if (!(z > 1e-3f))
        return {0, 0, false};
    const float nx = dot(v, b.right) / (z * t * aspect), ny = dot(v, b.up) / (z * t);
    const float x = (nx + 1.f) * 0.5f * float(w), y = (1.f - ny) * 0.5f * float(h);
    return {x, y, x >= 0 && y >= 0 && x < float(w) && y < float(h)};
}

constexpr std::uint32_t kW = 320, kH = 180;

// --- The lab: Beer-Lambert and thin panes against closed forms ---------------------------------------------

// A small test scene. View: the camera looks along +z through a glass body at z = 0 (a 1 m cube, or a thin
// pane) onto a white backdrop of radiance 1. Caustic: a rect light 4 m above a receiver floor throws the
// body's caustic (a 0.5 m slab, or a pane, 1.5 m up) onto it, seen from low in front.
struct LabConfig {
    bool caustic = false;
    bool pane = false;
    Vec3 absorption{0, 0, 0}; // the glass body's, per metre
    Vec3 tint{1, 1, 1};       // the pane's
    bool body = true;         // false: nothing between the lamp and the floor
};

// The scene reads the test's current configuration at every frame (its materials are the frame's).
class Lab final : public modern::ModernScene {
public:
    explicit Lab(const LabConfig* config) : config_(config) {
        static_.meshes = {modern::box({0.5f, 0.5f, 0.5f}), modern::quad(1.f, 1.f)};
        static_.materials = materials(*config);
        static_.capacity.causticResolution = 256;
    }
    static std::vector<rt::Material> materials(const LabConfig& config) {
        rt::Material glass;
        glass.baseColor[0] = config.tint.x;
        glass.baseColor[1] = config.tint.y;
        glass.baseColor[2] = config.tint.z;
        glass.baseColor2[0] = config.absorption.x;
        glass.baseColor2[1] = config.absorption.y;
        glass.baseColor2[2] = config.absorption.z;
        glass.roughness = 0;
        glass.ior = 1.5f;
        glass.transmission = 1;
        glass.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Glass);
        glass.flags = config.pane ? rt::kMaterialThinWalled : 0u;
        rt::Material backdrop;
        backdrop.baseColor[0] = backdrop.baseColor[1] = backdrop.baseColor[2] = 0;
        backdrop.emission[0] = backdrop.emission[1] = backdrop.emission[2] = 1;
        backdrop.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
        rt::Material floor;
        floor.baseColor[0] = floor.baseColor[1] = floor.baseColor[2] = 0.8f;
        floor.roughness = 0.9f;
        floor.ior = 1.f;
        floor.flags = rt::kMaterialCausticReceiver;
        floor.receiver = 0;
        rt::Material lamp = backdrop;
        lamp.emission[0] = lamp.emission[1] = lamp.emission[2] = 400.f;
        return {glass, backdrop, floor, lamp};
    }
    const modern::SceneStatic& staticScene() const override { return static_; }
    bool samplesRetro() const override { return false; }
    modern::Camera camera(const show::TimelineFrame&) const override {
        modern::Camera c;
        c.up = {0, 1, 0};
        c.fovYDegrees = config_->caustic ? 50.f : 20.f;
        c.eye = config_->caustic ? Vec3{0, 1.f, -3.f} : Vec3{0, 0, -4.f};
        c.target = config_->caustic ? Vec3{0, 0, 0} : Vec3{0, 0, 0};
        c.focusDistance = 1;
        return c;
    }
    modern::SceneFrame frame(const show::TimelineFrame& f) const override {
        modern::SceneFrame out;
        out.camera = camera(f);
        out.materials = materials(*config_);
        out.environment = modern::environmentDefaults(modern::EnvironmentKind::Space);
        out.environment.starDensity = 0;
        out.environment.zenith = out.environment.horizon = out.environment.ground = {0, 0, 0};
        const rt::Mat34 flat = modern::rotate({1, 0, 0}, -0.5f * 3.14159265f);
        if (!config_->caustic) {
            out.instances.push_back(
                {config_->pane ? 1u : 0u, 0u, config_->pane ? modern::identity() : modern::identity()});
            out.instances.push_back({1u, 1u, modern::compose(modern::translate({0, 0, 2.f}),
                                                             modern::compose(modern::rotate({0, 1, 0}, 3.14159265f),
                                                                             modern::scale({6, 6, 1})))});
            return out;
        }
        // The caster first, then the floor and the lamp.
        const rt::Mat34 body = config_->pane ? modern::compose(modern::translate({0, 1.5f, 0}), flat)
                                            : modern::compose(modern::translate({0, 1.5f, 0}),
                                                              modern::scale({1.f, 0.5f, 1.f}));
        out.instances.push_back({config_->pane ? 1u : 0u, 0u,
                                 config_->body ? body : modern::compose(modern::translate({0, -50.f, 0}), body)});
        const rt::Mat34 floorAt = modern::compose(flat, modern::scale({4, 4, 1}));
        out.instances.push_back({1u, 2u, floorAt});
        const rt::Mat34 lampAt =
            modern::compose(modern::translate({0, 4.f, 0}), modern::rotate({1, 0, 0}, 0.5f * 3.14159265f));
        out.lights.push_back(modern::rectLightForQuad(lampAt, 0.1f, 0.1f, rt::LightKind::Emitter, {400, 400, 400}, 1));
        out.instances.push_back({1u, 3u, modern::compose(lampAt, modern::scale({0.1f, 0.1f, 1})), 0u});
        modern::CausticFrame c;
        c.receivers.push_back(modern::receiverForQuad(flat, 4, 4, 256));
        c.photons = 1u << 18;
        c.light = 0;
        c.casterInstanceFirst = 0;
        c.casterInstanceCount = 1;
        out.caustics = c;
        return out;
    }

private:
    const LabConfig* config_;
    modern::SceneStatic static_;
};

bool near(float a, float b, float tolerance) {
    return std::abs(a - b) <= tolerance * std::max(std::abs(b), 1e-6f);
}

struct Bench;
Traced renderLab(Bench& bench, LabConfig config);

void testLab(Bench& bench) {
    // The view through glass: a 1 m cube against clear glass, and a tinted pane against a clear pane.
    const Vec3 sigma{1.f, 0.5f, 0.f};
    const Vec3 clear = renderLab(bench, {false, false, {0, 0, 0}, {1, 1, 1}}).lighting.mean(int(kW / 2), int(kH / 2), 6);
    const Vec3 absorbed = renderLab(bench, {false, false, sigma, {1, 1, 1}}).lighting.mean(int(kW / 2), int(kH / 2), 6);
    std::printf("lab, view: through 1 m of glass %.4f %.4f %.4f (clear %.4f %.4f %.4f)\n", absorbed.x, absorbed.y,
                absorbed.z, clear.x, clear.y, clear.z);
    CHECK(clear.x > 0.8f && clear.x < 0.98f); // (1 - R)^2 at normal incidence, R = 0.04: 0.92
    CHECK(near(absorbed.x / clear.x, std::exp(-1.f), 0.02f));
    CHECK(near(absorbed.y / clear.y, std::exp(-0.5f), 0.02f));
    CHECK(near(absorbed.z / clear.z, 1.f, 0.01f));
    const Vec3 tint{0.2f, 0.6f, 1.f};
    const Vec3 pane = renderLab(bench, {false, true, {0, 0, 0}, {1, 1, 1}}).lighting.mean(int(kW / 2), int(kH / 2), 6);
    const Vec3 stained = renderLab(bench, {false, true, {0, 0, 0}, tint}).lighting.mean(int(kW / 2), int(kH / 2), 6);
    // A pane passes (1 - R) / (1 + R) of its light (both faces and the bounces between them): 0.923 at IOR 1.5.
    std::printf("lab, view: a clear pane %.4f, a tinted one %.4f %.4f %.4f\n", pane.x, stained.x, stained.y,
                stained.z);
    CHECK(near(pane.x, 0.96f / 1.04f, 0.02f));
    CHECK(near(stained.x / pane.x, tint.x, 0.03f) && near(stained.y / pane.y, tint.y, 0.02f) &&
          near(stained.z / pane.z, tint.z, 0.01f));

    // The photons: the slab's caustic under it, absorbed over the slab's 0.5 m; the pane's, tinted.
    const LabConfig causticView{true};
    const modern::Camera cam = Lab(&causticView).camera(atLocal(show::Effect::Glenz, 2.0));
    const Pixel under = project(cam, {0, 0, 0}, kW, kH);
    CHECK(under.visible);
    const int ux = int(under.x), uy = int(under.y);
    const Vec3 slab = renderLab(bench, {true, false, {0, 0, 0}, {1, 1, 1}}).emission.mean(ux, uy, 2);
    const Vec3 slabAbsorbed = renderLab(bench, {true, false, sigma, {1, 1, 1}}).emission.mean(ux, uy, 2);
    std::printf("lab, photons: under the slab %.4f %.4f %.4f, absorbing %.4f %.4f %.4f\n", slab.x, slab.y, slab.z,
                slabAbsorbed.x, slabAbsorbed.y, slabAbsorbed.z);
    CHECK(slab.x > 0.f);
    CHECK(near(slabAbsorbed.x / slab.x, std::exp(-0.5f), 0.04f));
    CHECK(near(slabAbsorbed.y / slab.y, std::exp(-0.25f), 0.04f));
    CHECK(near(slabAbsorbed.z / slab.z, 1.f, 0.02f));
    // A pane is a filter, not a caustic: its light reaches the floor through next-event estimation (shadow
    // rays pass panes, tinted), and no photon lands for it. Under the pane, the lamp's light is its
    // transmission (1 - R) / (1 + R) of the light without it, times its tint.
    const Traced open = renderLab(bench, {true, true, {0, 0, 0}, {1, 1, 1}, false});
    const Traced clearPane = renderLab(bench, {true, true, {0, 0, 0}, {1, 1, 1}});
    const Traced tinted = renderLab(bench, {true, true, {0, 0, 0}, tint});
    const Vec3 lit = open.lighting.mean(ux, uy, 1), paneLit = clearPane.lighting.mean(ux, uy, 1),
               tintLit = tinted.lighting.mean(ux, uy, 1);
    std::printf("lab, panes: the floor under no pane %.4f, a clear pane %.4f, a tinted one %.4f %.4f %.4f; "
                "photon map %.4f\n",
                lit.x, paneLit.x, tintLit.x, tintLit.y, tintLit.z, clearPane.emission.mean(ux, uy, 1).x);
    CHECK(lit.x > 0.f);
    CHECK(near(paneLit.x / lit.x, 0.96f / 1.04f, 0.03f));
    CHECK(near(tintLit.x / paneLit.x, tint.x, 0.03f) && near(tintLit.y / paneLit.y, tint.y, 0.03f) &&
          near(tintLit.z / paneLit.z, tint.z, 0.01f));
    CHECK(clearPane.emission.mean(ux, uy, 1).x == 0.f);
}

// --- GLENZ's rainbows and the cathedral's pools (GPU) --------------------------------------------------------


struct Colours {
    int red = 0, blue = 0, pairs = 0, lit = 0;
};

// The caustic pixels of an emission image (the floor's map as the camera sees it) above a brightness, the red
// (r > ratio b) and blue (b > ratio r) among them, and how many red pixels have a blue one within `reach` px.
Colours colours(const Plane& e, float threshold, float ratio, int reach,
                const std::function<bool(int, int)>& inside = {}) {
    Colours c;
    std::vector<char> red(std::size_t(e.width) * e.height, 0), blue(red.size(), 0);
    for (int y = 0; y < int(e.height); ++y)
        for (int x = 0; x < int(e.width); ++x) {
            const Vec3 v = e.rgb(x, y);
            if (0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z < threshold || (inside && !inside(x, y)))
                continue;
            ++c.lit;
            const std::size_t i = std::size_t(y) * e.width + std::size_t(x);
            red[i] = v.x > ratio * v.z;
            blue[i] = v.z > ratio * v.x;
            c.red += red[i];
            c.blue += blue[i];
        }
    for (int y = 0; y < int(e.height); ++y)
        for (int x = 0; x < int(e.width); ++x) {
            if (!red[std::size_t(y) * e.width + std::size_t(x)])
                continue;
            bool found = false;
            for (int dy = -reach; dy <= reach && !found; ++dy)
                for (int dx = -reach; dx <= reach && !found; ++dx) {
                    const int u = x + dx, v = y + dy;
                    if (u >= 0 && v >= 0 && u < int(e.width) && v < int(e.height) && dx * dx + dy * dy <= reach * reach)
                        found = blue[std::size_t(v) * e.width + std::size_t(u)] != 0;
                }
            c.pairs += found;
        }
    return c;
}

// The show's Glass scene with GLENZ's core clear (dispersion is then the only colour on the floor), its flint
// at Abbe `abbe[*which]` (the test switches between them from frame to frame; every Glass frame carries its
// whole material table).
class Switch final : public modern::ModernScene {
public:
    Switch(modern::SceneDetail d, const int* which, const bool* overhead, const modern::Camera* const* view)
        : which_(which), overhead_(overhead), view_(view) {
        for (const float abbe : {modern::kFlintAbbe, 0.f}) {
            modern::GlassOptions o;
            o.abbe = abbe;
            o.clearCore = true;
            variants_.push_back(std::make_unique<modern::Glass>(d, o));
        }
    }
    const modern::SceneStatic& staticScene() const override { return variants_[0]->staticScene(); }
    bool samplesRetro() const override { return false; }
    // With `overhead` set, the camera looks straight down on the cathedral's nave from under its lamps.
    modern::Camera camera(const show::TimelineFrame& f) const override {
        modern::Camera c = variants_[0]->camera(f);
        if (*view_ != nullptr)
            return **view_;
        if (*overhead_) {
            c.eye = {0.f, 9.f, 30.f};
            c.target = {0.f, 0.f, 30.f};
            c.up = {0.f, 0.f, 1.f};
            c.fovYDegrees = 90.f;
            c.aperture = 0.f;
        }
        return c;
    }
    modern::SceneFrame frame(const show::TimelineFrame& f) const override {
        modern::SceneFrame out = variants_[std::size_t(*which_)]->frame(f);
        out.camera = camera(f);
        return out;
    }

private:
    const int* which_;
    const bool* overhead_;
    const modern::Camera* const* view_; // a camera of the test's, when set
    std::vector<std::unique_ptr<modern::Glass>> variants_;
};

// One renderer for every check here (one ray tracing pipeline to build): the Glass scene through Switch, and
// the lab in the Ship's slot, shown in place of GLENZ while `lab` is set.
struct Bench {
    explicit Bench(gfx::Context& ctx)
        : rig(ctx, kW, kH, 2, {1.f / 8},
              [this](SceneId id, modern::SceneDetail d) -> std::unique_ptr<modern::ModernScene> {
                  if (id == SceneId::Glass)
                      return std::make_unique<Switch>(d, &which, &overhead, &view);
                  if (id == SceneId::Ship)
                      return std::make_unique<Lab>(&labConfig);
                  return nullptr;
              },
              [this](show::Effect e) { return lab ? SceneId::Ship : modern::sceneFor(e); }) {}
    int which = 0;
    bool overhead = false;
    const modern::Camera* view = nullptr;
    bool lab = false;
    LabConfig labConfig;
    Rig rig; // last: it holds pointers to the fields above
};

Traced renderLab(Bench& bench, LabConfig config) {
    bench.lab = true;
    bench.labConfig = config;
    Traced t = bench.rig.render(atLocal(show::Effect::Glenz, 2.0));
    bench.lab = false;
    return t;
}

void testRainbowsAndPools(Bench& bench) {
    int& which = bench.which;
    bool& overhead = bench.overhead;
    const modern::Camera*& view = bench.view;
    Rig& rig = bench.rig;

    // GLENZ just after its accent, on a beat where the key is cool (the warm one is itself red against blue).
    const show::TimelineFrame head = atLocal(show::Effect::Glenz, 0.0);
    double local = head.accentSeconds + 0.05;
    for (int i = 0; i < 4; ++i) {
        const modern::SceneFrame fr = modern::Glass({1.f / 256}).frame(atLocal(show::Effect::Glenz, local));
        if (fr.lights[0].emission[2] >= fr.lights[0].emission[0])
            break;
        local += head.beatSeconds;
    }
    const show::TimelineFrame frame = atLocal(show::Effect::Glenz, local);
    which = 0;
    const Plane dispersed = rig.render(frame).emission;
    which = 1;
    const Plane plain = rig.render(frame).emission;
    // Only the floor in front of the copper drum (whose tubes are coloured emitters in the picture): the pixels
    // whose camera ray reaches y = 0 at z < 2 within the receiver.
    const modern::Camera cam = modern::glassCamera(frame);
    const modern::CameraBasis basis = modern::cameraBasis(cam);
    const float tanHalf = std::tan(0.5f * cam.fovYDegrees * 3.14159265f / 180.f);
    auto onFloor = [&](int x, int y) {
        const float nx = (2.f * (float(x) + 0.5f) / float(kW) - 1.f) * tanHalf * float(kW) / float(kH);
        const float ny = (1.f - 2.f * (float(y) + 0.5f) / float(kH)) * tanHalf;
        const Vec3 d = basis.forward + basis.right * nx + basis.up * ny;
        if (!(d.y < 0.f))
            return false;
        const Vec3 hit = cam.eye + d * (-cam.eye.y / d.y);
        return std::abs(hit.x) < 3.5f && hit.z > -3.f && hit.z < 2.f;
    };
    const Colours a = colours(dispersed, 0.05f, 1.3f, 6, onFloor);
    const Colours b = colours(plain, 0.05f, 1.3f, 6, onFloor);
    std::printf("glenz at %.2f s: Abbe %.1f: %d caustic pixels, %d red, %d blue, %d red with blue within 6 px; "
                "Abbe 0: %d, %d red, %d blue\n",
                local, modern::kFlintAbbe, a.lit, a.red, a.blue, a.pairs, b.lit, b.red, b.blue);
    CHECK(a.lit > 200);
    CHECK(a.red > 0 && a.blue > 0 && a.pairs > 0);
    CHECK(b.lit > 200);
    CHECK(b.red == 0 && b.blue == 0);

    // The cathedral, seen straight down the middle of the nave: before the accent the sun comes in only at the
    // door's end; six beats after it, the wave has opened the hero window and five more, and the pillars they
    // light throw their spectra on the floor.
    which = 0;
    overhead = true;
    const show::TimelineFrame c0 = atLocal(show::Effect::Cathedral, 0.0);
    const Plane early = rig.render(atLocal(show::Effect::Cathedral, c0.accentSeconds - 0.4)).emission;
    const Plane late = rig.render(atLocal(show::Effect::Cathedral, c0.accentSeconds + 6.5 * c0.beatSeconds)).emission;
    double before = 0, after = 0;
    for (std::size_t i = 0; i < late.rgba.size(); i += 4) {
        before += early.rgba[i] + early.rgba[i + 1] + early.rgba[i + 2];
        after += late.rgba[i] + late.rgba[i + 1] + late.rgba[i + 2];
    }
    const Colours c = colours(late, 0.02f, 1.3f, 6);
    std::printf("cathedral: caustic light seen on the floor %.3g before the accent, %.3g six beats after; %d lit "
                "pixels, %d red, %d blue, %d pairs\n",
                before, after, c.lit, c.red, c.blue, c.pairs);
    CHECK(after > 3.0 * before);
    CHECK(c.lit > 100 && c.red > 0 && c.blue > 0);

    // The shafts: in the open middle of the nave, the haze where the hero window's sunbeam (open since the
    // accent's downbeat) crosses the centre line, against the haze where the shut window beside it would
    // cross it; seen from between the western pillar rows, so that no pillar ends the camera's rays first.
    overhead = false;
    const Vec3 sun = modern::normalize({0.80f, 0.47f, -0.37f});
    const float bay = 3.4f, firstPillar = 5.f, wall = 10.5f + 0.32f, middle = 6.3f;
    auto crossing = [&](float windowZ) { // where the beam from the window's middle reaches x = 0
        const Vec3 w{wall, middle, windowZ};
        const float t = w.x / sun.x;
        return w - sun * t + Vec3{0.f, 1.2f, 0.f};
    };
    const Vec3 onShaft = crossing(firstPillar + bay * 4.5f), offShaft = crossing(firstPillar + bay * 3.5f);
    modern::Camera across;
    across.eye = {-2.5f, onShaft.y, 0.5f * (onShaft.z + offShaft.z)};
    across.target = {0.f, onShaft.y, across.eye.z};
    across.up = {0, 1, 0};
    across.fovYDegrees = 70.f;
    view = &across;
    const Traced shafts = rig.render(atLocal(show::Effect::Cathedral, c0.accentSeconds + 1.0));
    view = nullptr;
    const Pixel inBeam = project(across, onShaft, kW, kH), beside = project(across, offShaft, kW, kH);
    CHECK(inBeam.visible && beside.visible);
    auto volumeLuminance = [&](Pixel p) {
        const Vec3 v = shafts.volume.mean(int(p.x), int(p.y), 2);
        return 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z;
    };
    const float lit = volumeLuminance(inBeam), dark = volumeLuminance(beside);
    std::printf("cathedral shafts: volume luminance %.4f in the sunbeam, %.4f beside it (%.1fx)\n", lit, dark,
                lit / std::max(dark, 1e-9f));
    CHECK(lit >= 1.5f * dark && lit > 0.f);
}

// --- A look at the caustic maps (not a check) -------------------------------------------------------------

// The show's Glass scene seen from straight above the floor (the camera replaced), for looking at caustics.
class Overhead final : public modern::ModernScene {
public:
    Overhead(modern::SceneDetail d, modern::GlassOptions o, Vec3 at, float height)
        : glass_(d, o), at_(at), height_(height) {}
    const modern::SceneStatic& staticScene() const override { return glass_.staticScene(); }
    bool samplesRetro() const override { return false; }
    modern::Camera camera(const show::TimelineFrame& f) const override {
        modern::Camera c = glass_.camera(f);
        c.eye = at_ + Vec3{0, height_, 0};
        c.target = at_;
        c.up = {0, 0, 1};
        c.fovYDegrees = 50;
        c.aperture = 0;
        return c;
    }
    modern::SceneFrame frame(const show::TimelineFrame& f) const override {
        modern::SceneFrame out = glass_.frame(f);
        out.camera = camera(f);
        return out;
    }

private:
    modern::Glass glass_;
    Vec3 at_;
    float height_;
};

void writeLook(const Plane& p, const std::string& path, float gain) {
    std::vector<std::uint8_t> rgb(std::size_t(p.width) * p.height * 3);
    for (std::size_t i = 0; i < std::size_t(p.width) * p.height; ++i)
        for (int c = 0; c < 3; ++c) {
            const float v = std::max(p.rgba[4 * i + c], 0.f) * gain;
            rgb[3 * i + c] = static_cast<std::uint8_t>(std::lround(255.f * std::pow(v / (1.f + v), 1.f / 2.2f)));
        }
    evo::image::writePng(path, int(p.width), int(p.height), rgb.data());
}

void look(gfx::Context& ctx, const std::string& dir, double local, float detail, bool clearCore,
          show::Effect effect = show::Effect::Glenz, Vec3 at = {-0.2f, 0.f, 0.6f}, float height = 4.5f) {
    modern::GlassOptions o;
    o.clearCore = clearCore;
    modern::SceneFactory factory = [o, at, height](SceneId id, modern::SceneDetail d) -> std::unique_ptr<modern::ModernScene> {
        return id == SceneId::Glass ? std::make_unique<Overhead>(d, o, at, height) : nullptr;
    };
    Rig rig(ctx, 480, 480, 1, {detail}, factory);
    const Traced t = rig.render(atLocal(effect, local));
    const std::string path = dir + "/caustic_" + std::to_string(int(effect)) + "_" + std::to_string(int(local * 100)) +
                             (clearCore ? "_clear" : "") + ".png";
    writeLook(t.emission, path, 2.f);
    std::printf("look: %s\n", path.c_str());
}

} // namespace

int main(int argc, char** argv) {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        const std::string only = argc > 1 ? argv[1] : "";
        if (only == "look" && argc > 2) {
            auto ctx = gfx::Context::create({});
            const double local = argc > 3 ? std::atof(argv[3]) : 12.6;
            if (argc > 4 && std::string(argv[4]) == "city") {
                look(ctx, argv[2], local, 1.f, false, show::Effect::CrystalCity, {0.f, 0.f, 50.f}, 30.f);
                return 0;
            }
            look(ctx, argv[2], local, 1.f, false);
            look(ctx, argv[2], local, 1.f, true);
            return 0;
        }
        auto run = [&](const char* name) { return only.empty() || only == name; };
        if (run("cpu")) {
            testCards();
            testGlenzMatches1993();
            testMusic();
            testSweep();
        }
        auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
        if (!ctx.validationEnabled())
            std::printf("warning: validation layer unavailable; only the pixels are checked\n");
        else if (!ctx.syncValidationEnabled())
            std::printf("SKIP sync validation: unavailable\n");
        if (run("lab") || run("rainbows")) {
            Bench bench(ctx);
            if (run("lab"))
                testLab(bench);
            if (run("rainbows"))
                testRainbowsAndPools(bench);
        }
        if (run("pictures")) {
            evo::test::checkChapterPictures(ctx, SceneId::Glass);
            idle(ctx);
        }
        CHECK(ctx.validationMessageCount() == 0);
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("scene_glass_test: %d checks passed\n", checksPassed);
    return 0;
}
