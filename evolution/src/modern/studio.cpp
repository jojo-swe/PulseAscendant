#include "modern/studio.hpp"

#include "modern/mesh_builder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <stdexcept>

namespace evo::modern {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// --- Layout (metres) ---------------------------------------------------------------------------------------

// The room: x from -2 to 2, the back wall's face at z = -0.95, the front wall's at z = 2.9, floor 0, ceiling
// 2.7. Walls are 0.1 thick boxes, so the room is closed apart from the window.
constexpr float kRoomLeft = -2.0f, kRoomRight = 2.0f, kRoomBack = -0.95f, kRoomFront = 2.9f;
constexpr float kRoomHeight = 2.7f, kWall = 0.1f;
// The window opening in the back wall, left of the desk; the portal light fills it.
constexpr float kWindowLeft = -1.85f, kWindowRight = -0.65f, kWindowBottom = 1.0f, kWindowTop = 2.0f;

// The desk: 1.5 x 0.8, its top at 0.75, front edge on z = 0.
constexpr float kDeskTop = 0.75f, kDeskHalfWidth = 0.75f, kDeskDepth = 0.8f, kDeskThickness = 0.04f;

// The desktop case under the monitor and the monitor on it, both centred on x = 0.
constexpr float kCaseHalf[3] = {0.21f, 0.07f, 0.2f};
constexpr float kCaseZ = -0.52f;                                // centre
constexpr float kMonitorBottom = kDeskTop + 2 * 0.07f + 0.018f; // on a swivel foot on the case
constexpr float kMonitorHalf[3] = {0.2f, 0.18f, 0.11f};         // the front body
constexpr float kMonitorFrontZ = -0.29f;                        // the front face of the body
constexpr float kBezelDepth = 0.012f;                           // the bezel bars in front of the body
// The picture: 0.28 x 0.21, centred in the body's front, 1 cm behind the bezel's front.
constexpr float kScreenWidth = 0.28f, kScreenHeight = 0.21f;
constexpr float kScreenCentreY = kMonitorBottom + 0.18f + 0.008f;
constexpr float kScreenZ = kMonitorFrontZ + kBezelDepth - 0.01f;

// The keyboard: a key unit of 19 mm, a 101-key layout 22.5 units wide.
constexpr float kKeyUnit = 0.019f;
constexpr float kKeyboardTilt = 0.07f; // radians: the back higher
constexpr float kKeyboardZ = -0.135f;  // centre of the base
constexpr float kKeyboardX = -0.04f;

// The lamp on the desk's left: base, two arms and a shade aimed at the front left of the desk.
constexpr Vec3 kLampBase{-0.6f, kDeskTop, -0.5f};
constexpr Vec3 kLampElbow{-0.68f, kDeskTop + 0.4f, -0.56f};
constexpr Vec3 kLampHead{-0.44f, kDeskTop + 0.43f, -0.12f};
constexpr Vec3 kLampAim{-0.12f, kDeskTop, -0.16f}; // where the shade points
constexpr float kLampEmitterSize = 0.1f;

// The moon (see Studio::kMoonDirection): an 18 cm square 4 m out, about 2.6 degrees across.
// Indirect contributions above this luminance are clamped: the room's light is about 0.05, so the plan's
// generic 64 would let caustic fireflies (diffuse -> chrome or glass -> lamp or moon) through untouched.
constexpr float kClampIndirect = 2.f;

constexpr float kMoonDistance = 4.f, kMoonSize = 0.18f, kMoonIntensity = 300.f;
constexpr Vec3 kMoonColor{0.72f, 0.84f, 1.f};

// --- Small helpers -----------------------------------------------------------------------------------------

rt::Mat34 place(Vec3 position, const rt::Mat34& rotation, Vec3 size) {
    return compose(translate(position), compose(rotation, scale(size)));
}

rt::Mat34 place(Vec3 position, Vec3 size) {
    return compose(translate(position), scale(size));
}

// The rotation that turns unit vector a onto unit vector b.
rt::Mat34 rotationFromTo(Vec3 a, Vec3 b) {
    const Vec3 axis = cross(a, b);
    const float c = std::clamp(dot(a, b), -1.f, 1.f);
    if (length(axis) < 1e-6f) {
        if (c > 0)
            return identity();
        const Vec3 other = std::abs(a.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        return rotate(cross(a, other), kPi);
    }
    return rotate(axis, std::acos(c));
}

// A unit cylinder (radius 1, height 1, along y) stretched from a to b with the given radius.
rt::Mat34 between(Vec3 a, Vec3 b, float radius) {
    const Vec3 d = b - a;
    const float len = length(d);
    return compose(translate((a + b) * 0.5f),
                   compose(rotationFromTo({0, 1, 0}, d / len), scale({radius, len, radius})));
}

void setColor(float out[3], Vec3 c) {
    out[0] = c.x;
    out[1] = c.y;
    out[2] = c.z;
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

// The radiance of an Emitter light: its surface's emission times emissionScale (the path tracer checks).
Vec3 emitted(const rt::Material& m) {
    return Vec3{m.emission[0], m.emission[1], m.emission[2]} * m.emissionScale;
}

// A RetroScreen light's emission: the picture times its surface's emissionScale on every channel.
Vec3 screenEmission(const rt::Material& m) {
    return {m.emissionScale, m.emissionScale, m.emissionScale};
}

// --- Meshes and materials ----------------------------------------------------------------------------------

enum Mesh : std::uint32_t {
    kKeyCap = Studio::kKeyCapMesh,
    kUnitBox,
    kDeskTopMesh,
    kMonitorFront,
    kMonitorRear,
    kScreen,
    kBezelSide,
    kBezelTopBottom,
    kCase,
    kKeyboardBase,
    kSphere,
    kCylinder,
    kShade,
    kMug,
    kMugHandle,
    kEmitter,
    kFloppy,
    kMouse,
    kMoonMesh,
    kSeat,
    kBackrest,
    kMeshCount
};

enum Material : std::uint32_t {
    kWallPaint,
    kCeilingPaint,
    kFloorTiles,
    kDeskWood,
    kBrushedSteel,
    kBeige,
    kBeigeDark,
    kScreenGlass,
    kRecess,
    kKeyLight,
    kKeyDark,
    kDriveSlot,
    kPowerLed,
    kFloppyPlastic,
    kFloppyShutter,
    kFloppyLabel,
    kMugGlaze,
    kChrome,
    kGlass,
    kStandWood,
    kLampAluminium,
    kLampPaint,
    kLampEmitter,
    kWindowFrame,
    kMousePad,
    kSkirting,
    kMoonGlow,
    kChairFabric,
    kChairPlastic,
    kMaterialCount
};

std::vector<rt::Mesh> buildMeshes() {
    std::vector<rt::Mesh> meshes(kMeshCount);
    meshes[kKeyCap] = bevelBox({0.0083f, 0.0045f, 0.0083f}, 0.0022f, 2);
    meshes[kUnitBox] = box({0.5f, 0.5f, 0.5f});
    meshes[kDeskTopMesh] = bevelBox({kDeskHalfWidth, 0.5f * kDeskThickness, 0.5f * kDeskDepth}, 0.008f, 3);
    meshes[kMonitorFront] = bevelBox({kMonitorHalf[0], kMonitorHalf[1], kMonitorHalf[2]}, 0.03f, 3);
    meshes[kMonitorRear] = bevelBox({0.16f, 0.14f, 0.12f}, 0.05f, 3);
    meshes[kScreen] = quad(kScreenWidth, kScreenHeight);
    // The bezel: two upright bars beside the picture and two across above and below it.
    const float sideWidth = 0.5f * (2 * kMonitorHalf[0] - kScreenWidth) - 0.004f;
    const float topHeight = 0.5f * (2 * kMonitorHalf[1] - kScreenHeight) - 0.004f;
    meshes[kBezelSide] = bevelBox({0.5f * sideWidth, kMonitorHalf[1], 0.5f * kBezelDepth}, 0.005f, 2);
    meshes[kBezelTopBottom] = bevelBox({kMonitorHalf[0], 0.5f * topHeight, 0.5f * kBezelDepth}, 0.005f, 2);
    meshes[kCase] = bevelBox({kCaseHalf[0], kCaseHalf[1], kCaseHalf[2]}, 0.01f, 2);
    meshes[kKeyboardBase] = bevelBox({0.235f, 0.014f, 0.085f}, 0.01f, 2);
    meshes[kSphere] = uvSphere(1.f, 48, 96);
    meshes[kCylinder] = cylinder(1.f, 1.f, 48, true);
    // The lamp's shade: a cone open at the bottom, a rolled rim, a dome on top.
    const std::array<Vec2, 9> shade = {{{0.074f, 0.f},
                                        {0.077f, 0.004f},
                                        {0.072f, 0.012f},
                                        {0.058f, 0.05f},
                                        {0.04f, 0.09f},
                                        {0.03f, 0.11f},
                                        {0.024f, 0.125f},
                                        {0.012f, 0.134f},
                                        {0.f, 0.137f}}};
    meshes[kShade] = lathe(shade, 64);
    // The mug: the outer wall, the rim, the inner wall down to the floor of the cup.
    const std::array<Vec2, 11> mug = {{{0.f, 0.f},
                                       {0.034f, 0.f},
                                       {0.038f, 0.003f},
                                       {0.04f, 0.012f},
                                       {0.041f, 0.088f},
                                       {0.04f, 0.0935f},
                                       {0.0375f, 0.095f},
                                       {0.035f, 0.093f},
                                       {0.034f, 0.088f},
                                       {0.033f, 0.012f},
                                       {0.f, 0.01f}}};
    meshes[kMug] = lathe(mug, 64);
    // The handle: a torus, half of it inside the mug's wall.
    std::array<Vec2, 25> ring{};
    for (int i = 0; i < 25; ++i) {
        const float phi = -0.5f * kPi + 2.f * kPi * static_cast<float>(i) / 24.f;
        ring[i] = {0.019f + 0.0055f * std::cos(phi), 0.0055f * std::sin(phi)};
    }
    meshes[kMugHandle] = lathe(ring, 32);
    meshes[kEmitter] = quad(kLampEmitterSize, kLampEmitterSize);
    meshes[kFloppy] = bevelBox({0.045f, 0.0016f, 0.047f}, 0.0012f, 1);
    // The mouse: an egg cut flat underneath.
    const std::array<Vec2, 8> mouse = {{{0.f, 0.f},
                                        {0.028f, 0.f},
                                        {0.031f, 0.004f},
                                        {0.03f, 0.012f},
                                        {0.026f, 0.022f},
                                        {0.018f, 0.03f},
                                        {0.009f, 0.0335f},
                                        {0.f, 0.0345f}}};
    meshes[kMouse] = lathe(mouse, 48);
    meshes[kMoonMesh] = quad(kMoonSize, kMoonSize);
    meshes[kSeat] = bevelBox({0.23f, 0.035f, 0.22f}, 0.03f, 3);
    meshes[kBackrest] = bevelBox({0.21f, 0.22f, 0.03f}, 0.025f, 3);
    return meshes;
}

std::vector<rt::Material> buildMaterials(float screenEmission) {
    std::vector<rt::Material> m(kMaterialCount);
    m[kWallPaint] = plastic({0.7f, 0.66f, 0.6f}, 0.9f);
    m[kCeilingPaint] = plastic({0.6f, 0.58f, 0.55f}, 0.95f);
    // Black and off-white linoleum tiles, 0.3 m, slightly polished (a nod to RAYTRACE).
    m[kFloorTiles] = plastic({0.62f, 0.6f, 0.55f}, 0.32f);
    setColor(m[kFloorTiles].baseColor2, {0.022f, 0.022f, 0.024f});
    m[kFloorTiles].patternScale = 0.3f;
    m[kFloorTiles].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Checker);
    // Oiled oak planks under a satin coat.
    m[kDeskWood] = plastic({0.36f, 0.2f, 0.095f}, 0.6f);
    setColor(m[kDeskWood].baseColor2, {0.16f, 0.075f, 0.03f});
    m[kDeskWood].pattern = static_cast<std::uint32_t>(rt::Pattern::WoodPlanks);
    m[kDeskWood].patternScale = 0.16f;
    m[kDeskWood].coat = 0.6f;
    m[kDeskWood].coatRoughness = 0.15f;
    m[kBrushedSteel] = metal({0.62f, 0.62f, 0.64f}, 0.28f);
    m[kBrushedSteel].pattern = static_cast<std::uint32_t>(rt::Pattern::BrushedMetal);
    // Worn beige ABS: a little yellowed, satin.
    m[kBeige] = plastic({0.7f, 0.64f, 0.52f}, 0.5f);
    m[kBeigeDark] = plastic({0.56f, 0.51f, 0.42f}, 0.55f);
    // The picture: phosphor behind glass (Emissive with the retro picture; a smooth coat reflects the room).
    m[kScreenGlass] = plastic({0.035f, 0.038f, 0.036f}, 0.4f);
    m[kScreenGlass].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    m[kScreenGlass].texture = rt::kRetroScreenTexture;
    m[kScreenGlass].emissionScale = screenEmission;
    m[kScreenGlass].coat = 1.f;
    m[kScreenGlass].coatRoughness = 0.03f;
    m[kRecess] = plastic({0.06f, 0.06f, 0.06f}, 0.6f);
    // Key caps: soft, slightly rubbery; light letters, darker modifiers.
    m[kKeyLight] = plastic({0.74f, 0.71f, 0.63f}, 0.62f);
    m[kKeyDark] = plastic({0.42f, 0.41f, 0.39f}, 0.62f);
    m[kDriveSlot] = plastic({0.025f, 0.025f, 0.028f}, 0.45f);
    m[kPowerLed] = plastic({0.1f, 0.4f, 0.1f}, 0.2f);
    m[kPowerLed].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    setColor(m[kPowerLed].emission, {0.15f, 1.f, 0.25f});
    m[kPowerLed].emissionScale = 6.f;
    m[kFloppyPlastic] = plastic({0.035f, 0.04f, 0.07f}, 0.35f);
    m[kFloppyShutter] = metal({0.8f, 0.8f, 0.82f}, 0.22f);
    m[kFloppyLabel] = plastic({0.82f, 0.8f, 0.74f}, 0.9f);
    // A glazed mug: deep red stoneware under a glossy coat.
    m[kMugGlaze] = plastic({0.42f, 0.06f, 0.04f}, 0.4f);
    m[kMugGlaze].coat = 1.f;
    m[kMugGlaze].coatRoughness = 0.04f;
    m[kChrome] = metal({0.95f, 0.95f, 0.96f}, 0.02f);
    m[kGlass] = plastic({1.f, 1.f, 1.f}, 0.f);
    m[kGlass].transmission = 1.f;
    m[kGlass].ior = 1.5f;
    m[kGlass].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Glass);
    m[kStandWood] = plastic({0.05f, 0.03f, 0.02f}, 0.5f);
    m[kStandWood].coat = 0.8f;
    m[kStandWood].coatRoughness = 0.08f;
    m[kLampAluminium] = metal({0.86f, 0.86f, 0.88f}, 0.22f);
    m[kLampAluminium].pattern = static_cast<std::uint32_t>(rt::Pattern::BrushedMetal);
    m[kLampPaint] = plastic({0.03f, 0.045f, 0.035f}, 0.35f);
    m[kLampPaint].coat = 0.7f;
    m[kLampPaint].coatRoughness = 0.1f;
    // The lamp's bulb behind a diffuser: warm (1.0, 0.75, 0.5) x 35, also the Emitter light.
    m[kLampEmitter] = plastic({0.8f, 0.8f, 0.8f}, 0.9f);
    m[kLampEmitter].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    setColor(m[kLampEmitter].emission, Studio::kLampColor);
    m[kLampEmitter].emissionScale = Studio::kLampIntensity;
    m[kWindowFrame] = plastic({0.62f, 0.6f, 0.56f}, 0.55f);
    m[kMousePad] = plastic({0.03f, 0.035f, 0.06f}, 0.95f);
    m[kSkirting] = plastic({0.3f, 0.28f, 0.26f}, 0.6f);
    m[kChairFabric] = plastic({0.05f, 0.05f, 0.06f}, 0.95f);
    m[kChairPlastic] = plastic({0.025f, 0.025f, 0.027f}, 0.35f);
    m[kMoonGlow] = plastic({0.f, 0.f, 0.f}, 1.f);
    m[kMoonGlow].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    setColor(m[kMoonGlow].emission, kMoonColor);
    m[kMoonGlow].emissionScale = kMoonIntensity;
    return m;
}

// --- The keyboard ------------------------------------------------------------------------------------------

struct Key {
    float column, row; // the key's left edge in units, and its row (0 the function row, 5 the bottom)
    float width = 1, height = 1;
    bool dark = false; // a modifier or function key
};

// A US 101-key layout: the main block 15 units wide, the navigation block from 15.25, the keypad from 18.5.
std::vector<Key> keyboardLayout() {
    std::vector<Key> keys;
    const auto add = [&](float column, float row, float width = 1, bool dark = false, float height = 1) {
        keys.push_back({column, row, width, height, dark});
    };
    const auto run = [&](float column, float row, int count, bool dark = false) {
        for (int i = 0; i < count; ++i)
            add(column + static_cast<float>(i), row, 1, dark);
    };
    // Row 0: Esc, F1-F12 in threes of four, Print Screen, Scroll Lock, Pause.
    add(0, 0, 1, true);
    run(2, 0, 4, true);
    run(6.5f, 0, 4, true);
    run(11, 0, 4, true);
    run(15.25f, 0, 3, true);
    // Row 1: ` 1-0 - =, Backspace; Insert Home Page Up; Num Lock / * -.
    run(0, 1, 13);
    add(13, 1, 2, true);
    run(15.25f, 1, 3, true);
    run(18.5f, 1, 4, true);
    // Row 2: Tab, Q-P [ ], \; Delete End Page Down; 7 8 9, + two rows high.
    add(0, 2, 1.5f, true);
    run(1.5f, 2, 12);
    add(13.5f, 2, 1.5f);
    run(15.25f, 2, 3, true);
    run(18.5f, 2, 3);
    add(21.5f, 2, 1, true, 2);
    // Row 3: Caps Lock, A-', Enter; 4 5 6.
    add(0, 3, 1.75f, true);
    run(1.75f, 3, 11);
    add(12.75f, 3, 2.25f, true);
    run(18.5f, 3, 3);
    // Row 4: Shift, Z-/, Shift; Up; 1 2 3, Enter two rows high.
    add(0, 4, 2.25f, true);
    run(2.25f, 4, 10);
    add(12.25f, 4, 2.75f, true);
    add(16.25f, 4, 1, true);
    run(18.5f, 4, 3);
    add(21.5f, 4, 1, true, 2);
    // Row 5: Ctrl, Alt, the space bar, Alt, Ctrl; Left Down Right; 0 two wide and the point.
    add(0, 5, 1.5f, true);
    add(2.5f, 5, 1.5f, true);
    add(4, 5, 7);
    add(11, 5, 1.5f, true);
    add(13.5f, 5, 1.5f, true);
    run(15.25f, 5, 3, true);
    add(18.5f, 5, 2);
    add(20.5f, 5, 1);
    return keys;
}

// --- Cameras -----------------------------------------------------------------------------------------------

struct Key3 {
    Vec3 eye, target;
};

float smooth(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

Vec3 catmullRom(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.f * p1) + (p2 - p0) * t + (2.f * p0 - 5.f * p1 + 4.f * p2 - p3) * t2 +
                   (3.f * p1 - p0 - 3.f * p2 + p3) * t3);
}

