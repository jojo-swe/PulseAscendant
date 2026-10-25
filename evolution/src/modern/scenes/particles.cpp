#include "modern/scenes/particles.hpp"

#include "modern/mesh_builder.hpp"
#include "modern/scenes/logo_targets.hpp"
#include "retro/dots_schedule.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace evo::modern {

namespace {

// The shader's float constants, written the same way so that both round them alike.
constexpr float kPi = 3.14159265f;
constexpr float kTau = 6.28318531f;
constexpr float kFullCount = 2097152.f;

// --- The shader's helpers
// -----------------------------------------------------------------------------------

float golden(std::uint32_t i) {
    return logoUnit(i * 0x9E3779B9u);
}

float hashUnit(std::uint32_t x) {
    return logoUnit(logoHash(x));
}

float smooth01(float x) {
    x = std::clamp(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}

Vec3 turn(Vec3 p, float yaw, float pitch) {
    const float cy = std::cos(yaw), sy = std::sin(yaw), cx = std::cos(pitch), sx = std::sin(pitch);
    const float x = cy * p.x + sy * p.z, z = -sy * p.x + cy * p.z;
    return {x, cx * p.y - sx * z, sx * p.y + cx * z};
}

Vec3 sphereTarget(std::uint32_t id, std::uint32_t count) {
    const std::uint32_t shell = id % kDotsSphereShells, k = id / kDotsSphereShells;
    const std::uint32_t n = (count - 1u - shell) / kDotsSphereShells + 1u;
    const float y = static_cast<float>(2u * k + 1u) / static_cast<float>(n) - 1.f;
    const float ring = std::sqrt(std::max(1.f - y * y, 0.f));
    const float angle = kTau * (golden(k) + 0.37f * static_cast<float>(shell));
    const float radius = kDotsSphereRadius * (1.f - kDotsShellStep * static_cast<float>(shell));
    return radius * Vec3{ring * std::cos(angle), y, ring * std::sin(angle)};
}

Vec3 cubeTarget(std::uint32_t id, std::uint32_t count) {
    const std::uint32_t n = dotsLatticeSide(count);
    const std::uint32_t j = id % (n * n * n);
    const Vec3 lattice{static_cast<float>(j % n), static_cast<float>(j / (n * n)),
                       static_cast<float>((j / n) % n)};
    const float side = static_cast<float>(n - 1u);
    return Vec3{lattice.x / side - 0.5f, lattice.y / side - 0.5f, lattice.z / side - 0.5f} * kDotsCubeSide;
}

Vec3 knotTarget(std::uint32_t id, std::uint32_t count) {
    const float t = kTau * (static_cast<float>(id) + 0.5f) / static_cast<float>(count);
    const float c2 = std::cos(2.f * t), s2 = std::sin(2.f * t), c3 = std::cos(3.f * t),
                s3 = std::sin(3.f * t);
    const float r = kDotsKnotMajor + kDotsKnotMinor * c3;
    const Vec3 centre{r * c2, kDotsKnotLift * kDotsKnotMinor * s3, r * s2};
    const Vec3 tangent = normalize(Vec3{-3.f * kDotsKnotMinor * s3 * c2 - 2.f * r * s2,
                                        3.f * kDotsKnotLift * kDotsKnotMinor * c3,
                                        -3.f * kDotsKnotMinor * s3 * s2 + 2.f * r * c2});
    const Vec3 normal = normalize(cross(tangent, Vec3{0.f, 1.f, 0.f}));
    const Vec3 binormal = cross(normal, tangent);
    const float phi = kTau * golden(id);
    const float tube = kDotsKnotTube * (1.f - 0.15f * static_cast<float>(id % 3u));
    return centre + tube * (std::cos(phi) * normal + std::sin(phi) * binormal);
}

// The curl flow's six plane waves (the shader's kCurl* tables).
constexpr Vec3 kCurlWave[6] = {{2.1f, 0.7f, -1.3f}, {-1.1f, 2.6f, 0.9f},  {0.8f, -1.4f, 3.1f},
                               {4.3f, 1.9f, 2.2f},  {-2.7f, -3.8f, 1.6f}, {1.5f, 4.6f, -3.9f}};
constexpr Vec3 kCurlAxis[6] = {{0.f, 0.f, 1.f},   {1.f, 0.f, 0.f},   {0.f, 1.f, 0.f},
                               {0.6f, 0.f, 0.8f}, {0.f, 0.8f, 0.6f}, {0.8f, 0.6f, 0.f}};
constexpr float kCurlAmplitude[6] = {0.5f, 0.45f, 0.4f, 0.25f, 0.22f, 0.18f};
constexpr float kCurlSpeed[6] = {0.9f, -1.1f, 1.3f, 1.7f, -1.9f, 2.3f};

Vec3 curl(Vec3 p, float t) {
    Vec3 d{};
    for (int k = 0; k < 6; ++k) {
        const float phase = dot(kCurlWave[k], p) + kCurlSpeed[k] * t + 1.7f * static_cast<float>(k);
        d = d + kCurlAmplitude[k] * std::cos(phase) * normalize(cross(kCurlWave[k], kCurlAxis[k]));
    }
    return d;
}

constexpr DotsShape kOrder[7] = {DotsShape::Cube, DotsShape::Sphere, DotsShape::Knot, DotsShape::Logo,
                                 DotsShape::Knot, DotsShape::Sphere, DotsShape::Cube};

constexpr Vec3 kGlow[4] = {
    {0.12f, 0.27f, 0.9f}, {0.55f, 0.85f, 1.f}, {1.f, 0.5f, 0.15f}, {1.f, 0.82f, 0.55f}};

// The Gerstner waves (the shader's kWave* tables).
constexpr Vec2 kWaveDirection[8] = {{0.94f, 0.34f}, {0.77f, 0.64f}, {0.99f, -0.14f}, {0.6f, -0.8f},
                                    {0.87f, 0.5f},  {0.34f, 0.94f}, {0.97f, 0.24f},  {0.71f, -0.71f}};
constexpr float kWaveNumber[8] = {0.4488f, 0.6614f, 0.8976f, 1.1855f, 1.6111f, 2.244f, 2.992f, 4.1888f};
constexpr float kWaveSpeed[8] = {2.0983f, 2.5472f, 2.9674f, 3.4102f, 3.9755f, 4.692f, 5.4177f, 6.4103f};
constexpr float kWaveAmplitude[8] = {0.308f, 0.209f, 0.154f, 0.1166f, 0.0858f, 0.0616f, 0.0462f, 0.033f};
constexpr float kWavePhase[8] = {0.f, 1.3f, 2.9f, 4.1f, 0.7f, 5.3f, 2.2f, 3.7f};
constexpr float kWaveSteepness = 0.5f;

void setColor(float out[3], Vec3 c) {
    out[0] = c.x;
    out[1] = c.y;
    out[2] = c.z;
}

rt::Material metal(Vec3 color, float roughness) {
    rt::Material m;
    setColor(m.baseColor, color);
    m.metallic = 1.f;
    m.roughness = roughness;
    return m;
}

rt::Material emitter(Vec3 color, float intensity) {
    rt::Material m;
    setColor(m.baseColor, {0.f, 0.f, 0.f});
    setColor(m.emission, color);
    m.emissionScale = intensity;
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    return m;
}

Vec3 emitted(const rt::Material& m) {
    return Vec3{m.emission[0], m.emission[1], m.emission[2]} * m.emissionScale;
}

// The rotation that turns unit vector a onto unit vector b (not opposite).
rt::Mat34 rotationFromTo(Vec3 a, Vec3 b) {
    const Vec3 axis = cross(a, b);
    const float c = std::clamp(dot(a, b), -1.f, 1.f);
    if (length(axis) < 1e-6f)
        return identity();
    return rotate(axis, std::acos(c));
}

// --- Cameras
// ------------------------------------------------------------------------------------------------

struct Key {
    Vec3 eye, target;
};

Vec3 catmullRom(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.f * p1) + (p2 - p0) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 +
                   (3.f * p1 - p0 - 3.f * p2 + p3) * t3);
}

