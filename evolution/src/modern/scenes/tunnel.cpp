#include "modern/scenes/tunnel.hpp"

#include "modern/environment.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace evo::modern {

namespace {

constexpr double kTauD = 2 * std::numbers::pi;
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTauF = 6.28318530718f; // the shader's kTau, as a float
constexpr float kDegrees = kPi / 180.f;

// The 24 mm lens of the 36 mm format (73 degrees across a 16:9 frame), as a vertical field of view.
const float kLens24 = 2.f * std::atan(std::tan(36.5f * kDegrees) * 9.f / 16.f) / kDegrees;

// --- Small helpers -----------------------------------------------------------------------------------------

void setColor(float out[3], Vec3 c) {
    out[0] = c.x;
    out[1] = c.y;
    out[2] = c.z;
}

Vec3 colorOf(const float c[3]) {
    return {c[0], c[1], c[2]};
}

std::uint32_t hashU(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float unitOf(std::uint32_t x) {
    return float(x >> 8) * (1.f / 16777216.f);
}

float smooth(float x) {
    x = std::clamp(x, 0.f, 1.f);
    return x * x * (3.f - 2.f * x);
}

Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}

rt::Material plastic(Vec3 color, float roughness) {
    rt::Material m;
    setColor(m.baseColor, color);
    m.roughness = roughness;
    return m;
}

rt::Material metal(Vec3 color, float roughness) {
    rt::Material m = plastic(color, roughness);
    m.metallic = 1.f;
    return m;
}

Vec3 toVec(const std::array<double, 3>& v) {
    return {float(v[0]), float(v[1]), float(v[2])};
}

// The transform that stands a mesh built about the z axis on ring `r` (its z along the tangent, its y up),
// without mirroring.
rt::Mat34 onRing(const TunnelRing& r) {
    const Vec3 x = cross(r.up, r.tangent); // = -right: (x, y, z) stays right-handed
    return {{{x.x, r.up.x, r.tangent.x, r.centre.x},
             {x.y, r.up.y, r.tangent.y, r.centre.y},
             {x.z, r.up.z, r.tangent.z, r.centre.z}}};
}

// --- The ribs ----------------------------------------------------------------------------------------------

// A rib's cross-section in (axial a, radial r), a closed loop: a flanged steel ring whose inner face carries
// a channel the beads ride in (1993's radius is the channel's mouth: a bead of kTunnelBeadRadius sits half
// in it), with small chamfers on every edge the light can catch; the outer edge reaches into the ceramic
// tube.
constexpr float kRibHalf = 0.021f, kRibWeb = 0.013f, kRibLip = 1.006f, kRibGroove = 0.0165f,
                kRibGrooveDepth = 1.027f, kRibFlange = 1.105f, kRibOuter = 1.40f, kRibChamfer = 0.006f;

std::vector<Vec2> ribProfile() {
    const float h = kRibHalf, web = kRibWeb, c = kRibChamfer;
    return {
        {-web, kRibOuter},
        {-web, kRibFlange + 0.012f},
        {-h, kRibFlange},
        {-h, kRibLip + c},
        {-h + c, kRibLip},
        {-kRibGroove, kRibLip},
        {-kRibGroove, kRibGrooveDepth},
        {kRibGroove, kRibGrooveDepth},
        {kRibGroove, kRibLip},
        {h - c, kRibLip},
        {h, kRibLip + c},
        {h, kRibFlange},
        {web, kRibFlange + 0.012f},
        {web, kRibOuter},
    };
}

// Revolves a closed (a, r) loop about the z axis: each edge of the loop is its own band with its own normal
// (sharp machined edges), smooth around. `segments` steps round.
rt::Mesh revolveLoop(const std::vector<Vec2>& loop, int segments) {
    // The loop's orientation: outward normals are (dr, -da) for a counter-clockwise loop in (a, r).
    double area = 0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Vec2 p = loop[i], q = loop[(i + 1) % loop.size()];
        area += double(p.x) * q.y - double(q.x) * p.y;
    }
    const float sign = area > 0 ? 1.f : -1.f;
    rt::Mesh mesh;
    for (std::size_t e = 0; e < loop.size(); ++e) {
        const Vec2 p = loop[e], q = loop[(e + 1) % loop.size()];
        const float da = q.x - p.x, dr = q.y - p.y, len = std::hypot(da, dr);
        const float na = sign * dr / len, nr = -sign * da / len; // outward in (a, r)
        const auto first = std::uint32_t(mesh.vertices.size());
        for (int s = 0; s <= segments; ++s) {
            const float phi = kTauF * float(s) / float(segments);
            const float c = std::cos(phi), sn = std::sin(phi);
            for (const Vec2 v : {p, q}) {
                rt::Vertex vertex{};
                vertex.position[0] = v.y * c;
                vertex.position[1] = v.y * sn;
                vertex.position[2] = v.x;
                vertex.normal[0] = nr * c;
                vertex.normal[1] = nr * sn;
                vertex.normal[2] = na;
                vertex.uv[0] = float(s) / float(segments);
                vertex.uv[1] = float(e) / float(loop.size());
                mesh.vertices.push_back(vertex);
            }
        }
        for (int s = 0; s < segments; ++s) {
            const std::uint32_t a = first + 2u * std::uint32_t(s), b = a + 1u, c = a + 2u, d = a + 3u;
            // Wind each triangle counter-clockwise seen from its normal's side.
            auto emit = [&](std::uint32_t i0, std::uint32_t i1, std::uint32_t i2) {
                const auto pos = [&](std::uint32_t i) {
                    const rt::Vertex& v = mesh.vertices[i];
                    return Vec3{v.position[0], v.position[1], v.position[2]};
                };
                const Vec3 n = cross(pos(i1) - pos(i0), pos(i2) - pos(i0));
                const rt::Vertex& v0 = mesh.vertices[i0];
                const Vec3 want{v0.normal[0], v0.normal[1], v0.normal[2]};
                if (dot(n, want) >= 0.f)
                    mesh.indices.insert(mesh.indices.end(), {i0, i1, i2});
                else
                    mesh.indices.insert(mesh.indices.end(), {i0, i2, i1});
            };
            emit(a, b, d);
            emit(a, d, c);
        }
    }
    return mesh;
}

