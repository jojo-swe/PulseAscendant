#include "modern/scenes/lens.hpp"

#include "modern/caustics.hpp"
#include "modern/mesh_builder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>

namespace evo::modern {

namespace {

constexpr float kPi = 3.14159265f;
constexpr double kPiD = 3.14159265358979323846;
constexpr float kDegrees = kPi / 180.f;

void setColor(float out[3], Vec3 c) {
    out[0] = c.x;
    out[1] = c.y;
    out[2] = c.z;
}

// Smoother: zero first and second derivatives at both ends.
float smoother01(float x) {
    x = std::clamp(x, 0.f, 1.f);
    return std::min(x * x * x * (x * (6.f * x - 15.f) + 10.f), 1.f);
}

Vec3 lerp(Vec3 a, Vec3 b, float s) {
    return a + (b - a) * s;
}

Vec3 horizontal(Vec3 v) {
    return normalize(Vec3{v.x, 0.f, v.z});
}

// The rotation that turns unit vector a onto unit vector b (not opposite).
rt::Mat34 rotationFromTo(Vec3 a, Vec3 b) {
    const Vec3 axis = cross(a, b);
    const float c = std::clamp(dot(a, b), -1.f, 1.f);
    if (length(axis) < 1e-7f)
        return identity();
    return rotate(axis, std::acos(c));
}

// The mosaic: tesserae of `size`, moved by `offset` tesserae, each stone keeping `saturation` of its pixel's
// chroma (the 1993 plum and blue calmed to mauve marble and slate).
rt::Material mosaic(float size, rt::Pattern pattern, float roughness, Vec2 offset, float saturation) {
    rt::Material m;
    setColor(m.baseColor, {1.f, 1.f, 1.f});
    setColor(m.baseColor2, {offset.x, offset.y, saturation});
    m.roughness = roughness;
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Checker);
    m.pattern = static_cast<std::uint32_t>(pattern);
    m.patternScale = size;
    return m;
}

rt::Material glass(float ior, float abbe, Vec3 tint) {
    rt::Material m;
    setColor(m.baseColor, tint);
    m.roughness = 0.f;
    m.ior = ior;
    m.abbe = abbe;
    m.transmission = 1.f;
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Glass);
    return m;
}

// --- Meshes
// ---------------------------------------------------------------------------------------------------

// A profile point of a surface of revolution (radius, height); a crease ends one smooth strip and starts the
// next at the same point, so the normals break there.
struct ProfilePoint {
    float r, y;
    bool crease = false;
};

void addVertex(rt::Mesh& mesh, Vec3 p, Vec3 n, float u, float v) {
    rt::Vertex vertex{};
    vertex.position[0] = p.x;
    vertex.position[1] = p.y;
    vertex.position[2] = p.z;
    vertex.normal[0] = n.x;
    vertex.normal[1] = n.y;
    vertex.normal[2] = n.z;
    vertex.uv[0] = u;
    vertex.uv[1] = v;
    mesh.vertices.push_back(vertex);
}

// Revolves one strip of points with their (radial, y) normals about +y, counter-clockwise from outside when
// the strip runs bottom to top along the outside (as modern::lathe).
void revolveStrip(rt::Mesh& mesh, std::span<const Vec2> points, std::span<const Vec2> normals, int segments) {
    const auto columns = static_cast<std::uint32_t>(segments) + 1;
    const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(points.size() - 1);
        for (std::uint32_t j = 0; j < columns; ++j) {
            const float phi = 2.f * kPi * static_cast<float>(j % static_cast<std::uint32_t>(segments)) /
                              static_cast<float>(segments);
            const float c = j % static_cast<std::uint32_t>(segments) == 0 ? 1.f : std::cos(phi);
            const float s = j % static_cast<std::uint32_t>(segments) == 0 ? 0.f : std::sin(phi);
            addVertex(mesh, {points[i].x * c, points[i].y, -points[i].x * s},
                      {normals[i].x * c, normals[i].y, -normals[i].x * s},
                      static_cast<float>(j) / static_cast<float>(segments), 1.f - v);
        }
    }
    for (std::uint32_t i = 0; i + 1 < points.size(); ++i) {
        for (std::uint32_t j = 0; j < static_cast<std::uint32_t>(segments); ++j) {
            const std::uint32_t a = first + i * columns + j, b = a + 1, d = a + columns, c = d + 1;
            if (points[i + 1].x != 0.f) {
                mesh.indices.insert(mesh.indices.end(), {a, c, d});
            }
            if (points[i].x != 0.f) {
                mesh.indices.insert(mesh.indices.end(), {a, b, c});
            }
        }
    }
}

// A profile revolved with smooth normals within each strip (central differences; a point on the axis at a
// strip's end faces straight down or up) and creases between strips.
rt::Mesh revolveProfile(std::span<const ProfilePoint> profile, int segments) {
    rt::Mesh mesh;
    std::size_t begin = 0;
    while (begin + 1 < profile.size()) {
        std::size_t end = begin + 1;
        while (end + 1 < profile.size() && !profile[end].crease)
            ++end;
        std::vector<Vec2> points, normals;
        for (std::size_t i = begin; i <= end; ++i)
            points.push_back({profile[i].r, profile[i].y});
        for (std::size_t i = 0; i < points.size(); ++i) {
            const Vec2 prev = points[i == 0 ? 0 : i - 1];
            const Vec2 next = points[i + 1 == points.size() ? i : i + 1];
            const float tr = next.x - prev.x, ty = next.y - prev.y;
            const float len = std::hypot(tr, ty);
            Vec2 n{ty / len, -tr / len};
            if (points[i].x == 0.f && (i == 0 || i + 1 == points.size()))
                n = {0.f, n.y >= 0.f ? 1.f : -1.f};
            normals.push_back(n);
        }
        revolveStrip(mesh, points, normals, segments);
        begin = end;
    }
    return mesh;
}