Key alongKeys(std::span<const Key> keys, float s) {
    const int n = static_cast<int>(keys.size());
    const float x = std::clamp(s, 0.f, 1.f) * static_cast<float>(n - 1);
    const int i = std::min(static_cast<int>(x), n - 2);
    const float t = x - static_cast<float>(i);
    const auto at = [&](int k) { return keys[static_cast<std::size_t>(std::clamp(k, 0, n - 1))]; };
    return {catmullRom(at(i - 1).eye, at(i).eye, at(i + 1).eye, at(i + 2).eye, t),
            catmullRom(at(i - 1).target, at(i).target, at(i + 1).target, at(i + 2).target, t)};
}

// A point at `distance` from the formation's centre, `degrees` round from the front (+z) towards +x, `height`
// above the floor.
Vec3 around(float degrees, float distance, float height) {
    const float a = degrees * (kPi / 180.f);
    return {kDotsCentre.x + distance * std::sin(a), height, kDotsCentre.z + distance * std::cos(a)};
}

Camera makeCamera(const Key& key, float fov, float aperture, std::uint32_t shot) {
    Camera c;
    c.eye = key.eye;
    c.target = key.target;
    c.up = {0.f, 1.f, 0.f};
    c.fovYDegrees = fov;
    c.aperture = aperture;
    c.shot = shot;
    c.focusDistance = std::max(dot(kDotsCentre - key.eye, cameraBasis(c).forward), 0.1f);
    return c;
}