// --- The ceramic tube --------------------------------------------------------------------------------------

// 1993's solid tube, made of tiles: 16 sides, bands of kTunnelBandLength, its checker (A where band + side
// is even) laid as glazed panels with a seam round each, its corners turned with depth by 1993's twist
// (frozen: the tube itself does not spin; the beads run round their ribs instead).
constexpr float kPanelSeam = 0.007f;

Vec3 shellCorner(std::int64_t band, std::uint32_t side, float radius) {
    const double w = double(band) * kTunnelBandLength;
    const TunnelRing r = tunnelRing(w);
    const double a = kTauD * double(side) / kTunnelRetroSegments + kTunnelTwistPerDepth * w;
    return r.centre + (r.right * float(std::cos(a)) + r.up * float(std::sin(a))) * radius;
}

void addQuad(rt::Mesh& mesh, Vec3 p00, Vec3 p10, Vec3 p11, Vec3 p01, Vec3 inward) {
    const Vec3 n = normalize(cross(p10 - p00, p01 - p00));
    const bool flip = dot(n, inward) < 0.f;
    const Vec3 normal = flip ? -n : n;
    const auto first = std::uint32_t(mesh.vertices.size());
    const Vec3 corners[4] = {p00, p10, p11, p01};
    const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i) {
        rt::Vertex v{};
        setColor(v.position, corners[i]);
        setColor(v.normal, normal);
        v.uv[0] = uvs[i][0];
        v.uv[1] = uvs[i][1];
        mesh.vertices.push_back(v);
    }
    if (flip)
        mesh.indices.insert(mesh.indices.end(), {first, first + 2, first + 1, first, first + 3, first + 2});
    else
        mesh.indices.insert(mesh.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
}