// Points on a circle of the profile plane (centre (cr, cy), radius), from angle a0 to a1 in degrees
// (0 = +r, 90 = +y), `steps` intervals; the first point is left out when `skipFirst`.
void arc(std::vector<ProfilePoint>& out, float cr, float cy, float radius, float a0, float a1, int steps,
         bool skipFirst = true) {
    for (int i = skipFirst ? 1 : 0; i <= steps; ++i) {
        const float a = (a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(steps)) * kDegrees;
        float r = cr + radius * std::cos(a);
        if (std::abs(r) < 1e-6f)
            r = 0.f;
        out.push_back({std::max(r, 0.f), cy + radius * std::sin(a)});
    }
}

// LENS OF GOD's lens: two spherical caps of radius `curvature` meeting a short cylinder at the rim, with
// exact normals and creases at the rim. Its axis is +y (towards the sun when placed).
rt::Mesh godLensMesh() {
    const float rim = 0.5f * kGodLensDiameter;
    const auto curvature = static_cast<float>(godLensCurvature());
    const auto thickness = static_cast<float>(godLensThickness());
    const float sag = curvature - std::sqrt(curvature * curvature - rim * rim);
    const float edge = 0.5f * thickness - sag; // half the rim's height
    constexpr int kSteps = 40, kSegments = 192;
    rt::Mesh mesh;
    const auto cap = [&](float sign) { // sign -1: the bottom cap, from the pole out; +1: the top, rim in
        std::vector<Vec2> points, normals;
        const float centre = -sign * (curvature - 0.5f * thickness);
        for (int i = 0; i <= kSteps; ++i) {
            const float u = static_cast<float>(sign < 0.f ? i : kSteps - i) / static_cast<float>(kSteps);
            const float r = rim * u;
            const float y = centre + sign * std::sqrt(curvature * curvature - r * r);
            points.push_back({r, y});
            normals.push_back({r / curvature, (y - centre) / curvature});
        }
        revolveStrip(mesh, points, normals, kSegments);
    };
    cap(-1.f);
    const Vec2 band[] = {{rim, -edge}, {rim, edge}};
    const Vec2 out[] = {{1.f, 0.f}, {1.f, 0.f}};
    revolveStrip(mesh, band, out, kSegments);
    cap(1.f);
    return mesh;
}

// Its bezel: a gold band round the rim, a little proud of the glass on both faces, rounded.
rt::Mesh bezelMesh() {
    const float rim = 0.5f * kGodLensDiameter;
    const float inner = rim + 0.4f, outer = rim + 9.f,
                half = 0.5f * static_cast<float>(godLensThickness()) - 2.f;
    std::vector<ProfilePoint> p = {{inner, -half - 3.f}};
    arc(p, outer - 2.5f, -half - 0.5f, 2.5f, -90.f, 0.f, 4);
    p.back().crease = true;
    p.push_back({outer, -half + 3.f});
    arc(p, outer + 0.8f, -half + 4.f, 0.8f, 180.f, 90.f, 3, true);
    p.push_back({outer + 1.6f, -half + 4.8f});
    p.push_back({outer + 1.6f, half - 4.8f, true});
    p.push_back({outer + 0.8f, half - 4.8f});
    p.push_back({outer, half - 3.f, true});
    p.push_back({outer, half - 0.5f});
    arc(p, outer - 2.5f, half - 0.5f, 2.5f, 0.f, 90.f, 4);
    p.back().crease = true;
    p.push_back({inner, half + 3.f, true});
    p.push_back({inner, -half - 3.f});
    return revolveProfile(p, 192);
}

// --- Cameras
// ----------------------------------------------------------------------------------------------------

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

// fov is vertical; focus is the point the lens focuses on when aperture > 0.
Camera makeCamera(Vec3 eye, Vec3 target, Vec3 up, float fov, std::uint32_t shot, float aperture = 0.f,
                  Vec3 focus = {}) {
    Camera c;
    c.eye = eye;
    c.target = target;
    c.up = up;
    c.fovYDegrees = fov;
    c.shot = shot;
    c.aperture = aperture;
    c.focusDistance = aperture > 0.f ? std::max(dot(focus - eye, cameraBasis(c).forward), 0.05f)
                                     : std::max(length(target - eye), 0.05f);
    return c;
}

// The show's lenses (24, 35 and 50 mm, 36 mm equivalent) as vertical fields of view at 16:9.
constexpr float kLens24 = 45.2f, kLens35 = 31.6f, kLens50 = 23.1f;

float chapterLength(const show::TimelineFrame& frame) {
    return static_cast<float>(std::max(frame.chapterDuration, 1e-3));
}

float accentOf(const show::TimelineFrame& frame) {
    return std::clamp(static_cast<float>(frame.accentSeconds), 0.f, chapterLength(frame));
}