Camera dotsCamera(const show::TimelineFrame& frame) {
    const auto duration = static_cast<float>(std::max(frame.chapterDuration, 1e-3));
    const float accent = std::clamp(static_cast<float>(frame.accentSeconds), 0.f, duration);
    const auto local = static_cast<float>(frame.localSeconds);
    // Low, so that the floor's mirror image of the formation shows under it.
    const Vec3 aim = kDotsCentre + Vec3{0.f, -0.3f, 0.f};
    if (local < accent) {
        const float s = smooth01(local / std::max(accent, 1e-3f));
        const Key keys[] = {{around(46.f, 3.9f, 1.25f), aim},
                            {around(30.f, 3.6f, 1.05f), aim},
                            {around(14.f, 3.45f, 0.92f), aim},
                            {around(3.f, 3.4f, 0.88f), aim}};
        return makeCamera(alongKeys(keys, s), 42.f, 0.02f, 0);
    }
    const float s = smooth01((local - accent) / std::max(duration - accent, 1e-3f));
    const Vec3 logo = kDotsCentre + Vec3{0.f, -0.12f, 0.f};
    const Key keys[] = {{around(9.f, 3.4f, 0.95f), logo},
                        {around(-7.f, 3.5f, 0.98f), logo},
                        {around(-24.f, 3.5f, 1.15f), aim},
                        {around(-40.f, 3.9f, 1.4f), aim}};
    return makeCamera(alongKeys(keys, s), 42.f, 0.02f, 1);
}