// The keys at s in [0, 1] along a Catmull-Rom spline through them (the ends repeated).
Key3 alongKeys(std::span<const Key3> keys, float s) {
    const int n = static_cast<int>(keys.size());
    if (n == 1)
        return keys[0];
    const float x = std::clamp(s, 0.f, 1.f) * static_cast<float>(n - 1);
    const int i = std::min(static_cast<int>(x), n - 2);
    const float t = x - static_cast<float>(i);
    const auto at = [&](int k) { return keys[std::clamp(k, 0, n - 1)]; };
    return {catmullRom(at(i - 1).eye, at(i).eye, at(i + 1).eye, at(i + 2).eye, t),
            catmullRom(at(i - 1).target, at(i).target, at(i + 1).target, at(i + 2).target, t)};
}

Vec3 screenCentre() {
    return {0.f, kScreenCentreY, kScreenZ};
}

Camera makeCamera(const Key3& key, float fov, float aperture, std::uint32_t shot) {
    Camera c;
    c.eye = key.eye;
    c.target = key.target;
    c.up = {0, 1, 0};
    c.fovYDegrees = fov;
    c.aperture = aperture;
    c.focusDistance = length(key.target - key.eye);
    c.shot = shot;
    return c;
}

// A camera about the screen: in focus on the screen's plane wherever it aims (the thin lens focuses on the
// plane at focusDistance along the view, so the distance to the screen's centre along it), so the CRT's
// picture stays sharp; the aperture keeps a little depth of field in the room around it.
Camera screenCamera(const Key3& key, float fov, float aperture, std::uint32_t shot) {
    Camera c = makeCamera(key, fov, aperture, shot);
    c.focusDistance = std::max(dot(screenCentre() - key.eye, cameraBasis(c).forward), 0.05f);
    return c;
}