// The beat's length at the accent (the one ending on it).
double godBeat(const show::TimelineFrame& frame) {
    const show::BeatGridView grid = show::beatGrid(frame.track);
    const double start = frame.trackSeconds - frame.localSeconds;
    const int b = std::max(grid.beatAt(start + accentOf(frame) + 1e-6), 1);
    return grid.beatTime(b) - grid.beatTime(b - 1);
}

float settleOf(const show::TimelineFrame& frame) {
    if (frame.effect == show::Effect::LensOfGod)
        return static_cast<float>(godSettle(frame));
    return static_cast<float>(lensDownbeatBefore(frame, accentOf(frame)));
}

// The plan view's screen up and right on the mosaic at chapter time t: the rotozoom's turned texture axes
// (texel u along +x, v along +z; the screen's x along (cos a, sin a), its rows down along (-sin a, cos a)).
Vec3 planUp(double t) {
    const double a = rotozoomAngle(t);
    return {static_cast<float>(std::sin(a)), 0.f, static_cast<float>(-std::cos(a))};
}

// LENS before D: straight down over the rotozoom's centre, as high as makes the frame as many tesserae wide
// as 1993's 320 pixels show texels at that zoom.
Camera planCamera(double t) {
    const auto zoom = static_cast<float>(rotozoomZoom(t));
    const float width = 320.f * zoom * kTessera;
    const float height = width / (2.f * std::tan(0.5f * kLens35 * kDegrees) * 16.f / 9.f);
    const Vec3 centre = rotozoomCentre(t);
    return makeCamera(centre + Vec3{0.f, height, 0.f}, centre, planUp(t), kLens35, 0);
}

// LENS from D: when the camera frames the ball. It holds from D on the moment half a beat after the accent's
// beat (the spot then lies in the middle of the tessera it has just entered, the one the camera faces), and
// after the accent's beat eases into following the ball a bar behind it: constant acceleration over two bars,
// then the ball's own pace.
double lensFramingTime(const show::TimelineFrame& frame) {
    const double beat = lensAccentBeat(frame);
    const double held = beat + 0.5 * frame.beatSeconds;
    const double lag = 4.0 * frame.beatSeconds;
    const double u = frame.localSeconds - beat;
    if (u <= 0.0)
        return held;
    return held + (u < 2.0 * lag ? u * u / (4.0 * lag) : u - lag);
}

Camera lensChapterCamera(const show::TimelineFrame& frame) {
    const auto t = static_cast<float>(frame.localSeconds);
    const float settle = settleOf(frame);
    if (t < settle)
        return planCamera(t);
    // From D: low, three-quarters behind the ball on its near side, 50 mm: the burning spot on the stone in
    // the foreground, the ball beyond it lit from ahead, the horizon's reflections across the top third. Held
    // until the spot has crossed onto the tessera in the frame's focus on the accent, then following the
    // ball; the focus stays on the spot.
    show::TimelineFrame framing = frame;
    framing.localSeconds = lensFramingTime(frame);
    framing.trackSeconds = frame.trackSeconds + (framing.localSeconds - frame.localSeconds);
    const float s = smoother01((static_cast<float>(framing.localSeconds) - settle) /
                               std::max(chapterLength(frame) - settle, 1e-3f));
    const Vec3 ball = lensBallCentre(framing), spot = lensFocusPoint(framing);
    const Vec3 back = normalize(Vec3{-1.f, 0.f, 0.8f - 0.25f * s});
    const Vec3 eye = spot + back * (3.7f - 0.5f * s) + Vec3{0.f, 0.95f - 0.1f * s, 0.f};
    // The ball on the right third: aim left of it.
    const Vec3 right = normalize(cross(ball - eye, Vec3{0.f, 1.f, 0.f}));
    const Vec3 target = ball - right * 0.55f + Vec3{0.f, -0.38f, 0.f};
    return makeCamera(eye, target, {0.f, 1.f, 0.f}, kLens50, 1, 0.012f, lensFocusPoint(frame));
}

