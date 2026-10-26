#include "modern/scenes/particles.hpp"

#include "modern/mesh_builder.hpp"
#include "modern/scenes/logo_targets.hpp"
#include "modern/scenes/shots.hpp"
#include "retro/dots_schedule.hpp"
#include "show/era.hpp"
#include "show/music_curves.hpp"

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

constexpr std::uint32_t kLogoVoxels = kLogoPixels * kLogoDepth;
constexpr float kShapeHalfHeight[4] = {0.6f, 0.78f, 0.31f, 0.24f};
constexpr Vec3 kEmber{1.f, 0.36f, 0.1f}, kIvory{0.96f, 0.92f, 0.84f}, kIce{0.6f, 0.79f, 1.f};
constexpr float kFlight = 1.f / 7.f;
constexpr float kSparkShare = 0.0012f; // of the logo's particles: about 2,500 sparks

Vec3 mix(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}

Vec3 identityColor(std::uint32_t id, std::uint32_t count) {
    const float y = std::clamp(cubeTarget(id, count).y / (0.5f * kDotsCubeSide), -1.f, 1.f);
    return y < 0.f ? mix(kIvory, kEmber, -y) : mix(kIvory, kIce, y);
}

float bandKey(DotsShape shape, std::uint32_t id, std::uint32_t count) {
    if (shape == DotsShape::Logo)
        return static_cast<float>(logoPixel((id % kLogoVoxels) % kLogoPixels).row) / 6.f;
    const Vec3 target = shape == DotsShape::Cube     ? cubeTarget(id, count)
                        : shape == DotsShape::Sphere ? sphereTarget(id, count)
                                                     : knotTarget(id, count);
    const float key = std::clamp(
        0.5f - 0.5f * target.y / kShapeHalfHeight[static_cast<std::uint32_t>(shape)], 0.f, 1.f);
    return 0.97f * key + 0.03f * hashUnit(id ^ 0x5BD1E995u);
}

struct Flight {
    Vec3 p;
    float blend, flight;
};

Flight flightOf(std::uint32_t id, std::uint32_t count, std::uint32_t morph, DotsShape from, DotsShape to,
                float s, float seconds, float turbulence) {
    const Vec3 a = dotsShapePoint(from, id, count, seconds), b = dotsShapePoint(to, id, count, seconds);
    const float start = bandKey(to, id, count) * (1.f - kFlight);
    Flight f;
    f.blend = smooth01((s - start) / kFlight);
    f.flight = std::sin(kPi * f.blend);
    Vec3 mid = a + (b - a) * f.blend;
    mid = mid + (mid - Vec3{0.f, mid.y, 0.f}) * (0.15f * f.flight);
    const float swirl = (morph % 2u == 0u ? 0.7f : -0.7f) * f.flight;
    const float c = std::cos(swirl), sn = std::sin(swirl);
    mid = {c * mid.x + sn * mid.z, mid.y, -sn * mid.x + c * mid.z};
    const float step = 0.34f * f.flight * f.flight * turbulence;
    f.p = mid;
    for (int i = 0; i < 3; ++i)
        f.p = f.p + step * curl(f.p, seconds + 0.37f * static_cast<float>(i));
    return f;
}

struct MorphState {
    bool morphing = false;
    DotsShape held = DotsShape::Cube, from = DotsShape::Cube, to = DotsShape::Cube;
    std::uint32_t morph = 0;
    float s = 0.f;
};

MorphState morphAt(float seconds, const std::array<float, 16>& params) {
    MorphState st;
    for (std::uint32_t m = 0; m < 6; ++m) {
        const float first = params[2 * m], arrival = params[2 * m + 1];
        if (seconds >= arrival) {
            st.held = kOrder[m + 1];
            continue;
        }
        if (seconds > first) {
            st.morphing = true;
            st.from = kOrder[m];
            st.to = kOrder[m + 1];
            st.morph = m;
            st.s = (seconds - first) / (arrival - first);
        }
        break;
    }
    return st;
}