// The panels of one colour (A: band + side even) and one glaze variant, along the whole built length.
rt::Mesh panelMesh(bool colourA, std::uint32_t variant) {
    rt::Mesh mesh;
    const auto from = std::int64_t(std::floor(kTunnelBuiltFrom / kTunnelBandLength));
    const auto to = std::int64_t(std::ceil(kTunnelBuiltTo / kTunnelBandLength));
    for (std::int64_t m = from; m < to; ++m)
        for (std::uint32_t k = 0; k < kTunnelRetroSegments; ++k) {
            const bool a = ((m + std::int64_t(k)) & 1) == 0;
            const std::uint32_t h = hashU(std::uint32_t(m) * 0x8da6b343u ^ k * 0xcb1ab31fu);
            if (a != colourA || h % Tunnel::kVariants != variant)
                continue;
            const std::uint32_t next = (k + 1) % kTunnelRetroSegments;
            const Vec3 c00 = shellCorner(m, k, kTunnelShellRadius),
                       c10 = shellCorner(m, next, kTunnelShellRadius);
            const Vec3 c01 = shellCorner(m + 1, k, kTunnelShellRadius),
                       c11 = shellCorner(m + 1, next, kTunnelShellRadius);
            const Vec3 middle = (c00 + c10 + c01 + c11) * 0.25f;
            // Each panel shrunk towards its middle by the seam, and sunk a hair into the wall by a per-panel
            // amount so neighbouring glazes catch the light at slightly different angles.
            const float sink = 0.004f * unitOf(hashU(h + 7u));
            const Vec3 axis = tunnelRing((double(m) + 0.5) * kTunnelBandLength).centre;
            const Vec3 out = normalize(middle - axis);
            auto inset = [&](Vec3 c) {
                const Vec3 d = c - middle;
                const float l = length(d);
                return middle + d * ((l - kPanelSeam) / l) + out * sink;
            };
            addQuad(mesh, inset(c00), inset(c10), inset(c11), inset(c01), axis - middle);
        }
    return mesh;
}

// The matte tube behind the panels' seams.
rt::Mesh backingMesh() {
    rt::Mesh mesh;
    const auto from = std::int64_t(std::floor(kTunnelBuiltFrom / kTunnelBandLength));
    const auto to = std::int64_t(std::ceil(kTunnelBuiltTo / kTunnelBandLength));
    const float r = kTunnelShellRadius + 0.02f;
    for (std::int64_t m = from; m < to; ++m) {
        const Vec3 axis = tunnelRing((double(m) + 0.5) * kTunnelBandLength).centre;
        for (std::uint32_t k = 0; k < kTunnelRetroSegments; ++k) {
            const std::uint32_t next = (k + 1) % kTunnelRetroSegments;
            const Vec3 c00 = shellCorner(m, k, r), c10 = shellCorner(m, next, r);
            const Vec3 c01 = shellCorner(m + 1, k, r), c11 = shellCorner(m + 1, next, r);
            addQuad(mesh, c00, c10, c11, c01, axis - (c00 + c11) * 0.5f);
        }
    }
    return mesh;
}

// --- The sky -----------------------------------------------------------------------------------------------

// Nothing outside: the tube is closed, and down its length the haze fades into black.
EnvironmentDesc tunnelSky() {
    EnvironmentDesc d = environmentDefaults(EnvironmentKind::Space);
    d.starDensity = 0.f;
    d.starBrightness = 0.f;
    d.zenith = {0, 0, 0};
    d.horizon = {0, 0, 0};
    d.ground = {0, 0, 0};
    d.glow = 0.f;
    d.select = 0.f;
    return d;
}

// The haze: a smoky one, denser where the path dips (falloff along +y), scattering forward so every bead
// wears a halo. It absorbs about a third of what it meets, more of the red than of the blue, so it keeps the
// blacks between the rings and the light it scatters leans violet: the far end, seen through the most haze,
// takes 1993's far colour by itself.
FogDesc tunnelFog() {
    FogDesc fog;
    fog.density = 0.09f;
    fog.baseHeight = 0.f;
    fog.falloff = 0.3f;
    fog.albedo = {0.66f, 0.63f, 0.73f};
    fog.phaseG = 0.6f;
    return fog;
}