// LENS OF GOD's frame: the sun's horizontal heading and the side across it.
Vec3 godHeading() {
    return horizontal(godSunDirection());
}
Vec3 godSide() {
    return normalize(cross(godHeading(), Vec3{0.f, 1.f, 0.f}));
}
Camera godChapterCamera(const show::TimelineFrame& frame) {
    const auto t = static_cast<float>(frame.localSeconds);
    const float settle = settleOf(frame);
    // Down to the rim from four bars before D; back up to orbit from four bars after the accent's downbeat.
    const double bar = 4.0 * godBeat(frame);
    const auto descent = static_cast<float>(lensBarLine(frame, settle - 4.0 * bar));
    const auto rise =
        static_cast<float>(lensBarLine(frame, lensDownbeatBefore(frame, accentOf(frame)) + 4.0 * bar));
    const Vec3 h = godHeading(), p = godSide(), up{0.f, 1.f, 0.f};
    const Vec3 lens = godLensCentre(frame);
    const Vec3 orbitEnd = h * 1700.f + p * 520.f + Vec3{0.f, 1150.f, 0.f};
    if (t < descent) {
        // Orbit, the sun behind the camera: the mosaic to the curved horizon, the lens below, the logo
        // beyond.
        const float s = smoother01(t / std::max(descent, 1e-3f));
        const Vec3 eye = lerp(h * 2500.f + p * 760.f + Vec3{0.f, 1750.f, 0.f}, orbitEnd, s);
        const Vec3 target = lerp(-h * 700.f, -h * 250.f + Vec3{0.f, 40.f, 0.f}, s);
        return makeCamera(eye, target, up, kLens35, 0);
    }
    if (t < settle) {
        // Down to the lens's rim on the sun's side, looking through it at the magnified stone.
        const float s = smoother01((t - descent) / std::max(settle - descent, 1e-3f));
        const Vec3 axis = godSunDirection();
        const Key keys[] = {{h * 1100.f + p * 360.f + Vec3{0.f, 820.f, 0.f}, lens - axis * 60.f},
                            {lens + axis * 330.f + p * 120.f, lens - axis * 90.f},
                            {lens + axis * 170.f + p * 190.f + h * 10.f, lens - axis * 120.f + p * 30.f},
                            {lens + axis * 90.f + p * 175.f, lens - axis * 180.f + p * 50.f}};
        const Key k = alongKeys(keys, s);
        return makeCamera(k.eye, k.target, up, kLens35, 1);
    }
    // From D, above the foreground tessera from the side, 24 mm: the lens's shadow lies on the mosaic like a
    // pool, and inside it the focus walks in across the stone, stopping on the tessera at the accent (the
    // frame holds still until then). Then a slow push down onto the burning stone.
    const Vec3 heldEye = p * 215.f - h * 40.f + Vec3{0.f, 120.f, 0.f};
    const Vec3 heldTarget = -p * 20.f + h * 35.f + Vec3{0.f, 10.f, 0.f};
    const Vec3 closeEye = p * 120.f - h * 25.f + Vec3{0.f, 62.f, 0.f};
    const Vec3 closeTarget = -p * 5.f + h * 12.f + Vec3{0.f, 4.f, 0.f};
    const float accent = accentOf(frame);
    if (t < rise) {
        const float s = smoother01((t - accent) / std::max(rise - accent, 1e-3f));
        return makeCamera(lerp(heldEye, closeEye, s), lerp(heldTarget, closeTarget, s), up, kLens24, 2);
    }
    // Back up to orbit, the ember a point of light on the mosaic.
    const float s = smoother01((t - rise) / std::max(chapterLength(frame) - rise, 1e-3f));
    const Key keys[] = {{closeEye, closeTarget},
                        {-h * 300.f + p * 380.f + Vec3{0.f, 260.f, 0.f}, Vec3{0.f, 30.f, 0.f}},
                        {h * 150.f + p * 820.f + Vec3{0.f, 720.f, 0.f}, -p * 150.f}};
    const Key k = alongKeys(keys, s);
    return makeCamera(k.eye, k.target, up, kLens35, 3);
}

} // namespace

// --- Cards and counts
// -----------------------------------------------------------------------------------------

const char* lensEpochCard(show::Effect effect) {
    if (effect == show::Effect::Lens)
        return "2026 · THE PIXELS BECAME STONE · GLASS SPHERE · CAUSTICS";
    if (effect == show::Effect::LensOfGod)
        return "2026 · LENS OF GOD · 200 UNIT LENS · SUN FOCUS · PLANETARY SCALE";
    return "";
}

std::uint32_t lensPhotonCount(std::uint32_t full, SceneDetail detail) {
    const double wanted = static_cast<double>(full) * std::clamp(static_cast<double>(detail.scale), 0.0, 1.0);
    std::uint32_t n = kMinLensPhotons;
    while (n < full && 2.0 * n <= wanted)
        n *= 2;
    return std::min(n, std::max(full, kMinLensPhotons));
}

// --- Time
// ---------------------------------------------------------------------------------------------------------

double lensBarLine(const show::TimelineFrame& frame, double target) {
    const show::BeatGridView grid = show::beatGrid(frame.track);
    const double start = frame.trackSeconds - frame.localSeconds; // the chapter's start in track seconds
    const double end = start + frame.chapterDuration;
    const int near = grid.beatAt(start + target);
    double best = target, bestGap = 1e30;
    for (int b = std::max(near - 8, 0); b <= std::min(near + 8, grid.count() - 1); ++b) {
        const double time = grid.beatTime(b);
        if (grid.bar(b) != 0 || time <= start || time >= end)
            continue;
        if (std::abs(time - start - target) < bestGap) {
            bestGap = std::abs(time - start - target);
            best = time - start;
        }
    }
    return best;
}

double godSettle(const show::TimelineFrame& frame) {
    return lensDownbeatBefore(frame, accentOf(frame) - 0.75 * godBeat(frame));
}

double lensDownbeatBefore(const show::TimelineFrame& frame, double target) {
    const show::BeatGridView grid = show::beatGrid(frame.track);
    const double start = frame.trackSeconds - frame.localSeconds;
    for (int b = grid.beatAt(start + target + 1e-9); b >= 0; --b) {
        const double time = grid.beatTime(b);
        if (time < start - 1e-6)
            break;
        if (grid.bar(b) == 0)
            return time - start;
    }
    return 0.0;
}

double lensBeatsSinceD(const show::TimelineFrame& frame) {
    const show::BeatGridView grid = show::beatGrid(frame.track);
    const double start = frame.trackSeconds - frame.localSeconds;
    const double settle = start + lensDownbeatBefore(frame, frame.accentSeconds);
    const auto position = [&](double trackSeconds) {
        const int b = std::max(grid.beatAt(trackSeconds), 0);
        const double t0 = grid.beatTime(b), t1 = grid.beatTime(b + 1);
        const double length = t1 > t0 ? t1 - t0 : frame.beatSeconds;
        return b + (trackSeconds - t0) / std::max(length, 1e-3);
    };
    return position(frame.trackSeconds) - position(settle);
}

