// The City scene (VECTOR CITY, ASCENT, CITY NIGHT and METROPOLIS: step 5 Task 23), with synchronization
// validation:
//   - the card's numbers at full detail: the light tree's lights (every lamp and beacon), the windows, and the
//     instances, lights and splats of every chapter within the scene's capacity;
//   - the CPU sweep over every 1/60 s (cameras, shots, capacities, transforms, lights);
//   - VECTOR CITY flies the 1993 flight (retro::cityPoseAt) until its accent and stands on its streets
//     (retro::cityLot);
//   - the light tree in CITY NIGHT and METROPOLIS: the frame's light 0 stands for the tree, nearly every lamp is
//     lit, and on the GPU the picture with the tree's lights off is far darker;
//   - the wet street: where the street mirrors a sign, the wet street is at least twice as bright as the same
//     street dry;
//   - the windows: a close view of a tower changes as its windows switch over time, and is byte for byte the
//     same at one time reached by two seek paths;
//   - the standard chapter pictures (20 %, the accent, 80 %; the accent twice, byte for byte).
//
// `scene_city_test --probe <show seconds> [detail]` prints a frame's camera and contents instead.

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "modern/modern_scene.hpp"
#include "modern/path_tracer.hpp"
#include "modern/scene.hpp"
#include "modern/scene_gpu.hpp"
#include "modern/scenes/city.hpp"
#include "render/modern_stage.hpp"
#include "render/renderer.hpp"
#include "retro/city_plan.hpp"
#include "show/timeline.hpp"

#include "scene_fixture.hpp"
#include "test_env.hpp"

#include <algorithm>
#include <chrono>
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

constexpr modern::SceneDetail kTestDetail{1.f / 256};
constexpr show::Effect kChapters[] = {show::Effect::City, show::Effect::Ascent, show::Effect::CityNight,
                                      show::Effect::Metropolis};

int chapterOf(show::Effect effect) {
    for (int c = 0; c < show::chapterCount(); ++c)
        if (show::sampleTimeline(show::chapterStart(c)).effect == effect)
            return c;
    throw std::runtime_error("no chapter shows the effect");
}

double secondsInto(show::Effect effect, double local) {
    return show::chapterStart(chapterOf(effect)) + local;
}

show::TimelineFrame head(show::Effect effect) {
    return show::sampleTimeline(secondsInto(effect, 0));
}

show::TimelineFrame forced2026(double seconds) {
    show::TimelineFrame frame = show::sampleTimeline(seconds);
    frame.era = 1.f;
    frame.wipe = 1.f;
    frame.eraCut = 0.f;
    return frame;
}

// --- The card and the plan (CPU) -----------------------------------------------------------------------------

void testCard() {
    const auto start = std::chrono::steady_clock::now();
    const modern::City city({1.f});
    const modern::CityStats& s = city.stats();
    const double built = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("card: %u tree lights (%u lamps, %u beacons), %u buildings, %llu windows, %u sign lights, %u "
                "static instances; built in %.2f s\n",
                s.treeLights, s.lamps, s.beacons, s.buildings, static_cast<unsigned long long>(s.windows),
                s.signLights, city.staticInstanceCount(), built);
    CHECK(s.treeLights == modern::kCityCardLights);
    CHECK(s.treeLights == s.lamps + s.beacons);
    CHECK(s.treeLights <= 65536); // the light tree's capacity (rt::kMaxTreeLights)
    CHECK(s.windows >= modern::kCityCardWindows);
    const modern::SceneStatic& statics = city.staticScene();
    CHECK(statics.capacity.treeLights == s.treeLights && statics.capacity.spheres == s.treeLights);
    CHECK(statics.capacity.instances <= 200000); // the plan's per-scene TLAS capacity
    CHECK(statics.capacity.lights <= 64);
    // Every chapter's frames at full detail stay within the capacity (a few frames each; the sweep runs at the
    // test detail), and refit the tree only: the lights' centres, radii and emissions never change.
    std::uint32_t mostInstances = 0;
    for (show::Effect e : kChapters) {
        for (double f : {0.1, 0.5, 0.9}) {
            const show::TimelineFrame t = show::sampleTimeline(secondsInto(e, f * head(e).chapterDuration));
            const modern::SceneFrame frame = city.frame(t);
            mostInstances = std::max(mostInstances, std::uint32_t(frame.instances.size()));
            CHECK(frame.instances.size() <= statics.capacity.instances);
            CHECK(frame.treeLights.size() == s.treeLights);
            CHECK(frame.splats && frame.splats->count <= statics.capacity.splats);
            modern::checkSceneFrame(statics, frame);
        }
    }
    const modern::City small(kTestDetail);
    CHECK(small.stats().treeLights < s.treeLights);
    std::printf("card: \"%s\"; at most %u instances in a frame (capacity %u); test detail: %u tree lights, %u "
                "static instances\n",
                modern::kCityCardLine, mostInstances, statics.capacity.instances, small.stats().treeLights,
                small.staticInstanceCount());
}