Flight dotsPlace(std::uint32_t id, std::uint32_t count, float seconds, const std::array<float, 16>& params,
                 float turbulence) {
    const MorphState st = morphAt(seconds, params);
    if (st.morphing)
        return flightOf(id, count, st.morph, st.from, st.to, st.s, seconds, turbulence);
    return {dotsShapePoint(st.held, id, count, seconds), 1.f, 0.f};
}

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

constexpr float kOceanFogDensity = 0.02f, kOceanFogFalloff = 0.12f; // per metre at sea level; per metre up
constexpr float kRingRadiance = 3.5f;                                // the landing ring's crest at first

// D, in chapter seconds (the chapter's transition marks).
float dotsDownbeat(const show::TimelineFrame& frame) {
    return static_cast<float>(show::transitionMarks(frame).downbeat + frame.localSeconds - frame.showSeconds);
}

// The four lenses as vertical fields of view at 16:9 (shots.hpp's, and 24 mm).
constexpr float kLens24 = 45.2f;

// Hermite interpolation between (t0, v0, slope s0) and (t1, v1, slope s1).
float hermite(float t, float t0, float v0, float s0, float t1, float v1, float s1) {
    const float h = t1 - t0, u = std::clamp((t - t0) / h, 0.f, 1.f);
    const float u2 = u * u, u3 = u2 * u;
    return (2.f * u3 - 3.f * u2 + 1.f) * v0 + (u3 - 2.f * u2 + u) * h * s0 + (-2.f * u3 + 3.f * u2) * v1 +
           (u3 - u2) * h * s1;
}

// The logo's front face, its centre (the face is kLogoFront in front of the logo's middle).
constexpr float kLogoFront = 3.f * kDotsLogoCell;
Vec3 logoCentre() {
    return kDotsCentre + Vec3{0.f, kDotsLogoLift, 0.f};
}

// 15 degrees below the logo's centre at `distance`, aimed 8.5 degrees up (not at the logo), so the logo rides
// the upper third and the floor, where the ring of light runs, fills the lower.
shots::Key lowAim(Vec3 centre, float azimuth, float distance) {
    shots::Key key = shots::orbit(centre, azimuth, -15.f, distance);
    const float across = distance * std::cos(15.f * (kPi / 180.f));
    key.target = key.eye + normalize(Vec3{centre.x - key.eye.x, 0.f, centre.z - key.eye.z}) * across +
                 Vec3{0.f, across * std::tan(8.5f * (kPi / 180.f)), 0.f};
    key.up = {0.f, 1.f, 0.f};
    return key;
}

Camera dotsCamera(const show::TimelineFrame& frame) {
    const auto local = static_cast<float>(frame.localSeconds);
    const DotsLanding landing = dotsLanding(frame);
    // D + 1 bar and D + 3 bars (the landing is D + 4 bars).
    const float orbitStart = landing.seconds - 3.f * landing.bar, craneStart = landing.seconds - landing.bar;
    if (local < orbitStart) {
        // 1993's camera (retro::dotsCamera: 6 m from its formation, 40 degrees high), scaled to the 2026
        // formation's size (its cube is 1.2 m, 1993's 1.8).
        Camera c;
        c.eye = kDotsCentre + Vec3{0.f, 0.f, 4.f};
        c.target = kDotsCentre;
        c.up = {0.f, 1.f, 0.f};
        c.fovYDegrees = 40.f;
        c.aperture = 0.015f;
        c.focusDistance = 4.f;
        c.shot = 0;
        return c;
    }
    if (local < craneStart) {
        // A slow 50 mm orbit about the knot, rising a little.
        const float u = smooth01((local - orbitStart) / (craneStart - orbitStart));
        const shots::Key key = shots::orbit(kDotsCentre, 4.f + 30.f * u, 6.f + 7.f * u, 4.7f - 0.3f * u);
        return shots::makeCamera(key, shots::kLens50, 1, 0.03f, length(key.target - key.eye));
    }
    // Shot 2: from beside the printing logo down to 15 degrees below its centre by the landing, then a slow
    // push in.
    const Vec3 centre = logoCentre();
    shots::Key key;
    if (local < landing.seconds) {
        const float u = smooth01((local - craneStart) / std::max(landing.seconds - craneStart, 1e-3f));
        const shots::Key from = shots::orbit(centre, 21.f, 3.f, 7.4f), to = lowAim(centre, 0.f, 6.4f);
        const shots::Key keys[] = {from, to};
        key = shots::alongKeys(keys, u);
    } else {
        const auto end = static_cast<float>(frame.chapterDuration);
        const float u = smooth01((local - landing.seconds) / std::max(end - landing.seconds, 1e-3f));
        key = lowAim(centre, -3.f * u, 6.4f - 0.55f * u);
    }
    key.up = {0.f, 1.f, 0.f};
    // Focus: the face as it lands; then the sparks lifting towards the camera (about 0.8 m a second from
    // half a second on) over the bar; then back to the face over the chapter's last bar.
    const float face = std::max(dot(centre + Vec3{0.f, 0.f, kLogoFront} - key.eye,
                                    normalize(key.target - key.eye)),
                                0.1f);
    const float end = static_cast<float>(frame.chapterDuration);
    const float age = local - landing.seconds;
    const float sparks = std::max(face - 0.8f * std::max(age - 0.25f, 0.f), 0.4f);
    float focus = face;
    if (age > 0.f) {
        const float out = smooth01(age / landing.bar);
        const float back = smooth01((local - (end - landing.bar)) / landing.bar);
        focus = face + (sparks - face) * out * (1.f - back);
    }
    return shots::makeCamera(key, shots::kLens35, 2, 0.04f, focus);
}