Camera oceanCamera(const show::TimelineFrame& frame) {
    const auto duration = static_cast<float>(std::max(frame.chapterDuration, 1e-3));
    const float accent = std::clamp(static_cast<float>(frame.accentSeconds), 0.f, duration);
    const auto local = std::clamp(static_cast<float>(frame.localSeconds), 0.f, duration);
    // Forward over the swell towards the moon, easing out; from the accent the camera cranes up and back
    // until the whole sea lies below it, the moon's path across the beads in view.
    const float travel = local / duration;
    const float forward = travel * (2.f - travel); // ease out
    const float rise = smooth01((local - accent) / std::max(0.85f * (duration - accent), 1e-3f));
    const Vec3 skim{1.6f * std::sin(0.23f * local), 1.45f, 30.f - 40.f * forward};
    const float yaw = 0.06f * std::sin(0.17f * local), pitch = -6.f * (kPi / 180.f);
    const Vec3 look{std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    const Vec3 high{0.f, 42.f, 66.f}, centre{0.f, 0.f, -8.f};
    Camera c;
    c.eye = skim + (high - skim) * rise;
    c.target = (skim + 10.f * look) + (centre - (skim + 10.f * look)) * rise;
    c.up = {0.f, 1.f, 0.f};
    c.fovYDegrees = 50.f;
    c.aperture = 0.f;
    c.focusDistance = length(c.target - c.eye);
    c.shot = 0;
    return c;
}

} // namespace

// --- Counts and shapes
// ----------------------------------------------------------------------------------------

std::uint32_t particleCount(std::uint32_t full, SceneDetail detail) {
    const double scale = std::clamp(static_cast<double>(detail.scale), 0.0, 1.0);
    return std::max(kMinParticles, static_cast<std::uint32_t>(std::llround(full * scale)));
}

std::uint32_t dotsLatticeSide(std::uint32_t count) {
    auto n = static_cast<std::uint32_t>(std::pow(static_cast<float>(count), 1.f / 3.f) + 0.5f);
    while (n * n * n > count)
        --n;
    while ((n + 1u) * (n + 1u) * (n + 1u) <= count)
        ++n;
    return n;
}

Vec3 dotsTarget(DotsShape shape, std::uint32_t id, std::uint32_t count) {
    switch (shape) {
    case DotsShape::Cube:
        return cubeTarget(id, count);
    case DotsShape::Sphere:
        return sphereTarget(id, count);
    case DotsShape::Knot:
        return knotTarget(id, count);
    default:
        return logoTarget(id, kDotsLogoCell);
    }
}

Vec3 dotsShapePoint(DotsShape shape, std::uint32_t id, std::uint32_t count, float t) {
    if (shape == DotsShape::Logo)
        return turn(logoTarget(id, kDotsLogoCell), 0.12f * std::sin(kTau * t / 3.7f),
                    0.10f * std::sin(kTau * t / 2.9f));
    return turn(dotsTarget(shape, id, count), 0.6f + 0.85f * t, 0.4f + 0.12f * t);
}

// --- Particles
// ------------------------------------------------------------------------------------------------