// --- The moments -------------------------------------------------------------------------------------------

double chapterEnd(const show::TimelineFrame& f) {
    return std::max(f.chapterDuration, f.accentSeconds + 1e-3);
}

// Seconds since the latest beat at or before the frame, and the one before it (-1 without one), and the
// beat's length there.
struct BeatClock {
    double sinceLast, sincePrevious, beatSeconds;
};

BeatClock beatClock(const show::TimelineFrame& f) {
    const show::BeatGridView grid = show::beatGrid(f.track);
    const int beat = grid.beatAt(f.trackSeconds);
    if (beat < 0)
        return {-1.0, -1.0, 0.5};
    const double at = grid.beatTime(beat);
    const double next = beat + 1 < grid.count() ? grid.beatTime(beat + 1) : at + 0.5;
    const double previous = beat > 0 ? grid.beatTime(beat - 1) : -1e9;
    return {f.trackSeconds - at, beat > 0 ? f.trackSeconds - previous : -1.0, std::max(next - at, 1e-3)};
}

// The pulses that run down the rings after the accent: one leaves the camera on every beat.
constexpr double kPulseSpeed = 40.0; // units per second
constexpr double kPulseWidth = 1.1;  // units
constexpr double kPulseDecay = 0.55; // seconds

struct LightLevel {
    float scale, rose;
};

LightLevel lightLevel(const show::TimelineFrame& f, double ahead) {
    const TunnelMoments m = tunnelMoments(f);
    const double t = f.localSeconds;
    const float nearFade = smooth(float((ahead - kTunnelLitNear) / kTunnelNearFade));
    const float farFade = smooth(float((kTunnelLitFar - ahead) / kTunnelFarFade));
    float level = kTunnelPilot;
    float flash = 0.f;
    const double front = tunnelIgnitionFront(f);
    if (t >= m.downbeat) {
        const double passed = ahead - front; // > 0: the front has passed this ring on its way in
        const float lit = smooth(float(passed / 0.8 + 0.5));
        level = kTunnelPilot + (1.f - kTunnelPilot) * lit;
        // Each ring flares as the front passes it; the last flare (round the camera, on the accent) dies
        // out over the next beat.
        if (passed > -0.6 && t < m.accent + m.beatSeconds)
            flash = 2.4f * float(std::exp(-passed * passed / 1.2)) *
                    (t < m.accent ? 1.f : float(std::exp(-(t - m.accent) / 0.15)));
    }
    float pulse = 0.f;
    if (t >= m.accent) {
        const BeatClock clock = beatClock(f);
        const float amplitude = 0.8f + 1.4f * std::clamp(f.energy, 0.f, 1.f);
        for (const double since : {clock.sinceLast, clock.sincePrevious}) {
            if (since < 0.0 || f.localSeconds - since < m.accent - 1e-3)
                continue; // only beats from the accent on
            const double d = ahead - (kTunnelLitNear + 1.0 + kPulseSpeed * since);
            pulse += amplitude *
                     float(std::exp(-d * d / (kPulseWidth * kPulseWidth)) * std::exp(-since / kPulseDecay));
        }
    }
    const float fade = nearFade * farFade;
    return {(level + flash + pulse) * fade, std::clamp(0.45f * pulse + 0.2f * flash, 0.f, 0.7f)};
}

// --- Cameras -----------------------------------------------------------------------------------------------

constexpr float kLookAhead = 7.f; // how far ahead the camera's target stands