Camera oceanCamera(const show::TimelineFrame& frame) {
    const auto duration = static_cast<float>(std::max(frame.chapterDuration, 1e-3));
    const auto local = std::clamp(static_cast<float>(frame.localSeconds), 0.f, duration);
    const float d = std::clamp(dotsDownbeat(frame), 1.f, duration - 1.f);
    const float accent = std::clamp(static_cast<float>(frame.accentSeconds), d, duration);
    const float half = 0.5f * kLens24;
    // Forward over the swell towards the moon, 30 cm over the crests, easing to a stop on D; then up and
    // back until the whole sea lies below.
    const float forward = 1.f - (1.f - std::min(local / d, 1.f)) * (1.f - std::min(local / d, 1.f));
    const Vec3 skim{0.9f * std::sin(0.23f * local), 0.95f + 0.05f * std::sin(0.71f * local), 27.f - 30.f * forward};
    const float rise = smooth01((local - d) / std::max(duration - 1.f - d, 1e-3f));
    const Vec3 high{0.f, 15.f, 40.f};
    const Vec3 eye = skim + (high - skim) * rise;
    // The tilt: the frame's top edge 1 degree under the horizon until D, on it at the accent (the horizon's
    // first frame), then up to the final framing (the horizon on the upper third).
    float pitch;
    if (local <= d)
        pitch = -half - 1.f;
    else if (local <= accent)
        pitch = hermite(local, d, -half - 1.f, 0.f, accent, -half, 3.f);
    else
        pitch = hermite(local, accent, -half, 3.f, duration - 0.5f, -8.f, 0.f);
    const float yaw = 0.05f * std::sin(0.17f * local) * (1.f - rise);
    const float p = pitch * (kPi / 180.f);
    const Vec3 look{std::sin(yaw) * std::cos(p), std::sin(p), -std::cos(yaw) * std::cos(p)};
    Camera c;
    c.eye = eye;
    c.target = eye + look * 10.f;
    c.up = {0.f, 1.f, 0.f};
    c.fovYDegrees = kLens24;
    c.aperture = 0.f;
    c.focusDistance = 10.f;
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
                    0.10f * std::sin(kTau * t / 2.9f)) +
               Vec3{0.f, kDotsLogoLift, 0.f};
    const Vec3 p = turn(dotsTarget(shape, id, count), 0.6f + 0.85f * t, 0.4f + 0.12f * t);
    return {p.x, p.y, -p.z};
}

// --- Particles
// ------------------------------------------------------------------------------------------------