// --- LENS's optics and paths
// --------------------------------------------------------------------------------

double rotozoomAngle(double t) {
    return 0.95 * std::sin(0.33 * t) + 0.08 * t;
}

double rotozoomZoom(double t) {
    return 0.7 + 0.4 * std::sin(0.41 * t - 1.2);
}

Vec3 rotozoomCentre(double t) {
    return Vec3{static_cast<float>(64.0 + 23.0 * t), 0.f,
                static_cast<float>(64.0 + 40.0 * std::sin(0.29 * t))} *
           kTessera;
}

PlanLens planLens(double t) {
    // 1993's lens on its screen (continuous: 1993 rounds it to whole pixels), as an offset from the screen's
    // middle in texels across the rotozoom's turned axes.
    const double phase = t / 6.2 - std::floor(t / 6.2);
    const double tri = phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase;
    const double x = 48.0 + 224.0 * tri - 160.0;
    const double y = 159.0 - 119.0 * std::abs(std::sin(kPiD * t / 2.3)) - 100.0;
    const double a = rotozoomAngle(t), zoom = rotozoomZoom(t);
    const double du = x * zoom * std::cos(a) - y * 1.2 * zoom * std::sin(a);
    const double dv = x * zoom * std::sin(a) + y * 1.2 * zoom * std::cos(a);
    // The plan camera's height over the stone (planCamera), the lens's on the stone's scale.
    const double width = 320.0 * zoom * kTessera;
    const double camera = width / (2.0 * std::tan(0.5 * 31.6 * kPiD / 180.0) * 16.0 / 9.0);
    const double onStone = 48.0 * zoom * kTessera; // its radius, seen at the stone's depth
    // It hangs at half its focal length h = focus / 2 = (focus / rim) r: seen from the camera, r and the
    // offset shrink by (camera - h) / camera.
    const double hang = 0.5 * godLensFocusDistance() / (0.5 * kGodLensDiameter);
    const double radius = onStone / (1.0 + hang * onStone / camera);
    const double height = hang * radius, shrink = (camera - height) / camera;
    const Vec3 centre = rotozoomCentre(t) + Vec3{static_cast<float>(du * shrink), 0.f,
                                                 static_cast<float>(dv * shrink)} * kTessera +
                        Vec3{0.f, static_cast<float>(height), 0.f};
    return {centre, static_cast<float>(radius / (0.5 * kGodLensDiameter)), static_cast<float>(radius)};
}

double lensAccentBeat(const show::TimelineFrame& frame) {
    const show::BeatGridView grid = show::beatGrid(frame.track);
    const double start = frame.trackSeconds - frame.localSeconds;
    const double accent = start + frame.accentSeconds;
    const int b = std::max(grid.beatAt(accent), 0);
    const double before = grid.beatTime(b), after = grid.beatTime(std::min(b + 1, grid.count() - 1));
    return (accent - before <= after - accent ? before : after) - start;
}

Vec3 lensBallCentre(const show::TimelineFrame& frame) {
    const auto column = static_cast<float>(40.0 + lensBeatsSinceD(frame));
    return {column * kTessera, kLensBallRadius, (static_cast<float>(kLensRow) + 0.5f) * kTessera};
}

Vec3 lensSunDirection() {
    static const Vec3 direction = [] {
        // 45 degrees: the ball's light converges a little above the stone (its best focus lies about 0.55 m
        // from the centre, the stone 0.71 m away along the beam) and lands as a small spectral spot whose
        // centre trails the contact point by kLensTrail tesserae along the row, clear of the ball's overhang.
        const float up = kLensBallRadius / std::hypot(kLensBallRadius, kLensTrail * kTessera);
        const float across = std::sqrt(1.f - up * up);
        const Vec3 heading{1.f, 0.f, 0.f};
        return heading * across + Vec3{0.f, up, 0.f};
    }();
    return direction;
}

Vec3 lensFocusPoint(const show::TimelineFrame& frame) {
    const Vec3 centre = lensBallCentre(frame), sun = lensSunDirection();
    return centre - sun * (centre.y / sun.y);
}

// --- Thick lenses
// -------------------------------------------------------------------------------------------