// The pull from the screen out to a wide view of the desk (FINALE, PORTAL), and RETURN's push back in.
constexpr std::array<Key3, 4> kPullOut = {{
    {{0.f, kScreenCentreY, kScreenZ + 0.3f}, {0.f, kScreenCentreY, kScreenZ}},
    {{0.08f, kScreenCentreY + 0.03f, 0.25f}, {0.f, kScreenCentreY - 0.01f, kScreenZ}},
    {{0.45f, 1.27f, 0.95f}, {-0.05f, 1.02f, kScreenZ - 0.05f}},
    {{0.9f, 1.42f, 1.55f}, {-0.18f, 0.98f, -0.45f}},
}};

} // namespace

std::array<Vec3, 4> studioScreenCorners() {
    const float l = -0.5f * kScreenWidth, r = 0.5f * kScreenWidth;
    const float t = kScreenCentreY + 0.5f * kScreenHeight, b = kScreenCentreY - 0.5f * kScreenHeight;
    return {Vec3{l, t, kScreenZ}, Vec3{r, t, kScreenZ}, Vec3{r, b, kScreenZ}, Vec3{l, b, kScreenZ}};
}

std::vector<StudioShot> studioShots(const show::TimelineFrame& frame) {
    const auto duration = static_cast<float>(frame.chapterDuration);
    switch (frame.effect) {
    case show::Effect::StudioNight: {
        const float cut = std::clamp(static_cast<float>(frame.accentSeconds), 0.f, duration);
        return {{0.f, cut}, {cut, duration}};
    }
    default:
        return {{0.f, duration}};
    }
}