Camera tunnelChapterCamera(const show::TimelineFrame& f) {
    const TunnelMoments m = tunnelMoments(f);
    const double travel = tunnelTravel(f.localSeconds);
    Camera c = tunnelEchoCamera(travel);
    c.aperture = 0.012f;
    c.focusDistance = 3.2f;
    const float u = smooth(float((f.localSeconds - m.accent) / m.barSeconds));
    if (u <= 0.f)
        return c;
    // Free: off the axis towards the lower right wall (the near ribs there become the foreground), turned
    // so the far end sits on the left third, banked into the bends.
    const TunnelRing here = tunnelRing(travel);
    const TunnelRing ahead = tunnelRing(travel + kLookAhead);
    c.eye = here.centre + (here.right * 0.34f - here.up * 0.2f) * u;
    c.target = lerp(c.target, ahead.centre + ahead.right * 1.45f + ahead.up * 0.1f, u);
    // The path's turn ahead, across the view: its second difference over 4 units.
    const TunnelRing a = tunnelRing(travel + 2.0), b = tunnelRing(travel + 6.0);
    const Vec3 turn = (b.centre - a.centre) - (a.centre - here.centre);
    const float lateral = dot(turn, here.right);
    // Plus a slow roll through the rest of the chapter: the one move of the shot after it breaks free.
    const float drift =
        0.16f * smooth(float((f.localSeconds - m.accent) / std::max(chapterEnd(f) - m.accent, 1e-3)));
    const float bank = u * std::clamp(0.35f * lateral, -0.2f, 0.2f) + drift;
    const Vec3 forward = normalize(c.target - c.eye);
    const Vec3 side = normalize(cross(forward, here.up));
    const Vec3 up = cross(side, forward);
    c.up = up * std::cos(bank) - side * std::sin(bank);
    c.aperture = 0.012f + 0.02f * u;
    c.focusDistance = 3.2f - 0.6f * u;
    return c;
}

// The float mirror of the shader's helpers.
float phaseF(float x) {
    const float turns = x / kTauF + 0.5f;
    return kTauF * (turns - std::floor(turns) - 0.5f);
}

} // namespace

// --- 1993's path
// ---------------------------------------------------------------------------------------------

std::array<double, 2> tunnelPath1993(double w) {
    std::array<double, 2> p{0, 0};
    for (const TunnelWaveTerm& t : kTunnelWavesX)
        p[0] += t.amplitude * std::sin(t.frequency * w + t.phase);
    for (const TunnelWaveTerm& t : kTunnelWavesY)
        p[1] += t.amplitude * std::sin(t.frequency * w + t.phase);
    return p;
}

std::array<double, 2> tunnelSlope1993(double w) {
    std::array<double, 2> p{0, 0};
    for (const TunnelWaveTerm& t : kTunnelWavesX)
        p[0] += t.amplitude * t.frequency * std::cos(t.frequency * w + t.phase);
    for (const TunnelWaveTerm& t : kTunnelWavesY)
        p[1] += t.amplitude * t.frequency * std::cos(t.frequency * w + t.phase);
    return p;
}

double tunnelTwist1993(double seconds, double w) {
    return kTunnelTwistRate * seconds + kTunnelTwistPerDepth * w;
}

TunnelRing tunnelRing(double w) {
    const std::array<double, 2> p = tunnelPath1993(w), slope = tunnelSlope1993(w);
    const double tl = std::sqrt(slope[0] * slope[0] + slope[1] * slope[1] + 1.0);
    const std::array<double, 3> tangent{-slope[0] / tl, slope[1] / tl, 1.0 / tl};
    std::array<double, 3> up{-tangent[0] * tangent[1], 1.0 - tangent[1] * tangent[1],
                             -tangent[2] * tangent[1]};
    const double ul = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    for (double& v : up)
        v /= ul;
    const std::array<double, 3> right{tangent[1] * up[2] - tangent[2] * up[1],
                                      tangent[2] * up[0] - tangent[0] * up[2],
                                      tangent[0] * up[1] - tangent[1] * up[0]};
    return {Vec3{float(-p[0]), float(p[1]), float(w)}, toVec(tangent), toVec(right), toVec(up)};
}

double tunnelTravel(double seconds) {
    return kTunnelSpeed * seconds;
}

std::uint32_t tunnelDotsPerRing(SceneDetail detail) {
    const double dots = std::round(double(kTunnelRetroDots) * std::sqrt(double(detail.scale)));
    return std::uint32_t(std::clamp(dots, 4.0, double(kTunnelRetroDots)));
}

std::uint32_t tunnelLightCount(SceneDetail detail) {
    return kTunnelRetroRings * tunnelDotsPerRing(detail);
}