double bestFocusDistance(double curvature, double thickness, double aperture, double ior) {
    // The axis is z, towards the light; rays come down (-z) at heights y. The entry face's centre of
    // curvature lies below its apex (+thickness / 2), the exit face's above its apex (-thickness / 2).
    const double c1 = 0.5 * thickness - curvature, c2 = -0.5 * thickness + curvature,
                 r2 = curvature * curvature;
    const auto refract = [](double& dx, double& dz, double nx, double nz, double eta) {
        const double cosi = -(dx * nx + dz * nz);
        const double k = 1.0 - eta * eta * (1.0 - cosi * cosi);
        if (k < 0.0)
            return false;
        const double s = eta * cosi - std::sqrt(k);
        dx = eta * dx + s * nx;
        dz = eta * dz + s * nz;
        return true;
    };
    double sumAB = 0.0, sumBB = 0.0;
    constexpr int kRays = 512;
    for (int i = 0; i < kRays; ++i) {
        const double y = aperture * (i + 0.5) / kRays, weight = y; // uniform over the aperture's area
        if (y >= curvature)
            continue;
        const double z = c1 + std::sqrt(r2 - y * y);
        double dx = 0.0, dz = -1.0;
        if (!refract(dx, dz, y / curvature, (z - c1) / curvature, 1.0 / ior))
            continue;
        // Out through the exit face: the far root, from inside its circle.
        const double ox = y, oz = z - c2;
        const double b = ox * dx + oz * dz, c = ox * ox + oz * oz - r2;
        const double t = -b + std::sqrt(std::max(b * b - c, 0.0));
        const double px = y + t * dx, pz = z + t * dz;
        if (!refract(dx, dz, -px / curvature, -(pz - c2) / curvature, ior) || dz >= 0.0)
            continue;
        // The ray meets the plane z = Z at x = A + B Z; the least squares over Z is closed form.
        const double slope = dx / dz;
        const double a = px - slope * pz;
        sumAB += weight * a * slope;
        sumBB += weight * slope * slope;
    }
    return sumBB > 0.0 ? sumAB / sumBB : 0.0; // -Z of the least RMS radius
}

double godLensCurvature() {
    return 2.0 * (kGodLensIor - 1.0) * kGodLensFocalLength; // the thin-lens design
}

double godLensThickness() {
    const double rc = godLensCurvature(), rim = 0.5 * kGodLensDiameter;
    return 2.0 * (rc - std::sqrt(rc * rc - rim * rim)) + 3.0; // both sags and a 3-unit edge
}

double godLensFocusDistance() {
    static const double focus =
        bestFocusDistance(godLensCurvature(), godLensThickness(), 0.5 * kGodLensDiameter, kGodLensIor);
    return focus;
}

Vec3 godSunDirection() {
    const float up = std::sin(42.f * kDegrees);
    return normalize(Vec3{0.3f, 0.f, 0.95f}) * std::sqrt(1.f - up * up) + Vec3{0.f, up, 0.f};
}

double godLensDefocus(const show::TimelineFrame& frame) {
    const float settle = settleOf(frame);
    const auto t = static_cast<float>(frame.localSeconds);
    if (t >= settle)
        return 0.0;
    const double s = smoother01(t / std::max(settle, 1e-3f));
    return -100.0 * std::pow(std::max(1.0 - s, 0.0), 1.3);
}

Vec3 godFocusPoint(const show::TimelineFrame& frame) {
    const float settle = settleOf(frame), accent = accentOf(frame);
    const auto t = static_cast<float>(frame.localSeconds);
    const float walk = t < settle ? 1.f : 1.f - smoother01((t - settle) / std::max(accent - settle, 1e-3f));
    return godHeading() * (kGodFocusWalk * walk); // in from the sun's side
}

Vec3 godLensCentre(const show::TimelineFrame& frame) {
    return godFocusPoint(frame) +
           godSunDirection() * static_cast<float>(godLensFocusDistance() + godLensDefocus(frame));
}

double godSpotDiameter(const show::TimelineFrame& frame) {
    const double focus = godLensFocusDistance();
    return kGodLensDiameter * std::abs(godLensDefocus(frame)) / focus +
           2.0 * focus * std::tan(0.25 * kPiD / 180.0) + 1.5;
}

// --- The sky
// ----------------------------------------------------------------------------------------------------------

namespace {

// The clouds (shaders/modern/lens_clouds.comp): LENS's deck far over the mosaic, 240 to 700 m up, round a
// clear tube along the sun's line through the ball; LENS OF GOD's deck under the lens, 40 to 165 units up,
// round the lens's cone of light, which glows in the haze.
constexpr std::array<std::uint32_t, 3> kLensCloudGrid = {256, 24, 256};
constexpr std::array<std::uint32_t, 3> kGodCloudGrid = {256, 48, 256};

GridMediumDesc cloudDeck(bool god, Vec3 lens, float lensRadius, Vec3 through, float clearing) {
    const float half = god ? 1500.f : 3000.f, floor = god ? 40.f : 240.f, height = god ? 280.f : 460.f;
    GridMediumDesc grid;
    grid.size = god ? kGodCloudGrid : kLensCloudGrid;
    grid.worldToMedium = mediumBox({-half, floor, -half}, {half, floor + height, half});
    grid.densityScale = 1.f;
    grid.albedo = {0.96f, 0.96f, 0.965f};
    grid.phaseG = 0.6f;
    grid.emissionScale = 1.f;
    grid.params = {god ? kGodConeGlow : 0.f,
                   god ? 0.46f : 0.42f,
                   god ? 0.06f : 0.008f,
                   god ? kGodConeHaze : 0.f,
                   lens.x,
                   lens.y,
                   lens.z,
                   lensRadius,
                   through.x,
                   through.y,
                   through.z,
                   clearing,
                   half,
                   floor,
                   height,
                   god ? 0.45f : 1.f};
    return grid;
}

} // namespace

// --- Cameras
// ----------------------------------------------------------------------------------------------------

Camera lensCamera(const show::TimelineFrame& frame) {
    return frame.effect == show::Effect::LensOfGod ? godChapterCamera(frame) : lensChapterCamera(frame);
}

float lensExposureEV(show::Effect effect) {
    return effect == show::Effect::LensOfGod ? -1.1f : -0.9f;
}