void testSweep() {
    evo::test::SweepLimits limits;
    limits.maxCameraStep = 1.5f; // the avenue at 200 km/h is 0.92 m a frame
    limits.detail = kTestDetail;
    const evo::test::SweepStats stats = evo::test::checkSceneSweep(SceneId::City, limits);
    CHECK(stats.shots == 9); // VECTOR CITY 2, ASCENT 1, CITY NIGHT 4, METROPOLIS 2
    CHECK(stats.frames > 6000);
}

// VECTOR CITY flies the 1993 flight until its accent: at every whole retro frame the camera is the 1993
// camera. And it stands on the 1993 streets.
void testFlightMatches1993() {
    const int chapter = chapterOf(show::Effect::City);
    const double start = show::chapterStart(chapter);
    const show::TimelineFrame first = show::sampleTimeline(start);
    int matched = 0;
    for (std::uint32_t k = 0; double(k) / retro::kCityRetroRate < first.accentSeconds; ++k) {
        const double local = double(k) / retro::kCityRetroRate;
        const show::TimelineFrame frame = show::sampleTimeline(start + local);
        show::TimelineFrame exact = frame;
        exact.trackSeconds += local - frame.localSeconds;
        exact.localSeconds = local; // the 2026 camera at the retro frame's time
        const modern::Camera c = modern::cityCamera(exact);
        const retro::CityPose p = retro::cityPoseAt(frame, local);
        const Vec3 forward = modern::normalize(c.target - c.eye);
        const Vec3 expected{float(std::sin(p.yaw) * std::cos(p.pitch)), float(std::sin(p.pitch)),
                            float(std::cos(p.yaw) * std::cos(p.pitch))};
        CHECK(modern::length(c.eye - Vec3{float(p.x), float(p.y), float(p.z)}) < 1e-3f);
        CHECK(modern::length(forward - expected) < 1e-4f);
        CHECK(c.fovYDegrees == float(retro::kCityFovY) && c.shot == 0);
        ++matched;
    }
    CHECK(matched > 300);
    // The 1993 boxes along the main street each have a 2026 instance with their footprint at their place.
    const modern::City city(kTestDetail);
    const modern::SceneFrame frame = city.frame(show::sampleTimeline(start + 5.0));
    int found = 0, boxes = 0;
    for (int m = 0; m < 4; ++m)
        for (int i = -1; i <= 0; ++i)
            for (const retro::CityBuilding& b : retro::cityLot(i, m)) {
                ++boxes;
                const float cx = float(0.5 * (b.base.x0 + b.base.x1)), cz = float(0.5 * (b.base.z0 + b.base.z1));
                const float w = float(b.base.x1 - b.base.x0), d = float(b.base.z1 - b.base.z0);
                for (const modern::SceneInstance& in : frame.instances) {
                    const auto& t = in.transform.m;
                    if (std::abs(t[0][3] - cx) < 1e-3f && std::abs(t[2][3] - cz) < 1e-3f &&
                        std::abs(t[0][0] - w) < 1e-3f && std::abs(t[2][2] - d) < 1e-3f) {
                        ++found;
                        break;
                    }
                }
            }
    CHECK(found == boxes);
    std::printf("flight: the camera is 1993's on %d retro frames to the accent; %d of %d 1993 boxes along the "
                "main street stand in 2026\n",
                matched, found, boxes);
}