Camera studioCamera(const show::TimelineFrame& frame) {
    const auto local = static_cast<float>(frame.localSeconds);
    const std::vector<StudioShot> shots = studioShots(frame);
    std::uint32_t shot = 0;
    while (shot + 1 < shots.size() && local >= shots[shot + 1].start)
        ++shot;
    const StudioShot& span = shots[shot];
    const float length = std::max(span.end - span.start, 1e-3f);
    const float s = smooth((local - span.start) / length);

    switch (frame.effect) {
    case show::Effect::Finale:
    case show::Effect::Portal:
        return screenCamera(alongKeys(kPullOut, s), 40.f, 0.004f, shot);
    case show::Effect::StudioNight:
        if (shot == 0) {
            // A wide dolly across the dark room, from the window's side towards the desk.
            static constexpr std::array<Key3, 3> kDolly = {{
                {{1.3f, 1.52f, 1.95f}, {-0.45f, 1.08f, -0.65f}},
                {{0.9f, 1.4f, 1.35f}, {-0.3f, 1.02f, -0.5f}},
                {{0.52f, 1.3f, 0.88f}, {-0.1f, 0.97f, -0.42f}},
            }};
            return makeCamera(alongKeys(kDolly, s), 42.f, 0.006f, shot);
        } else {
            // A close orbit around the monitor, from its right to its left, the screen always in view: aimed
            // at the monitor's body, in focus on the screen.
            const Vec3 centre{0.f, kScreenCentreY - 0.06f, kScreenZ - 0.12f};
            const float angle = (0.65f - 1.25f * s); // radians from +z, positive to the right
            const float radius = 0.82f - 0.12f * std::sin(s * kPi);
            const Vec3 eye{centre.x + radius * std::sin(angle), kScreenCentreY + 0.08f - 0.06f * s,
                           centre.z + radius * std::cos(angle)};
            return screenCamera({eye, centre}, 38.f, 0.004f, shot);
        }
    case show::Effect::Return: {
        static constexpr std::array<Key3, 3> kPushIn = {kPullOut[3], kPullOut[2], kPullOut[1]};
        return screenCamera(alongKeys(kPushIn, s), 40.f, 0.004f, shot);
    }
    default: {
        // REBOOT (and anything else): a close-up of the screen, drifting slightly.
        const float t = local;
        const Vec3 eye{0.025f * std::sin(0.21f * t), kScreenCentreY + 0.012f * std::sin(0.17f * t + 1.f),
                       kScreenZ + 0.44f - 0.04f * s};
        return screenCamera({eye, screenCentre()}, 42.f, 0.003f, shot);
    }
    }
}