rt::SphereRecord dotsParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params) {
    const auto count = static_cast<std::uint32_t>(params[12]);
    const float turbulence = params[13], flash = params[14];
    DotsShape held = DotsShape::Cube, from = DotsShape::Cube, to = DotsShape::Cube;
    std::uint32_t morph = 0;
    float s = 0.f;
    bool morphing = false;
    for (std::uint32_t m = 0; m < 6; ++m) {
        const float first = params[2 * m], arrival = params[2 * m + 1];
        if (seconds >= arrival) {
            held = kOrder[m + 1];
            continue;
        }
        if (seconds > first) {
            morphing = true;
            from = kOrder[m];
            to = kOrder[m + 1];
            morph = m;
            s = (seconds - first) / (arrival - first);
        }
        break;
    }
    Vec3 p;
    float flight = 0.f, logo = 0.f;
    if (morphing) {
        const Vec3 a = dotsShapePoint(from, id, count, seconds), b = dotsShapePoint(to, id, count, seconds);
        const float delay =
            0.8f * std::clamp(0.5f - 0.6f * a.y, 0.f, 1.f) + 0.2f * hashUnit(id ^ 0x5BD1E995u);
        const float blend = smooth01((s - 0.55f * delay) / 0.45f);
        flight = std::sin(kPi * blend);
        Vec3 mid = (a + (b - a) * blend) * (1.f + 0.3f * flight);
        const float swirl = (morph % 2u == 0u ? 1.4f : -1.4f) * flight;
        const float c = std::cos(swirl), sn = std::sin(swirl);
        mid = {c * mid.x + sn * mid.z, mid.y, -sn * mid.x + c * mid.z};
        const float step = 0.34f * flight * flight * turbulence;
        p = mid;
        for (int i = 0; i < 3; ++i)
            p = p + step * curl(p, seconds + 0.37f * static_cast<float>(i));
        logo = to == DotsShape::Logo ? blend : from == DotsShape::Logo ? 1.f - blend : 0.f;
    } else {
        p = dotsShapePoint(held, id, count, seconds);
        logo = held == DotsShape::Logo ? 1.f : 0.f;
    }

    const float shock = seconds >= params[5] ? std::exp(-(seconds - params[5]) / 0.6f) : 0.f;
    const std::uint32_t h = logoHash(id ^ 0xA511E9B3u);
    rt::SphereRecord r{};
    setColor(r.center, p);
    r.radius = kDotsRadius * std::pow(kFullCount / static_cast<float>(count), 1.f / 3.f);
    if (logoUnit(h) < 0.04f + 0.1f * logo) {
        setColor(r.color, kGlow[(h >> 4) & 3u]);
        r.emission = 7.f * (1.f + 2.f * flash) * (1.f + flight) * (1.f + 0.6f * logo) * (1.f + 2.f * shock);
    } else {
        const Vec3 chrome = Vec3{0.92f, 0.93f, 0.96f} * (0.9f + 0.1f * logoUnit(logoHash(h)));
        setColor(r.color, chrome + (Vec3{0.45f, 0.75f, 1.f} - chrome) * flight);
        r.emission = 0.25f * flight * flight + 0.3f * shock * logo;
    }
    return r;
}

rt::SphereRecord oceanParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params) {
    const auto count = static_cast<std::uint32_t>(params[0]);
    const float sea = params[1], scale = params[2], flash = params[3];
    const float radial = sea * std::sqrt((static_cast<float>(id) + 0.5f) / static_cast<float>(count));
    const float angle = kTau * golden(id);
    const Vec2 base{radial * std::cos(angle), radial * std::sin(angle)};
    Vec3 p{base.x, 0.f, base.y};
    float total = 0.f;
    for (int k = 0; k < 8; ++k) {
        const float a = kWaveAmplitude[k] * scale;
        const float f = kWaveNumber[k] * (kWaveDirection[k].x * base.x + kWaveDirection[k].y * base.y) -
                        kWaveSpeed[k] * seconds + kWavePhase[k];
        const float c = std::cos(f);
        p.x += kWaveSteepness * a * kWaveDirection[k].x * c;
        p.z += kWaveSteepness * a * kWaveDirection[k].y * c;
        p.y += a * std::sin(f);
        total += a;
    }
    const float crest = p.y / std::max(total, 1e-6f);
    const float foam = logoUnit(logoHash(id ^ 0x2C1B3C6Du)) < 0.35f ? smooth01((crest - 0.55f) / 0.25f) : 0.f;
    const float depth = 0.45f + 0.55f * smooth01(0.5f + 0.5f * crest);
    const float edge = smooth01((1.f - radial / sea) / 0.12f);
    rt::SphereRecord r{};
    setColor(r.center, p);
    r.radius = 0.5f * sea * 1.7724539f / std::sqrt(static_cast<float>(count)) * edge;
    const Vec3 metal = Vec3{0.95f, 0.9f, 0.84f} * depth, foamColor{0.1f, 0.75f, 1.f};
    setColor(r.color, metal + (foamColor - metal) * foam);
    r.emission = foam * (0.3f + 2.5f * flash) * 2.f;
    return r;
}