// The light tree in CITY NIGHT and METROPOLIS: light 0 is the tree, its sphere instance is there, nearly all
// lamps are lit. VECTOR CITY: most lamps strike in the wave from D.
void testTreeLightsActive() {
    const modern::City city(kTestDetail);
    for (show::Effect e : {show::Effect::CityNight, show::Effect::Metropolis}) {
        for (double f : {0.25, 0.75}) {
            const modern::SceneFrame frame =
                city.frame(show::sampleTimeline(secondsInto(e, f * head(e).chapterDuration)));
            CHECK(!frame.lights.empty() && frame.lights[0].kind == std::uint32_t(evo::rt::LightKind::Tree));
            CHECK(frame.treeLights.size() == city.stats().treeLights);
            std::uint32_t lit = 0;
            for (std::uint32_t k = 0; k < city.lampCount(); ++k)
                lit += frame.treeLights[k].scale > 0.2f;
            CHECK(lit > 0.95 * city.lampCount());
            bool treeInstance = false;
            for (const modern::SceneInstance& in : frame.instances)
                treeInstance = treeInstance || (in.light == 0 && in.kind == evo::rt::GeometryKind::Spheres &&
                                                in.sphereCount == frame.treeLights.size());
            CHECK(treeInstance);
        }
    }
    const show::TimelineFrame vc = head(show::Effect::City);
    auto litShare = [&](double local) {
        const modern::SceneFrame frame = city.frame(show::sampleTimeline(secondsInto(show::Effect::City, local)));
        std::uint32_t lit = 0;
        for (std::uint32_t k = 0; k < city.lampCount(); ++k)
            lit += frame.treeLights[k].scale > 0.5f;
        return double(lit) / city.lampCount();
    };
    const double d = modern::cityBar(show::sampleTimeline(secondsInto(show::Effect::City, 1.0)), 0);
    const double before = litShare(d - 1.0), after = litShare(vc.accentSeconds + 7.0);
    std::printf("tree: VECTOR CITY lamps lit %.2f a second before D (%.2f s), %.2f after the accent\n", before, d,
                after);
    CHECK(before < 0.2 && after > 0.9);
}

// --- GPU ------------------------------------------------------------------------------------------------------

constexpr std::uint32_t kW = 160, kH = 90;

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
    float luminance(int x, int y) const {
        return 0.2126f * at(x, y, 0) + 0.7152f * at(x, y, 1) + 0.0722f * at(x, y, 2);
    }
    double mean() const {
        double sum = 0;
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
                sum += luminance(int(x), int(y));
        return sum / double(width * height);
    }
};