std::array<float, 16> tunnelLightParams(SceneDetail detail) {
    std::array<float, 16> p{};
    p[0] = float(tunnelLightCount(detail));
    p[1] = float(tunnelDotsPerRing(detail));
    p[2] = kTunnelBeadRadius;
    return p;
}

std::int64_t tunnelRingOfSlot(std::uint32_t slot, double travel) {
    // As the shader computes it, in float.
    const float t = float(travel);
    const int nearest = int(std::floor((t + float(kTunnelLitNear)) / float(kTunnelRingSpacing))) + 1;
    const int rings = int(kTunnelRetroRings);
    return nearest + (int(slot) - nearest + rings * 1024) % rings;
}

rt::SphereRecord tunnelLight(std::uint32_t id, float seconds, std::span<const float, 16> params) {
    const auto dots = std::uint32_t(params[1]);
    const float radius = params[2];
    const std::uint32_t slot = id / dots;
    const std::uint32_t dot = id - slot * dots;

    const float travel = 3.0f * seconds;
    const int nearest = int(std::floor((travel + -3.5f) / 0.25f)) + 1;
    const int ring = nearest + (int(slot) - nearest + 96 * 1024) % 96;
    const float w = float(ring) * 0.25f;

    const float px = 2.6f * std::sin(phaseF(0.13f * w)) + 1.0f * std::sin(phaseF(0.31f * w + 1.1f));
    const float py = 1.6f * std::sin(phaseF(0.17f * w + 0.6f)) + 0.6f * std::sin(phaseF(0.37f * w + 2.3f));
    const float sx =
        2.6f * 0.13f * std::cos(phaseF(0.13f * w)) + 1.0f * 0.31f * std::cos(phaseF(0.31f * w + 1.1f));
    const float sy =
        1.6f * 0.17f * std::cos(phaseF(0.17f * w + 0.6f)) + 0.6f * 0.37f * std::cos(phaseF(0.37f * w + 2.3f));
    const Vec3 centre{-px, py, w};
    const Vec3 tangent = normalize(Vec3{-sx, sy, 1.0f});
    const Vec3 up = normalize(Vec3{0.f, 1.f, 0.f} - tangent * tangent.y);
    const Vec3 right = cross(tangent, up);
    const float angle = phaseF(kTauF * float(dot) / float(dots) + 0.35f * seconds + 0.08f * w);
    const Vec3 position = centre + (right * std::cos(angle) + up * std::sin(angle)) * 1.0f;

    const std::uint32_t h =
        hashU(std::uint32_t(ring) * 0x9E3779B9u + (dot * (64u / dots)) * 0x85EBCA6Bu + 0x5bd1e995u);
    const float warmth = unitOf(h);
    const float rating = unitOf(hashU(h));

    rt::SphereRecord s{};
    setColor(s.center, position);
    s.radius = radius;
    // One in four of 1993's points (every fourth, so they wind down the tunnel as 16 helices) burns the
    // CRT's phosphor white-blue, the rest a warm tungsten white.
    const bool cool = (dot * (64u / dots)) % 4u == 2u;
    setColor(s.color, cool ? lerp(Vec3{0.56f, 0.73f, 1.0f}, Vec3{0.68f, 0.82f, 1.0f}, warmth)
                           : lerp(Vec3{1.0f, 0.64f, 0.36f}, Vec3{1.0f, 0.78f, 0.55f}, warmth));
    s.emission = 140.0f * (0.88f + 0.24f * rating);
    setColor(s.prevCenter, position);
    s.material = 0;
    return s;
}

// --- The music
// -------------------------------------------------------------------------------------------------