rt::SphereRecord particlesParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params) {
    return params[15] == 1.f ? oceanParticle(id, seconds, params) : dotsParticle(id, seconds, params);
}

ParticleFrame dotsParticleFrame(const show::TimelineFrame& frame, std::uint32_t count) {
    ParticleFrame p;
    p.count = count;
    const auto steps = retro::dotsSchedule(frame);
    for (std::size_t m = 0; m < steps.size(); ++m) {
        p.params[2 * m] = static_cast<float>(static_cast<double>(steps[m].first) / retro::kDotsRetroRate);
        p.params[2 * m + 1] =
            static_cast<float>(static_cast<double>(steps[m].arrival) / retro::kDotsRetroRate);
    }
    p.params[12] = static_cast<float>(count);
    p.params[13] = 0.12f + 0.18f * std::clamp(frame.bass, 0.f, 1.f);
    p.params[14] = std::clamp(frame.kick, 0.f, 1.f);
    p.params[15] = 0.f;
    p.material = Particles::kDotsChrome;
    return p;
}

ParticleFrame oceanParticleFrame(const show::TimelineFrame& frame, std::uint32_t count) {
    ParticleFrame p;
    p.count = count;
    p.params[0] = static_cast<float>(count);
    p.params[1] = kOceanRadius;
    p.params[2] = 0.35f + 0.45f * std::clamp(frame.energy, 0.f, 1.f);
    p.params[3] = std::clamp(frame.kick, 0.f, 1.f);
    p.params[15] = 1.f;
    p.material = Particles::kOceanMetal;
    return p;
}

// --- Cameras and exposure
// ---------------------------------------------------------------------------------------

Camera particlesCamera(const show::TimelineFrame& frame) {
    return frame.effect == show::Effect::Ocean ? oceanCamera(frame) : dotsCamera(frame);
}

float particlesExposureEV(show::Effect effect) {
    return effect == show::Effect::Ocean ? 0.f : -0.3f;
}

// --- The scene
// ----------------------------------------------------------------------------------------------

Particles::Particles(SceneDetail detail)
    : dotsCount_(particleCount(kDotsParticles, detail)), oceanCount_(particleCount(kOceanParticles, detail)) {
    static_.meshes = {quad(1.f, 1.f), quad(1.f, 1.f)};
    std::vector<rt::Material>& m = static_.materials;
    m.resize(kMaterialCount);
    // A dark glossy floor: black lacquer that mirrors the formation.
    setColor(m[kFloor].baseColor, {0.012f, 0.012f, 0.014f});
    m[kFloor].roughness = 0.22f;
    m[kKey] = emitter({1.f, 0.93f, 0.84f}, 9.f);
    m[kFill] = emitter({0.78f, 0.86f, 1.f}, 1.2f);
    m[kRim] = emitter({0.5f, 0.68f, 1.f}, 22.f);
    // Chrome-white: a white lacquer whose clear coat mirrors the soft boxes and the glowing particles.
    m[kDotsChrome] = metal({0.95f, 0.95f, 0.97f}, 0.15f);
    m[kOceanMetal] = metal({0.9f, 0.92f, 0.95f}, 0.2f);
    // The dark water the beads float on, out to the horizon: a smooth black dielectric mirror.
    setColor(m[kWater].baseColor, {0.004f, 0.006f, 0.01f});
    m[kWater].roughness = 0.02f;
    m[kWater].ior = 1.33f;
    static_.capacity.instances = 8;
    static_.capacity.lights = kLightCount;
    static_.capacity.materials = kMaterialCount;
    static_.capacity.particles = std::max(dotsCount_, oceanCount_);
    static_.particleShader = kParticlesShader;

    // The floor: an 80 m square facing +y.
    dotsInstances_.push_back(
        {kFloorMesh, kFloor, compose(rotate({1.f, 0.f, 0.f}, -0.5f * kPi), scale({80.f, 80.f, 1.f}))});
    // The soft boxes and the rim light, each a quad aimed at the formation.
    struct Box {
        Vec3 position;
        float width, height;
        MaterialIndex material;
        Light light;
    };
    // High and wide of every camera key, so no camera sees them (only their reflections).
    const Box boxes[] = {{{-1.8f, 4.4f, 1.9f}, 2.6f, 1.8f, kKey, kKeyLight},
                         {{3.6f, 3.9f, 1.f}, 2.f, 1.4f, kFill, kFillLight},
                         {{0.3f, 4.2f, -3.4f}, 3.4f, 1.f, kRim, kRimLight}};
    for (const Box& box : boxes) {
        const Vec3 aim = normalize(kDotsCentre - box.position);
        const rt::Mat34 transform =
            compose(translate(box.position),
                    compose(rotationFromTo({0.f, 0.f, 1.f}, aim), scale({box.width, box.height, 1.f})));
        dotsInstances_.push_back({kLightMesh, static_cast<std::uint32_t>(box.material), transform,
                                  static_cast<std::uint32_t>(box.light)});
        dotsLights_.push_back(
            rectLightForQuad(transform, 1.f, 1.f, rt::LightKind::Emitter, emitted(m[box.material]), 1.f));
    }
}