gfx::Buffer recordRead(gfx::Context& ctx, VkCommandBuffer cmd, gfx::Image& image) {
    if (image.format() != VK_FORMAT_R16G16B16A16_SFLOAT)
        throw std::logic_error("recordRead: expected RGBA16F");
    const VkExtent2D extent = image.extent();
    gfx::Buffer buffer(ctx, {VkDeviceSize(extent.width) * extent.height * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                             "city test readback"});
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {extent.width, extent.height, 1};
    vkCmdCopyImageToBuffer(cmd, image.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle(), 1, &region);
    gfx::bufferBarrier2(cmd, buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    return buffer;
}

Plane finishRead(const gfx::Buffer& buffer, VkExtent2D extent) {
    buffer.invalidate();
    Plane plane;
    plane.width = extent.width;
    plane.height = extent.height;
    const std::size_t values = std::size_t(extent.width) * extent.height * 4;
    plane.rgba.resize(values);
    const auto* bytes = static_cast<const std::uint8_t*>(buffer.mapped());
    for (std::size_t i = 0; i < values; ++i) {
        std::uint16_t h;
        std::memcpy(&h, bytes + 2 * i, 2);
        plane.rgba[i] = halfToFloat(h);
    }
    return plane;
}

// The City with each frame changed by `edit` (the tree's lights off, a dry street, a fixed camera).
class EditedCity final : public modern::ModernScene {
public:
    EditedCity(modern::SceneDetail detail, std::function<void(modern::SceneFrame&)> edit)
        : city_(detail), edit_(std::move(edit)) {}
    const modern::SceneStatic& staticScene() const override { return city_.staticScene(); }
    modern::SceneFrame frame(const show::TimelineFrame& t) const override {
        modern::SceneFrame f = city_.frame(t);
        edit_(f);
        return f;
    }
    modern::Camera camera(const show::TimelineFrame& t) const override { return frame(t).camera; }
    bool samplesRetro() const override { return false; }

private:
    modern::City city_;
    std::function<void(modern::SceneFrame&)> edit_;
};

modern::SceneFactory editedFactory(std::function<void(modern::SceneFrame&)> edit) {
    return [edit](SceneId id, modern::SceneDetail detail) -> std::unique_ptr<modern::ModernScene> {
        if (id != SceneId::City)
            return modern::makeScene(id, detail);
        return std::make_unique<EditedCity>(detail, edit);
    };
}

render::RenderSettings exportSettings(std::uint32_t spp, const modern::SceneFactory& factory) {
    render::RenderSettings s{kW, kH};
    s.hud = false;
    s.mode = render::RenderMode::Export;
    s.spp = spp;
    s.detail = kTestDetail;
    s.sceneFactory = factory;
    return s;
}

// What the lab's City changes in its frames.
enum class Edit { None, DryStreet, TreeOff, Windows };

// One export renderer at kW x kH whose City applies the current edit: HDR pictures (before the tonemap) of
// the same frames with and without a change, from one renderer (its pipelines built once).
class Lab {
public:
    Lab(gfx::Context& ctx, std::uint32_t spp)
        : ctx_(ctx), ring_(ctx),
          renderer_(ctx, ring_, exportSettings(spp, editedFactory([this](modern::SceneFrame& f) { apply(f); }))) {}

    Plane render(double seconds, Edit edit) {
        edit_ = edit;
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, forced2026(seconds));
        gfx::Image& hdr = renderer_.modern().hdr();
        const gfx::Buffer read = recordRead(ctx_, cmd, hdr);
        ring_.wait(ring_.submit());
        idle(ctx_);
        return finishRead(read, hdr.extent());
    }

private:
    void apply(modern::SceneFrame& f) const {
        if (edit_ == Edit::DryStreet)
            f.materials[modern::City::kAsphaltMaterial].coat = 0.f;
        if (edit_ == Edit::TreeOff || edit_ == Edit::Windows)
            for (modern::TreeLightFrame& l : f.treeLights)
                l.scale = 0.f;
        if (edit_ == Edit::Windows) {
            // A fixed close view of a tower's facade by the avenue, lit by nothing but its windows: no lamps,
            // signs, traffic or glow, so the picture is the windows (and a little sky).
            f.camera = modern::Camera{};
            f.camera.eye = {modern::kAvenueX - 3.f, 14.f, 880.f};
            f.camera.target = {modern::kAvenueX - 40.f, 18.f, 885.f};
            f.camera.up = {0, 1, 0};
            f.camera.fovYDegrees = 40.f;
            f.camera.focusDistance = 30.f;
            f.lights.resize(1);
            for (modern::SceneInstance& in : f.instances)
                if (in.light != evo::rt::kNoLight && in.light != 0)
                    in.light = evo::rt::kNoLight;
            f.splats.reset();
        }
    }

    gfx::Context& ctx_;
    gfx::FrameRing ring_;
    Edit edit_ = Edit::None;
    render::Renderer renderer_; // last: its destructor waits for the ring
};

struct Pixel {
    float x, y;
    bool visible;
};

Pixel project(const modern::Camera& c, Vec3 p) {
    const modern::CameraBasis b = modern::cameraBasis(c);
    const Vec3 v = p - c.eye;
    const float z = dot(v, b.forward);
    const float t = std::tan(0.5f * c.fovYDegrees * 3.14159265f / 180.f);
    const float aspect = float(kW) / float(kH);
    if (!(z > 1e-3f))
        return {0, 0, false};
    const float nx = dot(v, b.right) / (z * t * aspect), ny = dot(v, b.up) / (z * t);
    const float x = (nx + 1.f) * 0.5f * kW, y = (1.f - ny) * 0.5f * kH;
    return {x, y, x >= 1 && y >= 1 && x < kW - 1 && y < kH - 1};
}