rt::SphereRecord dotsParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params) {
    const auto count = static_cast<std::uint32_t>(params[12]);
    const float turbulence = params[13], flash = params[14];
    const float radius = kDotsRadius * std::pow(kFullCount / static_cast<float>(count), 1.f / 3.f);
    const MorphState st = morphAt(seconds, params);
    const Flight f = dotsPlace(id, count, seconds, params, turbulence);
    Vec3 p = f.p;
    const float logo = st.morphing ? (st.to == DotsShape::Logo     ? f.blend
                                      : st.from == DotsShape::Logo ? 1.f - f.blend
                                                                   : 0.f)
                                   : (st.held == DotsShape::Logo ? 1.f : 0.f);
    float trail = 0.f;
    if (st.morphing && (st.to == DotsShape::Logo || st.from == DotsShape::Logo)) {
        const std::uint32_t k = id / kLogoVoxels, j = k % 3u;
        if (j != 0u) {
            const std::uint32_t leader = id - j * kLogoVoxels;
            const Flight now = dotsPlace(leader, count, seconds, params, turbulence);
            const Flight before = dotsPlace(leader, count, seconds - 1.f / 120.f, params, turbulence);
            const Vec3 velocity = now.p - before.p;
            const float speed = length(velocity);
            trail = f.flight * now.flight;
            if (speed > 1e-4f && trail > 0.f) {
                const Vec3 behind = now.p - velocity * (1.5f * static_cast<float>(j) * radius / speed);
                p = mix(p, behind, trail);
            }
        }
    }

    const float landing = params[5];
    const float age = seconds - landing;
    const float shock = age >= 0.f ? std::exp(-age / 0.6f) : 0.f;
    const std::uint32_t h = logoHash(id ^ 0xA511E9B3u);
    float spark = 0.f;
    if (st.held == DotsShape::Logo && !st.morphing && age > 0.f &&
        logoUnit(logoHash(h ^ 0x1B873593u)) < kSparkShare) {
        const float rise = smooth01(age / 0.5f) * age;
        const float u = logoUnit(logoHash(h + 7u));
        const Vec3 drift = Vec3{0.25f * (u - 0.5f), 0.18f + 0.12f * u, 0.55f + 0.5f * u} * rise;
        p = p + drift + 0.12f * smooth01(age / 1.5f) * curl(p * 0.7f, seconds);
        spark = smooth01(age / 0.3f) * (1.f - smooth01((age - 2.5f) / 1.5f));
    }

    const Vec3 identity = identityColor(id, count);
    const std::uint32_t layer = (id % kLogoVoxels) / kLogoPixels;
    const Vec3 logoColor = layer == 0u ? Vec3{0.95f, 0.95f, 0.97f} : kEmber;
    const Vec3 color = mix(identity, logoColor, logo);
    rt::SphereRecord r{};
    setColor(r.center, p);
    r.radius = radius;
    const float edge = f.flight * smooth01((f.blend - 0.45f) / 0.4f);
    if (spark > 0.f) {
        setColor(r.color, mix(kEmber, Vec3{1.f, 0.75f, 0.45f}, logoUnit(h)));
        r.emission = 14.f * spark;
        r.radius *= 3.5f * (1.f - 0.5f * smooth01((age - 2.f) / 2.f));
    } else if (logoUnit(h) < 0.04f) {
        setColor(r.color, mix(color, Vec3{1.f, 1.f, 1.f}, 0.25f));
        r.emission = 5.f * (1.f + 2.f * flash) * (1.f + f.flight) * (1.f + 2.f * shock * logo);
    } else {
        setColor(r.color, color);
        r.emission = 0.9f * edge + 0.6f * trail + (layer == 0u ? 0.35f : 0.6f) * shock * logo +
                     (layer == 0u ? 0.f : 0.04f) * logo;
    }
    return r;
}