float studioExposureEV(show::Effect effect) {
    switch (effect) {
    case show::Effect::StudioNight:
        return 3.f; // the darkest: the DOS prompt barely lights the room
    case show::Effect::Reboot:
        return 1.f; // a close-up of the lit monitor
    default:
        return 2.5f;
    }
}

Studio::Studio(SceneDetail) {
    static_.meshes = buildMeshes();
    static_.materials = buildMaterials(kScreenEmission);
    std::array<rt::RectLight, kLightCount> lights{};

    auto add = [&](std::uint32_t mesh, std::uint32_t material, const rt::Mat34& transform,
                   std::uint32_t light = rt::kNoLight) {
        instances_.push_back({mesh, material, transform, light});
    };
    auto block = [&](std::uint32_t material, Vec3 min, Vec3 max) {
        add(kUnitBox, material, place((min + max) * 0.5f, max - min));
    };

    // --- The room: floor, ceiling, four walls (the back one around the window), skirting.
    block(kFloorTiles, {kRoomLeft - kWall, -kWall, kRoomBack - kWall},
          {kRoomRight + kWall, 0.f, kRoomFront + kWall});
    block(kCeilingPaint, {kRoomLeft - kWall, kRoomHeight, kRoomBack - kWall},
          {kRoomRight + kWall, kRoomHeight + kWall, kRoomFront + kWall});
    block(kWallPaint, {kRoomLeft - kWall, 0.f, kRoomBack}, {kRoomLeft, kRoomHeight, kRoomFront});
    block(kWallPaint, {kRoomRight, 0.f, kRoomBack}, {kRoomRight + kWall, kRoomHeight, kRoomFront});
    block(kWallPaint, {kRoomLeft, 0.f, kRoomFront}, {kRoomRight, kRoomHeight, kRoomFront + kWall});
    const float back0 = kRoomBack - kWall, back1 = kRoomBack;
    block(kWallPaint, {kRoomLeft, 0.f, back0}, {kWindowLeft, kRoomHeight, back1});
    block(kWallPaint, {kWindowRight, 0.f, back0}, {kRoomRight, kRoomHeight, back1});
    block(kWallPaint, {kWindowLeft, 0.f, back0}, {kWindowRight, kWindowBottom, back1});
    block(kWallPaint, {kWindowLeft, kWindowTop, back0}, {kWindowRight, kRoomHeight, back1});
    const float skirt = 0.08f, skirtDepth = 0.012f;
    block(kSkirting, {kRoomLeft, 0.f, back1}, {kRoomRight, skirt, back1 + skirtDepth});
    block(kSkirting, {kRoomLeft, 0.f, back1}, {kRoomLeft + skirtDepth, skirt, kRoomFront});
    block(kSkirting, {kRoomRight - skirtDepth, 0.f, back1}, {kRoomRight, skirt, kRoomFront});

    // --- The window: a frame in the opening with a cross of glazing bars, a sill; the portal fills it.
    {
        const float f = 0.05f, z0 = back0 + 0.03f, z1 = back0 + 0.08f;
        block(kWindowFrame, {kWindowLeft, kWindowBottom, z0}, {kWindowLeft + f, kWindowTop, z1});
        block(kWindowFrame, {kWindowRight - f, kWindowBottom, z0}, {kWindowRight, kWindowTop, z1});
        block(kWindowFrame, {kWindowLeft, kWindowBottom, z0}, {kWindowRight, kWindowBottom + f, z1});
        block(kWindowFrame, {kWindowLeft, kWindowTop - f, z0}, {kWindowRight, kWindowTop, z1});
        const float mx = 0.5f * (kWindowLeft + kWindowRight),
                    my = kWindowBottom + 0.62f * (kWindowTop - kWindowBottom);
        const float bar = 0.018f;
        block(kWindowFrame, {mx - bar, kWindowBottom, z0 + 0.01f}, {mx + bar, kWindowTop, z1 - 0.01f});
        block(kWindowFrame, {kWindowLeft, my - bar, z0 + 0.01f}, {kWindowRight, my + bar, z1 - 0.01f});
        block(kWindowFrame, {kWindowLeft - 0.06f, kWindowBottom - 0.03f, back0 + 0.02f},
              {kWindowRight + 0.06f, kWindowBottom, back1 + 0.07f});
        const rt::Mat34 portal =
            translate({0.5f * (kWindowLeft + kWindowRight), 0.5f * (kWindowBottom + kWindowTop), back1});
        lights[kWindowLight] =
            rectLightForQuad(portal, kWindowRight - kWindowLeft, kWindowTop - kWindowBottom,
                             rt::LightKind::Portal, {}, kWindowWeight);
        // The moon: a cool disc of light seen through the window, a small square emitter 4 m out along the
        // sky's moon direction, facing the window. It throws the window's cross onto the desk and the floor.
        const Vec3 window{0.5f * (kWindowLeft + kWindowRight), 0.5f * (kWindowBottom + kWindowTop), back1};
        const rt::Mat34 moon = compose(translate(window + kMoonDirection * kMoonDistance),
                                       rotationFromTo({0, 0, 1}, -kMoonDirection));
        add(kMoonMesh, kMoonGlow, moon, kMoonLight);
        lights[kMoonLight] = rectLightForQuad(moon, kMoonSize, kMoonSize, rt::LightKind::Emitter,
                                              emitted(static_.materials[kMoonGlow]), kMoonWeight);
        // Seen only through the window: light selection gives it no weight where the window hides it.
        lights[kMoonLight].gate = kWindowLight;
    }

    // --- The desk: a coated wooden top on brushed steel legs with a rail at the back.
    add(kDeskTopMesh, kDeskWood, translate({0.f, kDeskTop - 0.5f * kDeskThickness, -0.5f * kDeskDepth}));
    {
        const float legTop = kDeskTop - kDeskThickness, leg = 0.022f;
        for (float x : {-kDeskHalfWidth + 0.06f, kDeskHalfWidth - 0.06f})
            for (float z : {-0.07f, -kDeskDepth + 0.07f})
                block(kBrushedSteel, {x - leg, 0.f, z - leg}, {x + leg, legTop, z + leg});
        block(kBrushedSteel, {-kDeskHalfWidth + 0.06f, legTop - 0.06f, -kDeskDepth + 0.07f - 0.012f},
              {kDeskHalfWidth - 0.06f, legTop - 0.02f, -kDeskDepth + 0.07f + 0.012f});
    }

    // --- The desktop case: two drive bays, a power switch and LED.
    const Vec3 caseCentre{0.f, kDeskTop + kCaseHalf[1], kCaseZ};
    add(kCase, kBeige, translate(caseCentre));
    {
        const float front = kCaseZ + kCaseHalf[2];
        block(kBeigeDark, {-0.2f, kDeskTop + 0.004f, front - 0.004f},
              {0.2f, kDeskTop + 0.02f, front + 0.002f});
        block(kDriveSlot, {0.02f, kDeskTop + 0.085f, front - 0.01f},
              {0.165f, kDeskTop + 0.118f, front + 0.002f});
        block(kBeigeDark, {0.03f, kDeskTop + 0.098f, front}, {0.155f, kDeskTop + 0.104f, front + 0.004f});
        block(kDriveSlot, {0.04f, kDeskTop + 0.04f, front - 0.01f},
              {0.145f, kDeskTop + 0.062f, front + 0.002f});
        block(kBeigeDark, {0.075f, kDeskTop + 0.049f, front}, {0.11f, kDeskTop + 0.053f, front + 0.003f});
        block(kBeigeDark, {-0.17f, kDeskTop + 0.05f, front}, {-0.13f, kDeskTop + 0.075f, front + 0.008f});
        block(kPowerLed, {-0.11f, kDeskTop + 0.07f, front}, {-0.102f, kDeskTop + 0.074f, front + 0.003f});
        block(kDriveSlot, {-0.12f, kDeskTop + 0.045f, front - 0.002f},
              {-0.06f, kDeskTop + 0.08f, front + 0.001f});
    }

    // --- The monitor: a swivel foot, the body and the tube's housing behind, the bezel, the picture.
    add(kCylinder, kBeigeDark,
        place({0.f, kMonitorBottom - 0.009f, kMonitorFrontZ - kMonitorHalf[2] - 0.04f},
              {0.11f, 0.018f, 0.11f}));
    const float bodyZ = kMonitorFrontZ - kMonitorHalf[2];
    const float bodyY = kMonitorBottom + kMonitorHalf[1];
    add(kMonitorFront, kBeige, translate({0.f, bodyY, bodyZ}));
    add(kMonitorRear, kBeige, translate({0.f, bodyY + 0.01f, bodyZ - kMonitorHalf[2] - 0.08f}));
    {
        const float bz = kMonitorFrontZ + 0.5f * kBezelDepth;
        const float sideWidth = 0.5f * (2 * kMonitorHalf[0] - kScreenWidth) - 0.004f;
        const float topHeight = 0.5f * (2 * kMonitorHalf[1] - kScreenHeight) - 0.004f;
        add(kBezelSide, kBeige, translate({-kMonitorHalf[0] + 0.5f * sideWidth, bodyY, bz}));
        add(kBezelSide, kBeige, translate({kMonitorHalf[0] - 0.5f * sideWidth, bodyY, bz}));
        add(kBezelTopBottom, kBeige, translate({0.f, bodyY + kMonitorHalf[1] - 0.5f * topHeight, bz}));
        add(kBezelTopBottom, kBeige, translate({0.f, bodyY - kMonitorHalf[1] + 0.5f * topHeight, bz}));
        // The dark recess between the picture and the bezel.
        const float l = -0.5f * kScreenWidth - 0.004f, r = 0.5f * kScreenWidth + 0.004f;
        const float b = kScreenCentreY - 0.5f * kScreenHeight - 0.004f,
                    t = kScreenCentreY + 0.5f * kScreenHeight + 0.004f;
        const float z0 = kMonitorFrontZ - 0.002f, z1 = kScreenZ - 0.001f;
        block(kRecess, {l - 0.004f, b, z0}, {l, t, z1});
        block(kRecess, {r, b, z0}, {r + 0.004f, t, z1});
        block(kRecess, {l, b - 0.004f, z0}, {r, b, z1});
        block(kRecess, {l, t, z0}, {r, t + 0.004f, z1});
        // The power LED under the picture, right.
        const float ledY = bodyY - kMonitorHalf[1] + 0.035f;
        block(kPowerLed, {0.15f, ledY, kMonitorFrontZ + kBezelDepth - 0.001f},
              {0.158f, ledY + 0.005f, kMonitorFrontZ + kBezelDepth + 0.002f});
        block(kBeigeDark, {0.12f, ledY - 0.006f, kMonitorFrontZ + kBezelDepth - 0.001f},
              {0.135f, ledY + 0.011f, kMonitorFrontZ + kBezelDepth + 0.004f});
        // Brightness and contrast knobs left of the switch, and a dark maker's badge at the bottom left.
        const float front = kMonitorFrontZ + kBezelDepth;
        for (const float x : {0.07f, 0.095f})
            add(kCylinder, kDriveSlot,
                compose(translate({x, ledY + 0.0025f, front + 0.003f}),
                        compose(rotate({1, 0, 0}, 0.5f * kPi), scale({0.0065f, 0.006f, 0.0065f}))));
        block(kDriveSlot, {-0.16f, ledY - 0.003f, front - 0.001f}, {-0.11f, ledY + 0.008f, front + 0.0015f});
        block(kFloppyShutter, {-0.155f, ledY + 0.0005f, front + 0.0015f},
              {-0.115f, ledY + 0.0045f, front + 0.002f});
    }
    const rt::Mat34 screen = translate(screenCentre());
    add(kScreen, kScreenGlass, screen, kScreenLight);
    lights[kScreenLight] = rectLightForQuad(screen, kScreenWidth, kScreenHeight, rt::LightKind::RetroScreen,
                                            screenEmission(static_.materials[kScreenGlass]), kScreenWeight);

    // --- The keyboard: a tilted base and 101 key caps instanced from one mesh.
    {
        const rt::Mat34 frame =
            compose(translate({kKeyboardX, kDeskTop + 0.016f, kKeyboardZ}), rotate({1, 0, 0}, kKeyboardTilt));
        add(kKeyboardBase, kBeige, frame);
        const float gridWidth = 22.5f * kKeyUnit, gridDepth = 6.5f * kKeyUnit;
        for (const Key& key : keyboardLayout()) {
            const float rowZ = key.row == 0 ? 0.f : (key.row + 0.5f);
            const float cx = (key.column + 0.5f * key.width) * kKeyUnit - 0.5f * gridWidth;
            const float cz = (rowZ + 0.5f * key.height) * kKeyUnit - 0.5f * gridDepth + 0.006f;
            const Vec3 size{(key.width * kKeyUnit - 0.0024f) / 0.0166f, 1.f,
                            (key.height * kKeyUnit - 0.0024f) / 0.0166f};
            add(kKeyCap, key.dark ? kKeyDark : kKeyLight,
                compose(frame, place({cx, 0.014f + 0.0045f, cz}, size)));
        }
    }

    // --- The mouse on its pad, the floppy disks, the mug.
    block(kMousePad, {0.27f, kDeskTop, -0.27f}, {0.5f, kDeskTop + 0.003f, -0.05f});
    add(kMouse, kBeige,
        place({0.38f, kDeskTop + 0.003f, -0.15f}, rotate({0, 1, 0}, 0.12f), {1.f, 1.f, 1.6f}));
    {
        const std::array<Vec3, 3> at = {{{0.53f, 0.f, -0.66f}, {0.55f, 0.f, -0.63f}, {0.6f, 0.f, -0.36f}}};
        const std::array<float, 3> turn = {0.25f, -0.1f, 0.5f};
        const std::array<float, 3> lift = {0.f, 0.0033f, 0.f};
        for (int i = 0; i < 3; ++i) {
            const rt::Mat34 disk = compose(translate({at[i].x, kDeskTop + 0.0016f + lift[i], at[i].z}),
                                           rotate({0, 1, 0}, turn[i]));
            add(kFloppy, kFloppyPlastic, disk);
            add(kUnitBox, kFloppyLabel,
                compose(disk, place({0.f, 0.0017f, 0.012f}, {0.07f, 0.0006f, 0.055f})));
            add(kUnitBox, kFloppyShutter,
                compose(disk, place({0.008f, 0.0017f, -0.03f}, {0.05f, 0.0008f, 0.036f})));
        }
    }
    add(kMug, kMugGlaze, translate({0.44f, kDeskTop, -0.5f}));
    add(kMugHandle, kMugGlaze,
        compose(translate({0.44f + 0.051f, kDeskTop + 0.05f, -0.5f}), rotate({1, 0, 0}, 0.5f * kPi)));

    // --- The spheres: chrome and glass, each on a small dark wooden stand.
    {
        const Vec3 chrome{0.53f, kDeskTop, -0.2f};
        add(kCylinder, kStandWood, place(chrome + Vec3{0, 0.006f, 0}, {0.028f, 0.012f, 0.028f}));
        add(kSphere, kChrome,
            place(chrome + Vec3{0, 0.012f + kChromeRadius - 0.006f, 0},
                  {kChromeRadius, kChromeRadius, kChromeRadius}));
        const Vec3 glass{-0.43f, kDeskTop, -0.2f};
        add(kCylinder, kStandWood, place(glass + Vec3{0, 0.005f, 0}, {0.026f, 0.01f, 0.026f}));
        add(kSphere, kGlass,
            place(glass + Vec3{0, 0.01f + kGlassRadius - 0.007f, 0},
                  {kGlassRadius, kGlassRadius, kGlassRadius}));
    }

    // --- An office chair pushed back from the desk: a five-star base on casters, a gas lift, the seat and
    // the backrest in dark fabric.
    {
        const rt::Mat34 chair = compose(translate({0.3f, 0.f, 0.5f}), rotate({0, 1, 0}, 0.45f));
        const auto part = [&](std::uint32_t mesh, std::uint32_t material, const rt::Mat34& local) {
            add(mesh, material, compose(chair, local));
        };
        for (int i = 0; i < 5; ++i) {
            const float a = 2.f * kPi * static_cast<float>(i) / 5.f + 0.3f;
            const Vec3 tip{0.29f * std::cos(a), 0.f, 0.29f * std::sin(a)};
            part(kCylinder, kChairPlastic,
                 between(Vec3{0.f, 0.075f, 0.f}, tip + Vec3{0.f, 0.06f, 0.f}, 0.016f));
            part(kSphere, kChairPlastic, place(tip + Vec3{0.f, 0.025f, 0.f}, {0.025f, 0.025f, 0.025f}));
        }
        part(kCylinder, kChairPlastic, place({0.f, 0.075f, 0.f}, {0.04f, 0.03f, 0.04f}));
        part(kCylinder, kChrome, place({0.f, 0.27f, 0.f}, {0.022f, 0.36f, 0.022f}));
        part(kSeat, kChairFabric, translate({0.f, 0.48f, 0.f}));
        part(kUnitBox, kChairPlastic, place({0.f, 0.6f, 0.215f}, {0.05f, 0.24f, 0.025f}));
        part(kBackrest, kChairFabric, compose(translate({0.f, 0.82f, 0.235f}), rotate({1, 0, 0}, -0.12f)));
    }

    // --- The lamp: a weighted base, two brushed arms with a joint, the shade, its emitter.
    {
        add(kCylinder, kLampPaint, place(kLampBase + Vec3{0, 0.011f, 0}, {0.075f, 0.022f, 0.075f}));
        add(kCylinder, kLampAluminium, place(kLampBase + Vec3{0, 0.03f, 0}, {0.016f, 0.02f, 0.016f}));
        add(kCylinder, kLampAluminium, between(kLampBase + Vec3{0, 0.035f, 0}, kLampElbow, 0.0075f));
        add(kCylinder, kLampAluminium, between(kLampElbow, kLampHead, 0.0075f));
        add(kSphere, kLampPaint, place(kLampElbow, {0.014f, 0.014f, 0.014f}));
        const Vec3 aim = normalize(kLampAim - kLampHead);
        const rt::Mat34 head = compose(translate(kLampHead), rotationFromTo({0, -1, 0}, aim));
        // The shade's top sits at the head; its opening points along aim.
        const rt::Mat34 shade = compose(head, translate({0.f, -0.13f, 0.f}));
        add(kShade, kLampAluminium, shade);
        add(kSphere, kLampPaint, compose(head, scale({0.016f, 0.016f, 0.016f})));
        const rt::Mat34 emitter =
            compose(shade, compose(translate({0.f, 0.008f, 0.f}), rotate({1, 0, 0}, 0.5f * kPi)));
        add(kEmitter, kLampEmitter, emitter, kLampLight);
        lights[kLampLight] =
            rectLightForQuad(emitter, kLampEmitterSize, kLampEmitterSize, rt::LightKind::Emitter,
                             emitted(static_.materials[kLampEmitter]), kLampWeight);
    }

    lights_.assign(lights.begin(), lights.end());
}

SceneFrame Studio::frame(const show::TimelineFrame& frame) const {
    SceneFrame f;
    f.camera = studioCamera(frame);
    f.instances = instances_;
    f.lights = lights_;
    f.exposureEV = studioExposureEV(frame.effect);
    f.clampIndirect = kClampIndirect;
    return f;
}

} // namespace evo::modern