// --- The scene
// --------------------------------------------------------------------------------------------------

Lens::Lens(SceneDetail detail)
    : photons_(lensPhotonCount(kLensPhotons, detail)), godPhotons_(lensPhotonCount(kGodLensPhotons, detail)) {
    static_.meshes.resize(kMeshCount);
    static_.meshes[kQuadMesh] = quad(1.f, 1.f);
    static_.meshes[kGodLensMesh] = godLensMesh();
    static_.meshes[kBezelMesh] = bezelMesh();

    std::vector<rt::Material>& m = static_.materials;
    m.resize(kMaterialCount);
    m[kMosaic] = mosaic(kTessera, rt::Pattern::Tesserae, 0.045f, {0.f, 0.f}, kStoneSaturation);
    m[kMosaic].flags = rt::kMaterialCausticReceiver;
    m[kMosaic].receiver = 0;
    // The planet's mosaic, moved so that the focus (the world's origin) is the middle of kGodFocusTexel.
    m[kPlanet] =
        mosaic(kPlanetTessera, rt::Pattern::TesseraeSphere, 0.09f,
               {static_cast<float>(kGodFocusTexel[0]) + 0.5f, static_cast<float>(kGodFocusTexel[1]) + 0.5f},
               kStoneSaturation);
    m[kPlanet].flags = rt::kMaterialCausticReceiver;
    m[kPlanet].receiver = 0;
    m[kBallGlass] = glass(kLensBallIor, kLensBallAbbe, {0.985f, 0.99f, 1.f});
    m[kGodGlass] = glass(kGodLensIor, kGodLensAbbe, {0.99f, 0.995f, 1.f});
    setColor(m[kGold].baseColor, {1.f, 0.77f, 0.4f});
    m[kGold].metallic = 1.f;
    m[kGold].roughness = 0.28f;
    m[kGold].pattern = static_cast<std::uint32_t>(rt::Pattern::BrushedMetal);
    m[kGold].patternScale = 40.f;
    // The burning tessera: blackbody light the camera sees (it lights nothing: no noise), over stone that
    // still takes the caustic.
    setColor(m[kEmber].baseColor, {0.5f, 0.5f, 0.5f});
    setColor(m[kEmber].emission, {1.f, 0.5f, 0.16f});
    m[kEmber].emissionScale = 0.f;
    m[kEmber].roughness = 0.7f;
    m[kEmber].flags = rt::kMaterialCameraOnlyEmission | rt::kMaterialCausticReceiver;
    m[kEmber].receiver = 0;
    m[kEmber].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Pbr); // (a receiver must be)
    m[kPlanGold] = m[kGold];
    m[kPlanGold].patternScale = 0.6f;

    rt::SphereRecord ball{};
    ball.radius = kLensBallRadius;
    setColor(ball.color, {1.f, 1.f, 1.f});
    ball.material = kBallGlass;
    rt::SphereRecord planet = ball;
    planet.radius = kLensPlanetRadius;
    planet.material = kPlanet;
    static_.spheres = {ball, planet};
    static_.sphereRanges = {{kBallSphere, 1}, {kPlanetSphere, 1}};

    static_.capacity.instances = 16;
    static_.capacity.materials = kMaterialCount;
    static_.capacity.spheres = kSphereCount;
    static_.capacity.causticResolution = std::max(kLensReceiverResolution, kGodReceiverResolution);
    static_.mediumShader = "modern_lens_clouds.comp.spv";
    static_.capacity.mediumGrid = {kGodCloudGrid[0], kGodCloudGrid[1], kGodCloudGrid[2]};
}

SceneFrame Lens::frame(const show::TimelineFrame& frame) const {
    return frame.effect == show::Effect::LensOfGod ? godFrame(frame) : lensFrame(frame);
}

SceneFrame Lens::lensFrame(const show::TimelineFrame& frame) const {
    SceneFrame f;
    f.camera = lensCamera(frame);
    f.exposureEV = lensExposureEV(frame.effect);
    // The mosaic, to the horizon (its edge 3 km out).
    f.instances.push_back(
        {kQuadMesh, kMosaic, compose(rotate({1.f, 0.f, 0.f}, -0.5f * kPi), scale({6000.f, 6000.f, 1.f}))});
    const bool plan = frame.localSeconds < settleOf(frame);
    Vec3 spot;
    float receiverSide = kLensReceiverSide;
    if (plan) {
        // 1993's lens: the hanging lens in its bezel; its light lands where the sun's line through its middle
        // meets the stone, converging (the stone lies inside its focal length).
        const PlanLens lens = planLens(frame.localSeconds);
        const rt::Mat34 placed = compose(translate(lens.centre), scale({lens.scale, lens.scale, lens.scale}));
        f.instances.push_back({kGodLensMesh, kGodGlass, placed});
        f.instances.push_back({kBezelMesh, kPlanGold, placed});
        const Vec3 sun = lensSunDirection();
        spot = lens.centre - sun * (lens.centre.y / sun.y);
        receiverSide = 3.2f * lens.radius;
    } else {
        SceneInstance ball{};
        ball.material = kBallGlass;
        ball.kind = rt::GeometryKind::Spheres;
        ball.firstSphere = kBallSphere;
        ball.sphereCount = 1;
        ball.transform = translate(lensBallCentre(frame));
        f.instances.push_back(ball);
        spot = lensFocusPoint(frame);
    }

    EnvironmentDesc& env = f.environment;
    env = environmentDefaults(EnvironmentKind::Day);
    env.discDirection = lensSunDirection();
    env.select = 1.f;
    // Fair-weather cumulus over the horizon, mirrored in the polished stone; the sun shines on the ball
    // through a gap.
    f.medium = MediumFrame{std::nullopt, cloudDeck(false, spot + lensSunDirection() * 1000.f, 0.f, spot, 320.f)};

    CausticFrame c;
    c.receivers = {
        receiverForQuad(compose(translate({spot.x, 0.f, spot.z}), rotate({1.f, 0.f, 0.f}, -0.5f * kPi)),
                        receiverSide, receiverSide, kLensReceiverResolution)};
    c.photons = photons_;
    c.light = -1;
    c.casterInstanceFirst = 1;
    c.casterInstanceCount = 1;
    f.caustics = c;
    return f;
}