TunnelMoments tunnelMoments(const show::TimelineFrame& f) {
    const show::BeatGridView grid = show::beatGrid(f.track);
    const double start = f.trackSeconds - f.localSeconds;
    TunnelMoments m{f.accentSeconds, f.accentSeconds, 0.5, 2.0};
    int beat = grid.beatAt(start + f.accentSeconds);
    if (beat < 0 || grid.beatTime(beat) < start - 1e-6)
        return m;
    const double next = beat + 1 < grid.count() ? grid.beatTime(beat + 1) : grid.beatTime(beat) + 0.5;
    m.beatSeconds = std::max(next - grid.beatTime(beat), 1e-3);
    m.barSeconds = 4.0 * m.beatSeconds;
    while (beat > 0 && grid.bar(beat) != 0 && grid.beatTime(beat - 1) >= start - 1e-6)
        --beat;
    m.downbeat = std::min(grid.beatTime(beat) - start, f.accentSeconds);
    return m;
}

double tunnelIgnitionFront(const show::TimelineFrame& f) {
    const TunnelMoments m = tunnelMoments(f);
    const double t = f.localSeconds;
    constexpr double kFrom = kTunnelLitFar + 0.5, kDone = kTunnelLitNear - 1.0;
    if (t < m.downbeat)
        return kFrom;
    const double span = std::max(m.accent - m.downbeat, 1e-3);
    if (t < m.accent) {
        // From the far end towards the camera, gathering speed, reaching the camera on the accent.
        const double u = (t - m.downbeat) / span;
        return kFrom * (1.0 - u * u);
    }
    // On past the camera at the speed it arrived with, lighting the rings behind it.
    return std::max(kDone, -2.0 * kFrom / span * (t - m.accent));
}

float tunnelLightScale(const show::TimelineFrame& f, double ahead) {
    return lightLevel(f, ahead).scale;
}

Camera tunnelEchoCamera(double travel) {
    const TunnelRing r = tunnelRing(travel);
    Camera c{r.centre, r.centre + r.tangent * kLookAhead, r.up, kLens24};
    return c;
}

Camera tunnelCamera(const show::TimelineFrame& frame) {
    return tunnelChapterCamera(frame);
}

float tunnelExposureEV(const show::TimelineFrame&) {
    return -3.f;
}

// --- The scene
// -----------------------------------------------------------------------------------------------

Tunnel::Tunnel(SceneDetail detail) : detail_(detail) {
    static_.meshes.resize(kMeshCount);
    static_.meshes[kRibMesh] = revolveLoop(ribProfile(), 72);
    for (std::uint32_t v = 0; v < kVariants; ++v) {
        static_.meshes[kPanelMeshA + v] = panelMesh(true, v);
        static_.meshes[kPanelMeshB + v] = panelMesh(false, v);
    }
    static_.meshes[kBackingMesh] = backingMesh();

    std::vector<rt::Material>& m = static_.materials;
    m.resize(kMaterialCount);
    // Dark brushed steel: one finish polished and cool, one worn and warm, one between.
    constexpr Vec3 kSteel[kVariants] = {{0.5f, 0.5f, 0.52f}, {0.46f, 0.44f, 0.42f}, {0.56f, 0.57f, 0.6f}};
    constexpr float kSteelRoughness[kVariants] = {0.34f, 0.46f, 0.27f};
    for (std::uint32_t v = 0; v < kVariants; ++v) {
        m[kRib + v] = metal(kSteel[v], kSteelRoughness[v]);
        m[kRib + v].pattern = static_cast<std::uint32_t>(rt::Pattern::BrushedMetal);
        m[kRib + v].patternScale = 0.6f;
    }
    // The tube's glazed ceramic, in 1993's two tube colours taken almost to black: under a clear glaze the
    // beads reflect in it; three glazes from glassy to eggshell.
    constexpr Vec3 kGlazeA[kVariants] = {
        {0.045f, 0.075f, 0.17f}, {0.05f, 0.082f, 0.18f}, {0.036f, 0.06f, 0.145f}};
    constexpr Vec3 kGlazeB[kVariants] = {
        {0.07f, 0.045f, 0.1f}, {0.078f, 0.05f, 0.108f}, {0.06f, 0.038f, 0.088f}};
    constexpr float kGlazeRoughness[kVariants] = {0.02f, 0.06f, 0.14f};
    for (std::uint32_t v = 0; v < kVariants; ++v) {
        for (const std::uint32_t k : {kPanelA + v, kPanelB + v}) {
            m[k] = plastic(k < kPanelB ? kGlazeA[v] : kGlazeB[v], 0.5f);
            m[k].coat = 1.f;
            m[k].coatRoughness = kGlazeRoughness[v];
        }
    }
    m[kBacking] = plastic({0.012f, 0.012f, 0.013f}, 0.9f);
    m[kBead] = plastic({1.f, 1.f, 1.f}, 0.3f);

    static_.capacity.instances = 1 + 2 * kVariants + 160 + 1;
    static_.capacity.lights = 4;
    static_.capacity.particles = tunnelLightCount(detail);
    static_.capacity.treeLights = tunnelLightCount(detail);
    static_.particleShader = "modern_particles_tunnel.comp.spv";
}