// The tree's lights off: CITY NIGHT and METROPOLIS go far darker (the lamps light the streets through the tree).
void testTreeLightsLight(Lab& lab) {
    for (show::Effect e : {show::Effect::CityNight, show::Effect::Metropolis}) {
        const double seconds = secondsInto(e, 0.5 * head(e).chapterDuration);
        const Plane on = lab.render(seconds, Edit::None);
        const Plane off = lab.render(seconds, Edit::TreeOff);
        std::printf("tree: %s mean HDR luminance %.4f with the tree's lights, %.4f without\n",
                    show::sampleTimeline(seconds).title, on.mean(), off.mean());
        CHECK(on.mean() > 1.3 * off.mean());
    }
}

// The wet street mirrors the signs: where a sign's reflection lands on the street (seen from CITY NIGHT's
// crossing, 20 cm over a puddle), the wet street is at least twice as bright as the same
// street dry (its coat, the wetness, at 0).
void testWetStreet(Lab& lab) {
    const modern::City city(kTestDetail);
    int tested = 0, passed = 0;
    const show::TimelineFrame night = show::sampleTimeline(secondsInto(show::Effect::CityNight, 1.0));
    for (const double local : {double(modern::cityBar(night, -1))}) {
        const double seconds = secondsInto(show::Effect::CityNight, local);
        const modern::SceneFrame frame = city.frame(forced2026(seconds));
        const Plane wet = lab.render(seconds, Edit::None);
        const Plane dryPicture = lab.render(seconds, Edit::DryStreet);
        for (std::size_t k = 1; k < frame.lights.size(); ++k) {
            const evo::rt::RectLight& l = frame.lights[k];
            if (!(l.emission[0] + l.emission[1] + l.emission[2] > 0.f))
                continue; // dark (the red sign before the accent)
            const Vec3 centre = Vec3{l.corner[0], l.corner[1], l.corner[2]} +
                                0.5f * Vec3{l.edgeU[0], l.edgeU[1], l.edgeU[2]} +
                                0.5f * Vec3{l.edgeV[0], l.edgeV[1], l.edgeV[2]};
            // A one-sided light: only a face turned towards the camera's side shows in the street.
            const Vec3 eye = frame.camera.eye;
            const Vec3 normal = modern::cross(Vec3{l.edgeU[0], l.edgeU[1], l.edgeU[2]},
                                              Vec3{l.edgeV[0], l.edgeV[1], l.edgeV[2]});
            if (!(dot(normal, eye - centre) > 0.f))
                continue;
            // The sign's mirror image in the street (y = 0), seen from the camera, lands where the line to it
            // crosses the street.
            const Vec3 image{centre.x, -centre.y, centre.z};
            const Vec3 onStreet = eye + (image - eye) * (eye.y / (eye.y - image.y));
            if (std::abs(onStreet.x - modern::kNightStreetX) > 5.f || modern::length(centre - eye) > 60.f)
                continue; // on the pavement, not the street, or a sign too far to resolve
            const Pixel p = project(frame.camera, onStreet);
            if (!p.visible)
                continue;
            // The reflection's brightest pixel within one of the projected point (it is a small mirror image),
            // against the same pixels dry.
            const int x = int(p.x), y = int(p.y);
            float a = 0.f, b = 0.f;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    a = std::max(a, wet.luminance(x + dx, y + dy));
                    b = std::max(b, dryPicture.luminance(x + dx, y + dy));
                }
            ++tested;
            passed += a >= 2.f * b && a > 0.f;
            std::printf("wet street at %.2f s: sign light %zu mirrored at (%d, %d): wet %.4f, dry %.4f\n", local, k,
                        x, y, a, b);
        }
    }
    CHECK(tested >= 2);
    CHECK(passed * 3 >= tested * 2); // two in three at least (a passing car may cover one)
}