SceneFrame Lens::godFrame(const show::TimelineFrame& frame) const {
    const auto t = static_cast<float>(frame.localSeconds);
    const float accent = accentOf(frame);
    SceneFrame f;
    f.camera = lensCamera(frame);
    f.exposureEV = lensExposureEV(frame.effect);
    SceneInstance planet{};
    planet.material = kPlanet;
    planet.kind = rt::GeometryKind::Spheres;
    planet.firstSphere = kPlanetSphere;
    planet.sphereCount = 1;
    planet.transform = translate({0.f, -kLensPlanetRadius, 0.f});
    f.instances.push_back(planet);
    // The lens and its bezel, their axis on the sun.
    const rt::Mat34 lens =
        compose(translate(godLensCentre(frame)), rotationFromTo({0.f, 1.f, 0.f}, godSunDirection()));
    const auto lensIndex = static_cast<std::uint32_t>(f.instances.size());
    f.instances.push_back({kGodLensMesh, kGodGlass, lens});
    f.instances.push_back({kBezelMesh, kGold, lens});
    if (t >= accent) {
        // The foreground tessera ignites on the accent: white-hot within a sixth of a second, then cooling
        // through yellow to an ember's orange, breathing a little with the bass.
        const float since = t - accent;
        const float heat = std::min(1.f, since / 0.16f) * (0.1f + 0.9f * std::exp(-since / 1.4f));
        std::vector<rt::Material> materials = static_.materials;
        rt::Material& ember = materials[kEmber];
        const Vec3 white{1.f, 0.93f, 0.8f}, orange{1.f, 0.28f, 0.05f};
        const float whiteness = std::exp(-since / 0.7f);
        setColor(ember.emission, lerp(orange, white, whiteness));
        ember.emissionScale = 40.f * heat * (0.9f + 0.2f * std::clamp(frame.bass, 0.f, 1.f));
        f.materials = std::move(materials);
        const float side = 0.94f * kPlanetTessera; // inside the mortar
        f.instances.push_back(
            {kQuadMesh, kEmber,
             compose(translate({0.f, 0.03f, 0.f}),
                     compose(rotate({1.f, 0.f, 0.f}, -0.5f * kPi), scale({side, side, 1.f})))});
    }

    EnvironmentDesc& env = f.environment;
    env = environmentDefaults(EnvironmentKind::Day);
    env.discDirection = godSunDirection();
    env.discRadiance = {26000.f, 23000.f, 18500.f}; // the afternoon's warmer sun
    env.haze = 0.55f;
    env.ground = env.horizon; // beyond the planet's limb, seen from orbit, the sky goes on
    env.select = 1.f;
    // The planet's air: a haze layer a few hundred units deep that softens the far mosaic from orbit and in
    // which the lens's shadow hangs; the cumulus under the lens, round its cone of light.
    FogDesc air;
    air.density = 1.f / kGodHazeDepth;
    air.falloff = 1.f / 260.f;
    air.albedo = {0.93f, 0.95f, 0.98f};
    air.phaseG = 0.6f;
    const Vec3 lensAt = godLensCentre(frame);
    const Vec3 focal = lensAt - godSunDirection() * static_cast<float>(godLensFocusDistance());
    f.medium = MediumFrame{air, cloudDeck(true, lensAt, 0.5f * kGodLensDiameter, focal, 6.f)};

    // The photon map follows the spot: wide while the lens is out of focus, tight and fine at the focus.
    const auto spot = static_cast<float>(godSpotDiameter(frame));
    const float stretch = 1.f / godSunDirection().y; // the spot's length along the sun's heading
    const float side = 1.5f * spot * stretch + 18.f;
    const auto resolution = static_cast<std::uint32_t>(
        std::clamp(std::round(side / (spot / 90.f)), 128.f, static_cast<float>(kGodReceiverResolution)));
    const Vec3 focus = godFocusPoint(frame);
    CausticFrame c;
    c.receivers = {
        receiverForQuad(compose(translate({focus.x, 0.f, focus.z}), rotate({1.f, 0.f, 0.f}, -0.5f * kPi)),
                        side, side, resolution)};
    c.photons = godPhotons_;
    c.light = -1;
    c.casterInstanceFirst = lensIndex;
    c.casterInstanceCount = 1;
    f.caustics = c;
    return f;
}

// --- The epoch cards
// ----------------------------------------------------------------------------------------------

std::string Lens::nowCard(show::Effect effect) const {
    return lensEpochCard(effect);
}

} // namespace evo::modern