rt::SphereRecord oceanParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params) {
    const auto count = static_cast<std::uint32_t>(params[0]);
    const float sea = params[1], scale = params[2], flash = params[3];
    const float radial = sea * std::sqrt((static_cast<float>(id) + 0.5f) / static_cast<float>(count));
    const float angle = kTau * golden(id);
    const float spacing = sea * 1.7724539f / std::sqrt(static_cast<float>(count));
    const std::uint32_t jitter = logoHash(id ^ 0x68E31DA4u);
    const Vec2 base{radial * std::cos(angle) + 0.4f * spacing * (logoUnit(jitter) - 0.5f),
                    radial * std::sin(angle) + 0.4f * spacing * (logoUnit(logoHash(jitter)) - 0.5f)};
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
    const float foam = logoUnit(logoHash(id ^ 0x2C1B3C6Du)) < 0.12f ? smooth01((crest - 0.65f) / 0.2f) : 0.f;
    const float depth = 0.45f + 0.55f * smooth01(0.5f + 0.5f * crest);
    const float edge = smooth01((1.f - radial / sea) / 0.12f);
    rt::SphereRecord r{};
    setColor(r.center, p);
    r.radius = 0.5f * spacing * edge * (0.72f + 0.28f * logoUnit(logoHash(jitter ^ 0x51u)));
    const Vec3 metal = Vec3{0.95f, 0.9f, 0.84f} * depth, foamColor{0.55f, 0.8f, 1.f};
    setColor(r.color, metal + (foamColor - metal) * (0.6f * foam));
    r.emission = foam * (0.1f + 2.f * flash) * 1.5f;
    return r;
}

rt::SphereRecord particlesParticle(std::uint32_t id, float seconds, const std::array<float, 16>& params) {
    return params[15] == 1.f ? oceanParticle(id, seconds, params) : dotsParticle(id, seconds, params);
}

DotsLanding dotsLanding(const show::TimelineFrame& frame) {
    // D and the bar from the chapter's transition marks, the landing 16 beats after D on the track's grid.
    const show::TransitionMarks marks = show::transitionMarks(frame);
    const double toLocal = frame.localSeconds - frame.showSeconds;
    const show::BeatGridView grid = show::beatGrid(frame.track);
    const double offset = show::trackStart(frame.track);
    const int d = grid.beatAt(marks.downbeat - offset + 1e-6);
    const double landing = d >= 0 && d + 16 < grid.count() ? grid.beatTime(d + 16) + offset
                                                           : marks.downbeat + 4.0 * marks.bar;
    return {static_cast<float>(landing + toLocal), static_cast<float>(marks.bar)};
}


std::array<std::array<float, 2>, 6> dotsSchedule2026(const show::TimelineFrame& frame) {
    std::array<std::array<float, 2>, 6> out{};
    const auto steps = retro::dotsSchedule(frame);
    for (std::size_t m = 0; m < 2; ++m)
        out[m] = {static_cast<float>(static_cast<double>(steps[m].first) / retro::kDotsRetroRate),
                  static_cast<float>(static_cast<double>(steps[m].arrival) / retro::kDotsRetroRate)};
    // The logo: one row a beat, the last landing on D + 4 bars.
    const DotsLanding landing = dotsLanding(frame);
    out[2] = {landing.seconds - 7.f * 0.25f * landing.bar, landing.seconds};
    for (std::size_t m = 3; m < 6; ++m)
        out[m] = {kDotsNever, kDotsNever};
    return out;
}