SceneFrame Particles::frame(const show::TimelineFrame& frame) const {
    SceneFrame f;
    f.camera = particlesCamera(frame);
    f.exposureEV = particlesExposureEV(frame.effect);
    SceneInstance field;
    field.mesh = 0;
    field.kind = rt::GeometryKind::Spheres;
    field.dynamicSpheres = true;
    field.firstSphere = 0;
    if (frame.effect == show::Effect::Ocean) {
        f.particles = oceanParticleFrame(frame, oceanCount_);
        field.material = kOceanMetal;
        field.transform = identity();
        field.sphereCount = oceanCount_;
        f.instances.push_back(field);
        f.instances.push_back(
            {kFloorMesh, kWater,
             compose(translate({0.f, kOceanWaterLevel, 0.f}),
                     compose(rotate({1.f, 0.f, 0.f}, -0.5f * kPi), scale({4000.f, 4000.f, 1.f})))});
        EnvironmentDesc& env = f.environment;
        env = environmentDefaults(EnvironmentKind::NightSky);
        env.discDirection = normalize(Vec3{0.08f, 0.29f, -1.f}); // 16 degrees up, ahead of the skim
        env.haze = 0.25f;
        env.discAngularRadius = 0.25f * (kPi / 180.f);
        env.discRadiance = {9000.f, 9400.f, 10000.f};
        env.townDensity = 0.f;
        env.starBrightness = 1.4f;
        env.select = 1.f;
        return f;
    }
    f.instances = dotsInstances_;
    f.lights = dotsLights_;
    f.particles = dotsParticleFrame(frame, dotsCount_);
    field.material = kDotsChrome;
    field.transform = translate(kDotsCentre);
    field.sphereCount = dotsCount_;
    f.instances.push_back(field);
    f.environment = environmentDefaults(EnvironmentKind::Space);
    f.environment.starBrightness = 0.35f;
    f.environment.select = 0.f;
    return f;
}

// --- The epoch cards
// ----------------------------------------------------------------------------------------------

std::string Particles::nowCard(show::Effect effect) const {
    if (effect == show::Effect::Dots)
        return std::string("2026") + kCardDot + groupThousands(kDotsParticles) + " TRACED PARTICLES" + kCardDot +
               "MOTION BLUR" + kCardDot + "DEPTH OF FIELD";
    if (effect == show::Effect::Ocean)
        return std::string("2026") + kCardDot + groupThousands(kOceanParticles) + " TRACED PARTICLES" + kCardDot +
               groupThousands(std::size(kWaveAmplitude)) + " GERSTNER WAVES" + kCardDot + "ONE SHOT";
    return {};
}

} // namespace evo::modern