SceneFrame Tunnel::frame(const show::TimelineFrame& frame) const {
    return tunnelFrame(frame);
}

SceneFrame Tunnel::tunnelFrame(const show::TimelineFrame& frame) const {
    SceneFrame f;
    f.camera = tunnelCamera(frame);
    f.exposureEV = tunnelExposureEV(frame);
    f.environment = tunnelSky();
    MediumFrame medium;
    medium.fog = tunnelFog();
    f.medium = medium;

    // The tube, whole.
    f.instances.push_back({kBackingMesh, kBacking, identity()});
    for (std::uint32_t v = 0; v < kVariants; ++v) {
        f.instances.push_back({kPanelMeshA + v, kPanelA + v, identity()});
        f.instances.push_back({kPanelMeshB + v, kPanelB + v, identity()});
    }
    // The ribs from a little behind the camera to well past the lit window, each on its ring.
    const double travel = tunnelTravel(frame.localSeconds);
    const auto first = std::int64_t(std::floor((travel - 6.0) / kTunnelRingSpacing));
    const auto last = std::int64_t(std::ceil((travel + kTunnelLitFar + 12.0) / kTunnelRingSpacing));
    for (std::int64_t j = first; j <= last; ++j) {
        const std::uint32_t finish = hashU(std::uint32_t(j) * 0x27d4eb2du + 17u) % kVariants;
        f.instances.push_back({kRibMesh, kRib + finish, onRing(tunnelRing(double(j) * kTunnelRingSpacing))});
    }

    // The beads: the particle field, every one a light of the tree.
    ParticleFrame p;
    p.count = tunnelLightCount(detail_);
    p.params = tunnelLightParams(detail_);
    p.material = kBead;
    f.particles = p;
    SceneInstance field{};
    field.mesh = 0;
    field.material = kBead;
    field.transform = identity();
    field.kind = rt::GeometryKind::Spheres;
    field.firstSphere = 0;
    field.sphereCount = p.count;
    field.dynamicSpheres = true;
    rt::RectLight tree{};
    tree.kind = static_cast<std::uint32_t>(rt::LightKind::Tree);
    tree.selectWeight = 1.f;
    tree.gate = rt::kNoLight;
    field.light = std::uint32_t(f.lights.size());
    f.lights.push_back(tree);
    f.instances.push_back(field);

    // Fewer beads at a lower detail: each carries the light of the 1993 points it stands for.
    const std::uint32_t dots = tunnelDotsPerRing(detail_);
    const float share = float(kTunnelRetroDots) / float(dots);
    const auto seconds = float(frame.localSeconds);
    const Vec3 rose{1.f, 0.42f, 0.55f};
    f.treeLights.reserve(p.count);
    for (std::uint32_t k = 0; k < p.count; ++k) {
        const rt::SphereRecord s = tunnelLight(k, seconds, p.params);
        // The ring's depth in front of the camera (every bead of a ring burns alike).
        const std::int64_t ring = tunnelRingOfSlot(k / dots, frame.localSeconds * kTunnelSpeed);
        const double ahead = double(ring) * kTunnelRingSpacing - travel;
        const LightLevel level = lightLevel(frame, ahead);
        const Vec3 colour = lerp(colorOf(s.color), rose, level.rose);
        f.treeLights.push_back({colorOf(s.center), s.radius, colour * (s.emission * share), level.scale, k});
    }
    return f;
}

} // namespace evo::modern