ParticleFrame dotsParticleFrame(const show::TimelineFrame& frame, std::uint32_t count) {
    ParticleFrame p;
    p.count = count;
    const auto steps = dotsSchedule2026(frame);
    for (std::size_t m = 0; m < steps.size(); ++m) {
        p.params[2 * m] = steps[m][0];
        p.params[2 * m + 1] = steps[m][1];
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
    return effect == show::Effect::Ocean ? 0.f : 0.4f; // DOTS' first shot; Particles::frame sets each shot's
}

// --- The scene
// ----------------------------------------------------------------------------------------------

Particles::Particles(SceneDetail detail)
    : dotsCount_(particleCount(kDotsParticles, detail)), oceanCount_(particleCount(kOceanParticles, detail)) {
    static_.meshes = {quad(1.f, 1.f), quad(1.f, 1.f)};
    std::vector<rt::Material>& m = static_.materials;
    m.resize(kMaterialCount);
    // A dark glossy floor: black lacquer that mirrors the formation, where the landing's ring of light runs
    // (Pattern LightRing: off until a frame sets its radius).
    setColor(m[kFloor].baseColor, {0.012f, 0.012f, 0.014f});
    m[kFloor].roughness = 0.22f;
    m[kFloor].pattern = static_cast<std::uint32_t>(rt::Pattern::LightRing);
    m[kFloor].patternScale = 0.f;
    m[kKey] = emitter({1.f, 0.93f, 0.84f}, 9.f);
    m[kFill] = emitter({0.78f, 0.86f, 1.f}, 1.2f);
    m[kRim] = emitter({0.5f, 0.68f, 1.f}, 22.f);
    // Chrome-white: a white lacquer whose clear coat mirrors the soft boxes and the glowing particles.
    m[kDotsChrome] = metal({0.95f, 0.95f, 0.97f}, 0.15f);
    // The sea's beads: liquid metal, each a little mirror riding the swell (Pattern SeaBead: the frame sets
    // the wave scale in patternScale).
    m[kOceanMetal] = metal({0.9f, 0.92f, 0.95f}, 0.12f);
    m[kOceanMetal].pattern = static_cast<std::uint32_t>(rt::Pattern::SeaBead);
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
    // High and wide of every camera key, so no camera sees them (only their reflections); the rim stands
    // high behind the logo and aims at it, so its extrusion reads from the low camera.
    const Box boxes[] = {{{-1.8f, 4.4f, 1.9f}, 2.6f, 1.8f, kKey, kKeyLight},
                         {{3.6f, 3.9f, 1.f}, 2.f, 1.4f, kFill, kFillLight},
                         {{0.3f, 7.f, -2.6f}, 4.4f, 1.2f, kRim, kRimLight}};
    for (const Box& box : boxes) {
        const Vec3 at = box.material == kRim ? kDotsCentre + Vec3{0.f, kDotsLogoLift, 0.f} : kDotsCentre;
        const Vec3 aim = normalize(at - box.position);
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
        f.materials = static_.materials;
        f.materials[kOceanMetal].patternScale = f.particles->params[2];
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
        env.discDirection = normalize(Vec3{0.08f, 0.176f, -1.f}); // 10 degrees up, ahead of the skim
        env.haze = 0.15f;
        env.discAngularRadius = 0.25f * (kPi / 180.f);
        env.discRadiance = {9000.f, 9400.f, 10000.f};
        env.townDensity = 0.f;
        env.starBrightness = 0.8f;
        env.select = 1.f;
        // A low sea fog: the disc's edge and the black water beyond it fade into it long before the
        // horizon, which is a soft band of moonlit haze.
        MediumFrame medium;
        FogDesc fog;
        fog.density = kOceanFogDensity;
        fog.baseHeight = 0.f;
        fog.falloff = kOceanFogFalloff;
        fog.albedo = {0.8f, 0.84f, 0.9f};
        fog.phaseG = 0.6f;
        medium.fog = fog;
        f.medium = medium;
        return f;
    }
    // Per shot: 1993's framing leaves the formation small on black, so it is exposed up; the logo's close
    // shot carries the shock and the sparks, so it is exposed down.
    constexpr float kShotEV[3] = {0.4f, 0.1f, 0.f};
    f.exposureEV = kShotEV[std::min<std::uint32_t>(f.camera.shot, 2)];
    f.instances = dotsInstances_;
    f.lights = dotsLights_;
    f.particles = dotsParticleFrame(frame, dotsCount_);
    // The landing's ring of light across the floor: it spreads from under the logo to 7 m over a bar,
    // dimming as it goes.
    const DotsLanding landing = dotsLanding(frame);
    const float age = static_cast<float>(frame.localSeconds) - landing.seconds;
    if (age >= 0.f && age < landing.bar) {
        const float a = age / landing.bar;
        f.materials = static_.materials;
        rt::Material& floor = f.materials[kFloor];
        floor.patternScale = 0.4f + 6.6f * (1.f - (1.f - a) * (1.f - a));
        setColor(floor.baseColor2, Vec3{1.f, 0.8f, 0.55f} * (kRingRadiance * (1.f - a) * (1.f - a) *
                                                             std::min(age / 0.05f, 1.f)));
    }
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