// The windows switch over time, and at one time reached by two seek paths the picture is the same.
void testWindows(Lab& lab) {
    const double t0 = secondsInto(show::Effect::Metropolis, 2.0);
    const Plane fresh = lab.render(t0 + 9.0, Edit::Windows);
    lab.render(t0 + 31.0, Edit::Windows); // forwards past it, then back
    lab.render(t0, Edit::Windows);
    const Plane seeked = lab.render(t0 + 9.0, Edit::Windows);
    const Plane later = lab.render(t0 + 30.0, Edit::Windows);
    CHECK(fresh.rgba == seeked.rgba);
    // Windows that switched: pixels much brighter or darker 21 s later (the rest is the sky's faint noise).
    std::size_t switched = 0;
    for (std::uint32_t y = 0; y < kH; ++y)
        for (std::uint32_t x = 0; x < kW; ++x) {
            const float a = fresh.luminance(int(x), int(y)), b = later.luminance(int(x), int(y));
            switched += std::abs(a - b) > 0.1f + 0.5f * std::min(a, b);
        }
    std::printf("windows: the same picture at %.2f s by two seek paths; %zu of %u pixels switched 21 s later\n",
                t0 + 9.0, switched, kW * kH);
    CHECK(switched > 20);
}

// --- Probe ---------------------------------------------------------------------------------------------------

void probe(double seconds, float detail) {
    const modern::City city({detail});
    const show::TimelineFrame t = show::sampleTimeline(seconds);
    const modern::SceneFrame f = city.frame(t);
    std::printf("%s at %.3f (local %.3f, D %.3f): eye (%.2f, %.2f, %.2f) target (%.2f, %.2f, %.2f) fov %.1f "
                "shot %u\n",
                t.title, seconds, t.localSeconds, modern::cityBar(t, 0), f.camera.eye.x, f.camera.eye.y,
                f.camera.eye.z, f.camera.target.x, f.camera.target.y, f.camera.target.z, f.camera.fovYDegrees,
                f.camera.shot);
    std::printf("instances %zu, lights %zu, tree lights %zu, splats %u, exposure %.2f\n", f.instances.size(),
                f.lights.size(), f.treeLights.size(), f.splats ? f.splats->count : 0u, f.exposureEV);
    for (std::size_t k = 0; k < f.instances.size(); ++k) {
        const modern::SceneInstance& in = f.instances[k];
        if (in.kind != evo::rt::GeometryKind::Triangles)
            continue;
        const auto& m = in.transform.m;
        // The eye inside an instance's oriented box (unit meshes).
        float local[3];
        const Vec3 d = f.camera.eye - Vec3{m[0][3], m[1][3], m[2][3]};
        bool inside = true;
        for (int a = 0; a < 3; ++a) {
            const Vec3 axis{m[0][a], m[1][a], m[2][a]};
            const float len2 = dot(axis, axis);
            local[a] = dot(d, axis) / len2;
            inside = inside && std::abs(local[a]) < 0.5f;
        }
        if (inside)
            std::printf("  the eye is inside instance %zu (mesh %u, material %u)\n", k, in.mesh, in.material);
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 3 && std::string(argv[1]) == "--probe") {
            probe(std::stod(argv[2]), argc >= 4 ? std::stof(argv[3]) : 1.f);
            return 0;
        }
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        const auto start = std::chrono::steady_clock::now();
        auto stamp = [&](const char* what) {
            std::printf("[%6.1f s] %s\n",
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), what);
        };
        testCard();
        stamp("card");
        testFlightMatches1993();
        testTreeLightsActive();
        stamp("flight, tree");
        testSweep();
        stamp("sweep");
        {
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            else if (!ctx.syncValidationEnabled())
                std::printf("SKIP sync validation: unavailable\n");
            {
                Lab lab(ctx, 2);
                testWetStreet(lab);
                stamp("wet street");
                testTreeLightsLight(lab);
                stamp("tree lights");
                testWindows(lab);
                stamp("windows");
            }
            idle(ctx);
            evo::test::PictureOptions options;
            options.detail = kTestDetail;
            evo::test::checkChapterPictures(ctx, SceneId::City, options);
            idle(ctx);
            stamp("pictures");
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("scene_city_test: %d checks passed\n", checksPassed);
    return 0;
}
