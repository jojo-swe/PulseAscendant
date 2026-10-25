#include "modern/studio.hpp"

#include "modern/mesh_builder.hpp"
#include "show/cues.hpp"
#include "show/era.hpp"
#include "show/music_curves.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <stdexcept>

namespace evo::modern {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// --- Layout (metres) ---------------------------------------------------------------------------------------

// The room: x from -2 to 2, the back wall's face at z = -0.95, the front wall's at z = 4, floor 0, ceiling
// 2.7. Walls are 0.1 thick boxes, so the room is closed apart from the window.
constexpr float kRoomLeft = -2.0f, kRoomRight = 2.0f, kRoomBack = -0.95f, kRoomFront = 4.0f;
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
// The diffuser: an 8 cm square recessed 3.5 cm into the shade (from its rim), so that it is hidden from
// every camera and the shade's painted inside spreads its light (Task 14: no flat white plate).
constexpr float kLampEmitterSize = 0.08f, kLampRecess = 0.035f;

// The moon (see Studio::kMoonDirection): an 18 cm square 4 m out, about 2.6 degrees across.
constexpr float kMoonDistance = 4.f, kMoonSize = 0.18f;

// The town's glow: the sodium haze over the streets below and to the left, as a dim warm panel outside the
// window, hidden behind the wall left of it from inside, that throws the window's shape onto the right wall
// and lifts that half of the night wide (the bible's motivated fill; the moon stays the key).
constexpr float kTownWidth = 1.2f, kTownHeight = 0.9f;
constexpr Vec3 kTownAt{-3.0f, 0.6f, -2.6f};      // below the sill and left of the window, seen from inside
constexpr Vec3 kTownAim{2.f, 2.f, 1.6f};          // high on the right wall
constexpr Vec3 kTownColor{1.f, 0.52f, 0.2f};
constexpr float kTownIntensity = 2.4f;

// Contributions after a rough vertex above this luminance are clamped (TraceSettings::clampIndirect; the
// integrator's own default is 16, the Studio keeps the tighter value its look was tuned with). At the Studio's
// exposures (EV 0.3 to 2) a clamp of 2 left lamp-lit surfaces sparkling at export spp, where from 16 spp the
// a-trous runs only its two light iterations; 0.5 barely moves their mean (0.143 -> 0.142, RETURN's close-up).
constexpr float kClampIndirect = 0.5f;

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
    kTownMesh,
    kSeat,
    kBackrest,
    kShadeInner,
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
    kShadeInside,
    kConcreteWall,
    kBuilding,
    kCityWindow,
    kVoid,
    kScreenBack,
    kStar,
    kTownGlow,
    kMaterialCount
};
static_assert(kScreenGlass == Studio::kScreenMaterial && kLampEmitter == Studio::kLampMaterial &&
              kPowerLed == Studio::kLedMaterial && kScreenBack == Studio::kScreenBackMaterial &&
              kMoonGlow == Studio::kMoonMaterial);

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
    // Its painted inside: the profile from inside the rim up to the dome's top, about 2 mm in.
    std::array<Vec2, 7> inner{};
    for (std::size_t i = 0; i < inner.size(); ++i)
        inner[i] = {0.965f * shade[i + 2].x, 0.985f * shade[i + 2].y};
    meshes[kShadeInner] = lathe(inner, 64);
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
    meshes[kTownMesh] = quad(kTownWidth, kTownHeight);
    meshes[kSeat] = bevelBox({0.23f, 0.035f, 0.22f}, 0.03f, 3);
    meshes[kBackrest] = bevelBox({0.21f, 0.22f, 0.03f}, 0.025f, 3);
    return meshes;
}

// The materials with a chapter's light rig: the screen's emissionScale and the lamp's level.
std::vector<rt::Material> buildMaterials(StudioRig rig) {
    std::vector<rt::Material> m(kMaterialCount);
    // Warm white plaster, matte; the ceiling a little darker.
    m[kWallPaint] = plastic({0.66f, 0.64f, 0.6f}, 0.92f);
    m[kCeilingPaint] = plastic({0.56f, 0.55f, 0.53f}, 0.95f);
    // A board-formed concrete wall behind the desk (no joints).
    m[kConcreteWall] = plastic({0.462f, 0.451f, 0.429f}, 0.85f);
    setColor(m[kConcreteWall].baseColor2, {0.33f, 0.325f, 0.314f});
    m[kConcreteWall].pattern = static_cast<std::uint32_t>(rt::Pattern::Concrete);
    m[kConcreteWall].patternScale = 0.f;
    // Polished concrete: cloudy grey under a satin sealer, saw-cut into 1.2 m slabs.
    m[kFloorTiles] = plastic({0.396f, 0.39f, 0.38f}, 0.45f);
    setColor(m[kFloorTiles].baseColor2, {0.275f, 0.27f, 0.264f});
    m[kFloorTiles].pattern = static_cast<std::uint32_t>(rt::Pattern::Concrete);
    m[kFloorTiles].patternScale = 1.2f;
    m[kFloorTiles].coat = 0.5f;
    m[kFloorTiles].coatRoughness = 0.12f;
    // Oiled oak planks under a satin coat.
    m[kDeskWood] = plastic({0.56f, 0.38f, 0.21f}, 0.6f);
    setColor(m[kDeskWood].baseColor2, {0.37f, 0.22f, 0.1f});
    m[kDeskWood].pattern = static_cast<std::uint32_t>(rt::Pattern::WoodPlanks);
    m[kDeskWood].patternScale = 0.16f;
    m[kDeskWood].coat = 0.6f;
    m[kDeskWood].coatRoughness = 0.15f;
    m[kBrushedSteel] = metal({0.62f, 0.62f, 0.64f}, 0.28f);
    m[kBrushedSteel].pattern = static_cast<std::uint32_t>(rt::Pattern::BrushedMetal);
    // Worn beige ABS: a little yellowed, satin.
    m[kBeige] = plastic({0.7f, 0.64f, 0.52f}, 0.5f);
    m[kBeigeDark] = plastic({0.56f, 0.51f, 0.42f}, 0.55f);
    // The picture: phosphor behind grey-green tube glass (Emissive with the retro picture; a smooth coat on
    // the curved glass reflects the room; the phosphor's green-grey base catches the lamp where the picture
    // is dark). Not clean: dust specks and a fingerprint low right (Pattern CrtGlass).
    m[kScreenGlass] = plastic({0.07f, 0.086f, 0.076f}, 0.34f);
    m[kScreenGlass].pattern = static_cast<std::uint32_t>(rt::Pattern::CrtGlass);
    m[kScreenGlass].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    m[kScreenGlass].texture = rt::kRetroScreenTexture;
    m[kScreenGlass].emissionScale = rig.screen;
    m[kScreenGlass].coat = 1.f;
    m[kScreenGlass].coatRoughness = 0.03f;
    m[kRecess] = plastic({0.06f, 0.06f, 0.06f}, 0.6f);
    // Key caps: soft, slightly rubbery; light letters, darker modifiers; yellowed ABS, pale in the middle of
    // a cap's top and darker towards its edges (Pattern KeyCap).
    m[kKeyLight] = plastic({0.76f, 0.72f, 0.62f}, 0.62f);
    m[kKeyDark] = plastic({0.44f, 0.42f, 0.38f}, 0.62f);
    m[kKeyLight].pattern = m[kKeyDark].pattern = static_cast<std::uint32_t>(rt::Pattern::KeyCap);
    m[kDriveSlot] = plastic({0.025f, 0.025f, 0.028f}, 0.45f);
    // The power LEDs: green, seen by the camera only (their glow is the bloom's, step 6): a BSDF ray after a
    // rough vertex that found them would be a firefly no light sample could ever take.
    m[kPowerLed] = plastic({0.1f, 0.4f, 0.1f}, 0.2f);
    m[kPowerLed].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    setColor(m[kPowerLed].emission, {0.15f, 1.f, 0.25f});
    m[kPowerLed].emissionScale = 6.f;
    m[kPowerLed].flags = rt::kMaterialCameraOnlyEmission;
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
    // The lamp's shade and base: black powder coat; the shade is painted a matte warm grey inside (a diffuse
    // inside spreads the recessed diffuser's light without the aluminium's caustic speckle; darker than
    // white, since nothing samples the light it bounces).
    m[kLampPaint] = plastic({0.022f, 0.022f, 0.024f}, 0.4f);
    m[kLampPaint].coat = 0.5f;
    m[kLampPaint].coatRoughness = 0.18f;
    m[kShadeInside] = plastic({0.55f, 0.52f, 0.47f}, 0.95f);
    // The lamp's bulb behind a frosted diffuser: warm (1.0, 0.75, 0.5) x 50 at full, also the Emitter light.
    m[kLampEmitter] = plastic({0.8f, 0.8f, 0.8f}, 0.9f);
    m[kLampEmitter].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    setColor(m[kLampEmitter].emission, Studio::kLampColor);
    m[kLampEmitter].emissionScale = Studio::kLampIntensity * rig.lamp;
    // A slim black aluminium window frame.
    m[kWindowFrame] = metal({0.03f, 0.03f, 0.032f}, 0.45f);
    m[kMousePad] = plastic({0.03f, 0.035f, 0.06f}, 0.95f);
    m[kSkirting] = plastic({0.035f, 0.035f, 0.037f}, 0.6f);
    m[kChairFabric] = plastic({0.05f, 0.05f, 0.06f}, 0.95f);
    m[kChairPlastic] = plastic({0.025f, 0.025f, 0.027f}, 0.35f);
    m[kMoonGlow] = plastic({0.f, 0.f, 0.f}, 1.f);
    m[kMoonGlow].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    setColor(m[kMoonGlow].emission, Studio::kMoonColor);
    m[kMoonGlow].emissionScale = Studio::kMoonIntensity;
    // The skyline outside: dark towers with lit windows, which only the camera sees (they light nothing).
    m[kBuilding] = plastic({0.012f, 0.012f, 0.014f}, 0.8f);
    m[kCityWindow] = plastic({0.f, 0.f, 0.f}, 1.f);
    m[kCityWindow].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    setColor(m[kCityWindow].emission, {1.f, 0.72f, 0.42f});
    m[kCityWindow].emissionScale = 0.6f;
    m[kCityWindow].flags = rt::kMaterialCameraOnlyEmission;
    // Behind the screen (PORTAL's and REBOOT's crossings): the dark the 1993 stars hang in, reflecting
    // nothing, with the faintest blue glow (seen only, lighting nothing: the volume of nothing has a colour
    // in 2026); the screen seen from behind, thin glass that shows the picture, dimmed, as an emission mask
    // (black is clear: emissive.rchit) and lets the room through; the stars, emissive spheres (their colour
    // times emission is the radiance).
    m[kVoid] = plastic({0.f, 0.f, 0.f}, 1.f);
    m[kVoid].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    setColor(m[kVoid].emission, {0.35f, 0.45f, 1.f});
    m[kVoid].emissionScale = 0.0026f;
    m[kVoid].flags = rt::kMaterialCameraOnlyEmission;
    m[kScreenBack] = plastic({0.8f, 0.86f, 0.83f}, 0.f);
    m[kScreenBack].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    m[kScreenBack].texture = rt::kRetroScreenTexture;
    m[kScreenBack].emissionScale = Studio::kScreenBackLevel * rig.screen;
    m[kScreenBack].transmission = 1.f;
    m[kScreenBack].ior = 1.5f;
    m[kScreenBack].flags = rt::kMaterialThinWalled;
    m[kStar] = plastic({0.f, 0.f, 0.f}, 1.f);
    m[kStar].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    m[kTownGlow] = plastic({0.f, 0.f, 0.f}, 1.f);
    m[kTownGlow].materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    setColor(m[kTownGlow].emission, kTownColor);
    m[kTownGlow].emissionScale = kTownIntensity;
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
//
// Every Studio move is authored in show seconds, so that a move can run across a chapter join (FINALE into
// PORTAL into STUDIO NIGHT, RETURN into REBOOT) without a seam. A path is a list of timed keys: the eye, and
// the view as yaw (0 looks along -z, 90 along +x), pitch (up) and roll, in degrees. Between keys it is a
// cubic Hermite with Catmull-Rom tangents from the neighbouring keys (non-uniform: divided by their time
// apart); the first and last key of a path, and every key marked `rest`, have zero velocity, so a move
// eases in and out like a smoothstep. Yaw is unwrapped (PORTAL's turn runs past 360).

struct Pose {
    Vec3 eye;
    float yaw = 0, pitch = 0, roll = 0;
};

struct PoseKey {
    double t;
    Pose pose;
    bool rest = false;
};

constexpr float kDegrees = kPi / 180.f;

Vec3 forwardOf(float yaw, float pitch) {
    const float y = yaw * kDegrees, p = pitch * kDegrees;
    return {std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
}

// The yaw and pitch that look from eye at target.
Pose look(Vec3 eye, Vec3 target, float roll = 0) {
    const Vec3 d = normalize(target - eye);
    return {eye, std::atan2(d.x, -d.z) / kDegrees, std::asin(std::clamp(d.y, -1.f, 1.f)) / kDegrees, roll};
}

// A pose's numbers, so that the path interpolates them alike.
using PoseVector = std::array<float, 6>;
PoseVector pack(const Pose& p) {
    return {p.eye.x, p.eye.y, p.eye.z, p.yaw, p.pitch, p.roll};
}
Pose unpack(const PoseVector& v) {
    return {{v[0], v[1], v[2]}, v[3], v[4], v[5]};
}

Pose alongPath(std::span<const PoseKey> keys, double t) {
    const std::size_t n = keys.size();
    if (t <= keys.front().t)
        return keys.front().pose;
    if (t >= keys.back().t)
        return keys.back().pose;
    std::size_t i = 0;
    while (i + 2 < n && t >= keys[i + 1].t)
        ++i;
    const auto tangent = [&](std::size_t k) {
        PoseVector m{};
        if (k == 0 || k + 1 == n || keys[k].rest)
            return m;
        const PoseVector a = pack(keys[k - 1].pose), b = pack(keys[k + 1].pose);
        const auto span = static_cast<float>(keys[k + 1].t - keys[k - 1].t);
        for (std::size_t c = 0; c < m.size(); ++c)
            m[c] = (b[c] - a[c]) / span;
        return m;
    };
    const auto h = static_cast<float>(keys[i + 1].t - keys[i].t);
    const auto s = static_cast<float>((t - keys[i].t) / (keys[i + 1].t - keys[i].t));
    const float s2 = s * s, s3 = s2 * s;
    const float h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
    const PoseVector p0 = pack(keys[i].pose), p1 = pack(keys[i + 1].pose);
    const PoseVector m0 = tangent(i), m1 = tangent(i + 1);
    PoseVector out{};
    for (std::size_t c = 0; c < out.size(); ++c)
        out[c] = h00 * p0[c] + h10 * h * m0[c] + h01 * p1[c] + h11 * h * m1[c];
    return unpack(out);
}

Vec3 screenCentre() {
    return {0.f, kScreenCentreY, kScreenZ};
}
// A point relative to the screen's centre.
Vec3 fromScreen(float x, float y, float z) {
    return screenCentre() + Vec3{x, y, z};
}

// The landmarks of the Studio's chapters in show seconds (transitionMarks), found once.
struct ChapterMarks {
    double start = 0, end = 0, accent = 0, downbeat = 0, bar = 2;
    double twoBarsOn = 0; // the tracked bar line two bars after D (PORTAL's way out)
};
struct Landmarks {
    ChapterMarks flyby, finale, portal, night, ret, reboot;
};

ChapterMarks marksOf(show::Effect effect) {
    for (int c = 0; c < show::chapterCount(); ++c) {
        if (show::cues[static_cast<std::size_t>(c)].effect != effect)
            continue;
        const show::TimelineFrame head = show::sampleTimeline(show::chapterStart(c));
        const show::TransitionMarks m = show::transitionMarks(head);
        const show::BeatGridView grid = show::beatGrid(head.track);
        const double offset = show::trackStart(head.track);
        const double twoBarsOn = grid.beatTime(grid.beatAt(m.downbeat - offset + 1e-9) + 8) + offset;
        return {show::chapterStart(c), show::chapterStart(c + 1), m.accent, m.downbeat, m.bar, twoBarsOn};
    }
    throw std::logic_error("the show has no chapter for a Studio effect");
}

const Landmarks& landmarks() {
    static const Landmarks marks{marksOf(show::Effect::Flyby),  marksOf(show::Effect::Finale),
                                 marksOf(show::Effect::Portal), marksOf(show::Effect::StudioNight),
                                 marksOf(show::Effect::Return), marksOf(show::Effect::Reboot)};
    return marks;
}

// The distance from the screen's plane at which the picture spans kFillSpan of the frame's width on the 35 mm
// lens: where FLYBY's pull-out crosses the glass (its first frame matches crt.comp's last, studioScreenSpan).
constexpr float kFillSpan = 1.02f;
float fillDistance() {
    return 0.5f * kScreenWidth / (kFillSpan * std::tan(0.5f * kLens35 * kDegrees));
}
// The pillarbox distance: the picture spans 3/4 of the 16:9 frame's width, as crt.comp shows it full frame.
float pillarboxDistance() {
    return fillDistance() * kFillSpan / 0.75f;
}

// FLYBY: the pull back out of the CRT. Before D the 1993 picture is full frame and
// crt.comp shows the camera's approach on it (studioScreenSpan): from the pillarbox distance at D - 2 bars it
// eases in to six times closer by D - 0.5 s, where the beam and the grille resolve, then pulls back; on D
// the Studio takes over at the glass with the picture filling the frame, and the pull continues out to
// 1.2 m by A + 1 s: the room is revealed on the accent. From D + 1 bar one slow arc round the desk to the
// right ends on the CRT in profile, the 1993 ship still flying on it.
std::vector<PoseKey> flybyKeys() {
    const ChapterMarks& m = landmarks().flyby;
    const float fill = fillDistance(), pillarbox = pillarboxDistance();
    const Vec3 c = screenCentre();
    const Vec3 monitor{0.f, 1.02f, -0.42f}; // the arc's aim: the monitor's body
    std::vector<PoseKey> keys = {
        {m.downbeat - 2 * m.bar, look(fromScreen(0, 0, pillarbox), c), true},
        {m.downbeat - 0.5, look(fromScreen(0, 0, pillarbox / 6.f), c), true},
        {m.downbeat, look(fromScreen(0, 0, fill), c)},
        {m.accent + 1.0, look(fromScreen(0.1f, 0.07f, 1.2f), fromScreen(0.02f, -0.02f, 0.f)), true},
        {m.downbeat + 1 * m.bar, look(fromScreen(0.1f, 0.07f, 1.2f), fromScreen(0.02f, -0.02f, 0.f)), true},
    };
    // The arc: round the monitor's body to the right, rising a little, closing in to a three-quarter profile.
    const double arcStart = m.downbeat + m.bar, arcEnd = m.end;
    const Vec3 pivot{0.f, 1.02f, -0.42f};
    for (int k = 1; k <= 3; ++k) {
        const float f = static_cast<float>(k) / 3.f;
        const float azimuth = (6.f + 66.f * f) * kDegrees, radius = 1.33f - 0.28f * f;
        const Vec3 eye{pivot.x + radius * std::sin(azimuth), 1.27f + 0.05f * f,
                       pivot.z + radius * std::cos(azimuth)};
        keys.push_back({arcStart + (arcEnd - arcStart) * f, look(eye, monitor + Vec3{0.f, 0.03f * f, 0.f})});
    }
    keys.back().rest = true;
    return keys;
}

// FINALE, PORTAL and the start of STUDIO NIGHT: one take across both joins.
//   FINALE       35 mm, 1.5 m from the CRT, handheld; it closes in over the last bar, into PORTAL;
//   PORTAL S0    the dolly into the CRT as the handheld fades: the screen fills the frame by D - 0.5 s and
//                the camera crosses the glass on D (shot 1 from D, on a 24 mm lens);
//   inside       the 1993 flyby's starfield as 2026 stars, in the dark behind the screen. The camera turns
//                right, fastest on the accent: half way round it faces the back of the CRT's glass, the
//                room hanging in space beyond it, then drifts back to the glass sideways and crosses it
//                outward on the bar two bars after D (shot 2, 35 mm);
//   out          still turning, it pulls back from the screen into the room and lands on STUDIO NIGHT's
//                opening wide on 194.00, the act's join;
//   STUDIO NIGHT the dolly that never stops: on back to the night wide, round to the left behind the chair
//                in the moonbeam towards the window (by D - 4 bars), a tilt up to the sky as it nears the
//                pane, through the pane on the accent, and on out over the town.
std::vector<PoseKey> portalKeys() {
    const Landmarks& l = landmarks();
    const ChapterMarks &finale = l.finale, &portal = l.portal, &night = l.night;
    const Vec3 c = screenCentre();
    const double in = portal.downbeat, out = portal.twoBarsOn;
    const Pose wide = look({0.16f, 1.22f, kScreenZ + 1.49f}, fromScreen(0.04f, -0.09f, 0.f));
    std::vector<PoseKey> keys = {
        {finale.start, wide, true},
        {finale.end - finale.bar, wide, true},
        {portal.start + 1.25, look(fromScreen(0.1f, 0.07f, 1.08f), fromScreen(0.02f, -0.02f, 0.f))},
        {in - 2 * portal.bar, look(fromScreen(0.03f, 0.02f, 0.62f), c)},
        {in - 0.5, look(fromScreen(0.f, 0.f, 0.21f), c)},
        // The glass on D, already turning a little; the turn's fastest part on the accent.
        {in, {fromScreen(0.f, 0.f, 0.f), 8.f, 0.f, 0.f}},
        {in + 0.5, {fromScreen(0.012f, 0.004f, -0.15f), 55.f, 1.f, -2.f}},
        {portal.accent, {fromScreen(0.05f, 0.01f, -0.29f), 135.f, 2.f, -3.f}},
        // Facing the back of the glass, the room beyond it: the slowest part of the turn.
        {in + portal.bar, {fromScreen(0.075f, 0.012f, -0.37f), 175.f, 1.f, -1.5f}},
        {out - 1.02, {fromScreen(0.06f, 0.006f, -0.3f), 215.f, 0.f, 0.f}},
        {out - 0.52, {fromScreen(0.02f, 0.f, -0.17f), 248.f, -1.f, 0.f}},
        // Out through the glass sideways, still turning.
        {out, {fromScreen(-0.04f, 0.f, 0.f), 268.f, -2.f, 0.f}},
        {out + 0.83, {{-0.15f, 1.12f, 0.55f}, 330.f, -8.f, 0.f}},
        // STUDIO NIGHT's first frame: the CRT on the right, the window's sill on the left.
        {night.start, {{-0.25f, 1.15f, 1.05f}, 350.f, -9.f, 0.f}},
        // The night wide, on back and low: the DOS prompt on the CRT, the moonbeam on the floor and the chair
        // standing in it.
        {night.start + 4.0, {{-0.25f, 0.95f, 3.3f}, 352.f, -8.f, 0.f}},
        // Round to the left, behind the chair, towards the window.
        {night.start + 6.3, {{-1.1f, 1.15f, 2.3f}, 366.f, -5.f, 0.f}},
        {night.downbeat - 4 * night.bar, {{-1.45f, 1.3f, 1.2f}, 376.f, -2.f, 0.f}},
        // Nearing the window, tilting up to the sky.
        {night.downbeat - 1.5 * night.bar, {{-1.33f, 1.52f, -0.45f}, 372.f, 16.f, 0.f}},
        {night.downbeat, {{-1.12f, 1.71f, -0.84f}, 368.f, 10.f, 0.f}},
        // Through the pane on the accent: the upper right pane, clear of the mullion and the transom.
        {night.accent, {{-1.04f, 1.79f, kRoomBack - 0.05f}, 366.f, 3.f, 0.f}},
        {night.end - 1.6, {{-0.86f, 2.05f, -2.4f}, 362.f, -4.f, 0.f}},
        {night.end, {{-0.8f, 2.12f, -2.85f}, 361.f, -5.f, 0.f}},
    };
    return keys;
}

// RETURN and REBOOT: one take from RETURN's D across the act join.
//   RETURN S0   outside the window at the first grey of dawn, looking in, where ASCENT left;
//   RETURN S1   it descends through the window (the lamp is on, the CRT dark), past the desk's left;
//   RETURN S2   a cut on D to the CRT close-up on the 50 mm lens, handheld, pushing in from 65 cm to 40 cm
//               by the act's join, where REBOOT holds it;
//   REBOOT S1   the dolly into the screen from D - 4 bars, crossing the glass on D.
std::vector<PoseKey> returnKeys() {
    const ChapterMarks& m = landmarks().ret;
    const Vec3 room{-0.35f, 0.92f, -0.1f};
    return {
        {m.start, look({-0.96f, 2.3f, -2.75f}, {-0.6f, 1.1f, 0.6f}), true},
        {m.downbeat - 4 * m.bar, look({-1.02f, 1.98f, -1.75f}, {-0.55f, 1.0f, 0.6f})},
        {m.downbeat - 2.2 * m.bar, look({-1.04f, 1.76f, kRoomBack - 0.05f}, {-0.5f, 0.95f, 0.6f})},
        {m.downbeat - 0.9 * m.bar, look({-1.12f, 1.48f, -0.3f}, room)},
        {m.downbeat, look({-1.3f, 1.34f, 0.32f}, room), true},
    };
}

std::vector<PoseKey> closeUpKeys() {
    const Landmarks& l = landmarks();
    const Vec3 c = screenCentre();
    const double glass = l.reboot.downbeat;
    return {
        {l.ret.downbeat, look(fromScreen(0.f, 0.f, 0.65f), c), true},
        {l.ret.end, look(fromScreen(0.f, 0.f, 0.4f), c), true},
        {glass - 4 * l.reboot.bar, look(fromScreen(0.f, 0.f, 0.4f), c), true},
        {glass, {fromScreen(0.f, 0.f, 0.f), 0.f, 0.f, 0.f}},
        {glass + 1.5, look(fromScreen(0.f, 0.f, -0.16f), fromScreen(0.f, 0.f, -1.f)), true},
    };
}

const std::vector<PoseKey>& cachedKeys(int which) {
    static const std::array<std::vector<PoseKey>, 4> paths = {flybyKeys(), portalKeys(), returnKeys(),
                                                              closeUpKeys()};
    return paths[static_cast<std::size_t>(which)];
}
enum PathId { kFlybyPath, kPortalPath, kReturnPath, kCloseUpPath };

// The handheld's weight in a chapter (studioHandheld itself is 0 near every glass time): Studio chapters
// only; FLYBY from its glass on, FINALE fully, PORTAL fading out over its dolly in (by the approach), none in
// STUDIO NIGHT's dolly or RETURN's crane, the close-up from RETURN's cut through REBOOT's glass.
float handheldWeight(const show::TimelineFrame& frame) {
    const Landmarks& l = landmarks();
    const double t = frame.showSeconds;
    const auto smooth = [](double a, double b, double x) {
        const double s = std::clamp((x - a) / (b - a), 0.0, 1.0);
        return static_cast<float>(s * s * (3 - 2 * s));
    };
    switch (frame.effect) {
    case show::Effect::Flyby:
        return t >= l.flyby.downbeat ? 1.f : 0.f;
    case show::Effect::Finale:
        return 1.f;
    case show::Effect::Portal:
        return 1.f - smooth(l.portal.start, l.portal.downbeat - 2 * l.portal.bar, t);
    case show::Effect::Return:
        return t >= l.ret.downbeat ? 1.f : 0.f;
    case show::Effect::Reboot:
        return t < l.reboot.downbeat ? 1.f : 0.f;
    default:
        return 0.f;
    }
}

Camera poseCamera(const Pose& pose, float lensDegrees, float aperture, float focus, std::uint32_t shot) {
    const Vec3 forward = forwardOf(pose.yaw, pose.pitch);
    const Vec3 right = normalize(cross(forward, Vec3{0, 1, 0}));
    const Vec3 up = cross(right, forward);
    const float roll = pose.roll * kDegrees;
    Camera c;
    c.eye = pose.eye;
    c.target = pose.eye + forward * 0.5f;
    c.up = up * std::cos(roll) + right * std::sin(roll);
    c.fovYDegrees = lensFovY(lensDegrees);
    // At the glass the focus is millimetres away: the aperture closes down with it (to a twentieth of the
    // distance), so the picture stays sharp as the camera passes it.
    c.focusDistance = std::max(focus, 0.002f);
    c.aperture = std::min(aperture, 0.05f * c.focusDistance);
    c.shot = shot;
    return c;
}

// The focus on the screen's plane (where the camera looks at it), so the CRT's picture stays sharp.
float screenFocus(const Pose& pose) {
    return dot(screenCentre() - pose.eye, forwardOf(pose.yaw, pose.pitch));
}

Pose withHandheld(Pose pose, const StudioHandheld& h, float weight) {
    if (weight <= 0.f)
        return pose;
    const Vec3 forward = forwardOf(pose.yaw, pose.pitch);
    const Vec3 right = normalize(cross(forward, Vec3{0, 1, 0}));
    const Vec3 up = cross(right, forward);
    pose.eye = pose.eye + (right * h.offset.x + up * h.offset.y - forward * h.offset.z) * weight;
    pose.yaw += h.yaw * weight;
    pose.pitch += h.pitch * weight;
    pose.roll += h.roll * weight;
    return pose;
}

// The hash of the handheld's noise (lowbias32).
std::uint32_t hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// Smooth value noise in [-1, 1]: hashed values on knots kNoiseRate a second, eased between (C1).
constexpr double kNoiseRate = 1.6;
float valueNoise(double t, std::uint32_t channel) {
    const double x = t * kNoiseRate;
    const double knot = std::floor(x);
    const auto at = [&](double k) {
        const auto i = static_cast<std::uint32_t>(static_cast<std::int64_t>(k));
        return static_cast<float>(hash32(i * 0x9e3779b9u + channel * 0x85ebca6bu + 0x2026u)) / 4294967296.f *
                   2.f -
               1.f;
    };
    const auto s = static_cast<float>(x - knot);
    const float e = s * s * (3.f - 2.f * s);
    return at(knot) + (at(knot + 1) - at(knot)) * e;
}

// The times the camera passes a glass: FLYBY's D, PORTAL's crossings (D and the bar two bars on), REBOOT's D
// and STUDIO NIGHT's window pane (its accent).
const std::array<double, 5>& glassTimes() {
    static const std::array<double, 5> times = [] {
        const Landmarks& l = landmarks();
        return std::array<double, 5>{l.flyby.downbeat, l.portal.downbeat, l.portal.twoBarsOn, l.night.accent,
                                     l.reboot.downbeat};
    }();
    return times;
}

} // namespace

float lensFovY(float horizontalDegrees) {
    const float half = 0.5f * horizontalDegrees * kDegrees;
    return 2.f * std::atan(std::tan(half) * 9.f / 16.f) / kDegrees;
}

std::array<Vec3, 4> studioScreenCorners() {
    const float l = -0.5f * kScreenWidth, r = 0.5f * kScreenWidth;
    const float t = kScreenCentreY + 0.5f * kScreenHeight, b = kScreenCentreY - 0.5f * kScreenHeight;
    return {Vec3{l, t, kScreenZ}, Vec3{r, t, kScreenZ}, Vec3{r, b, kScreenZ}, Vec3{l, b, kScreenZ}};
}

std::vector<double> studioGlassTimes() {
    const auto& times = glassTimes();
    return {times.begin(), times.end()};
}

StudioHandheld studioHandheld(double showSeconds) {
    // Off within 0.5 s of a glass, easing back in over the next second.
    float gate = 1.f;
    for (const double glass : glassTimes()) {
        const double s = std::clamp((std::abs(showSeconds - glass) - 0.5) / 1.0, 0.0, 1.0);
        gate = std::min(gate, static_cast<float>(s * s * (3 - 2 * s)));
    }
    if (gate <= 0.f)
        return {};
    // Each channel: three sines at incommensurate rates within 0.4 - 1.1 Hz and a slower hashed wander,
    // scaled so that the sum never exceeds its limit (0.72 + 0.27 < 1).
    const double tau = 2 * std::numbers::pi;
    const auto channel = [&](std::uint32_t k, float limit) {
        const double phase = 0.7 * k;
        const double sines = 0.36 * std::sin(tau * 0.53 * showSeconds + 1.3 + phase) +
                             0.22 * std::sin(tau * 0.79 * showSeconds + 0.4 + 2.1 * phase) +
                             0.14 * std::sin(tau * 1.07 * showSeconds + 2.9 + 3.3 * phase);
        return gate * limit * (static_cast<float>(sines) + 0.27f * valueNoise(showSeconds, k));
    };
    // Three channels each: the offset's length and the rotation's angle stay within the limits.
    const float metres = kHandheldMetres / std::sqrt(3.f), degrees = kHandheldDegrees / std::sqrt(3.f);
    StudioHandheld h;
    h.offset = {channel(0, metres), channel(1, metres), channel(2, metres)};
    h.yaw = channel(3, degrees);
    h.pitch = channel(4, degrees);
    h.roll = channel(5, degrees);
    return h;
}

std::vector<StudioShot> studioShots(const show::TimelineFrame& frame) {
    const Landmarks& l = landmarks();
    const auto duration = static_cast<float>(frame.chapterDuration);
    const auto local = [&](double showSeconds, const ChapterMarks& m) {
        return std::clamp(static_cast<float>(showSeconds - m.start), 0.f, duration);
    };
    switch (frame.effect) {
    case show::Effect::Portal: {
        const float in = local(l.portal.downbeat, l.portal), out = local(l.portal.twoBarsOn, l.portal);
        return {{0.f, in}, {in, out}, {out, duration}};
    }
    case show::Effect::Return: {
        const float cut = local(l.ret.downbeat, l.ret);
        return {{0.f, cut}, {cut, duration}};
    }
    default:
        return {{0.f, duration}};
    }
}

float studioScreenSpan(const show::TimelineFrame& frame) {
    const Camera camera = studioCamera(frame);
    const float distance = std::max(dot(screenCentre() - camera.eye, cameraBasis(camera).forward), 1e-4f);
    const float halfWidth = distance * std::tan(0.5f * kLens35 * kDegrees);
    return 0.5f * kScreenWidth / halfWidth;
}

Camera studioCamera(const show::TimelineFrame& frame) {
    const Landmarks& l = landmarks();
    const double t = frame.showSeconds;
    const std::vector<StudioShot> shots = studioShots(frame);
    std::uint32_t shot = 0;
    while (shot + 1 < shots.size() && static_cast<float>(frame.localSeconds) >= shots[shot + 1].start)
        ++shot;
    const StudioHandheld handheld = studioHandheld(t);
    const float weight = handheldWeight(frame);

    switch (frame.effect) {
    case show::Effect::Flyby: {
        const Pose pose = withHandheld(alongPath(cachedKeys(kFlybyPath), t), handheld, weight);
        return poseCamera(pose, kLens35, 0.003f, screenFocus(pose), shot);
    }
    case show::Effect::Finale:
    case show::Effect::Portal: {
        const Pose pose = withHandheld(alongPath(cachedKeys(kPortalPath), t), handheld, weight);
        if (frame.effect == show::Effect::Portal && shot == 1) // behind the glass, among the stars
            return poseCamera(pose, kLens24, 0.001f, 0.3f, shot);
        return poseCamera(pose, kLens35, 0.003f, screenFocus(pose), shot);
    }
    case show::Effect::StudioNight: {
        // The dolly's focus follows from the screen's plane to the window's as it nears the pane.
        const Pose pose = alongPath(cachedKeys(kPortalPath), t);
        const Vec3 forward = forwardOf(pose.yaw, pose.pitch);
        const auto s = static_cast<float>(
            std::clamp((t - l.night.start) / (l.night.downbeat - 4 * l.night.bar - l.night.start), 0.0, 1.0));
        const float toWindow = std::max(dot(Vec3{-1.2f, 1.5f, kRoomBack} - pose.eye, forward), 0.3f);
        const float onScreen = std::max(screenFocus(pose), 0.3f);
        const float near = onScreen + (toWindow - onScreen) * s * s * (3 - 2 * s);
        // Through the pane the focus racks out to the town.
        const auto r = static_cast<float>(std::clamp((t - l.night.accent + 0.5) / 2.0, 0.0, 1.0));
        return poseCamera(pose, kLens35, 0.004f, near * std::pow(30.f / near, r * r * (3 - 2 * r)), shot);
    }
    case show::Effect::Return:
        if (shot == 0) {
            const Pose pose = alongPath(cachedKeys(kReturnPath), t);
            return poseCamera(pose, kLens35, 0.004f,
                              dot(Vec3{-0.35f, 0.92f, -0.1f} - pose.eye, forwardOf(pose.yaw, pose.pitch)),
                              shot);
        }
        [[fallthrough]];
    default: {
        // RETURN's close-up and REBOOT (and anything else): 40 cm from the glass on the 50 mm lens.
        const Pose pose = withHandheld(alongPath(cachedKeys(kCloseUpPath), t), handheld, weight);
        return poseCamera(pose, kLens50, 0.003f, screenFocus(pose), shot);
    }
    }
}

float studioExposureEV(show::Effect effect) {
    switch (effect) {
    case show::Effect::StudioNight:
        return 2.f; // the screen at 8: a display gain of 28 for the grey DOS text; the moon fills the room
    case show::Effect::Flyby:
        return 1.4f; // the screen at 2.4 with the 1993 ship on black: the room is lit by phosphor and moon
    case show::Effect::Finale:
    case show::Effect::Portal:
        return 0.2f; // the screen at 2.4: a gain of 2.4, so the megamix's colours stay saturated
    case show::Effect::Reboot:
        return 0.8f; // a close-up of the screen at 1.5: a gain of 2.3
    default:
        return 1.f; // RETURN: the screen at 1.5, a gain of 2.6 for the DOS text; the lamp is back on
    }
}

EnvironmentDesc studioEnvironment() {
    EnvironmentDesc sky;
    sky.discDirection = Studio::kMoonDirection;
    sky.horizon = {0.06f, 0.1f, 0.22f}; // grey-blue, not lavender: the stars are the only white
    sky.haze = 0.15f;
    return sky;
}

StudioRig studioRig(show::Effect effect) {
    switch (effect) {
    case show::Effect::StudioNight:
        return {0.f, 8.f};
    case show::Effect::Flyby:
        return {0.f, 2.4f};
    case show::Effect::Finale:
    case show::Effect::Portal:
        return {0.2f, 2.4f};
    default:
        return {1.f, 1.5f};
    }
}

namespace {

// FLYBY's exposure on its glass: the screen's 2.4 times 2^EV near 1, as crt.comp's picture.
constexpr float kFlybyGlassEV = -1.25f;
// RETURN's extra exposure before its cut, while the camera comes in through the window.
constexpr float kDawnEV = 0.8f;
// RETURN's dawn: the horizon's grey-blue at its end.
constexpr Vec3 kDawnHorizon{0.1f, 0.14f, 0.22f};
// STUDIO NIGHT's exposure once the camera is at the window looking out: the room's 2 EV would show the night
// sky as a pale dusk; the eye adapts to the sky as the window fills the frame (D - 5 bars to D - 2 bars).
constexpr float kNightOutsideEV = 0.3f;
// REBOOT's exposure on its glass (D): the screen's 1.5 times 2^EV near 1, as crt.comp's picture, so the
// dolly into the picture lands on the 1993 frame's own brightness (FLYBY's approach, reversed).
constexpr float kRebootGlassEV = -0.4f;

float smoothBetween(double from, double to, double t) {
    const double s = std::clamp((t - from) / (to - from), 0.0, 1.0);
    return static_cast<float>(s * s * (3 - 2 * s));
}

} // namespace

EnvironmentDesc studioEnvironmentAt(const show::TimelineFrame& frame) {
    EnvironmentDesc sky = studioEnvironment();
    if (frame.effect == show::Effect::Return || frame.effect == show::Effect::Reboot) {
        // Dawn's first grey: the horizon brightens to grey-blue over RETURN, the moon (and its glow) gone.
        const double start = landmarks().ret.start, end = landmarks().ret.end;
        const float s = frame.effect == show::Effect::Reboot ? 1.f : smoothBetween(start, end, frame.showSeconds);
        const Vec3 night = sky.horizon;
        sky.horizon = night + (kDawnHorizon - night) * s;
        sky.glow = 0.f;
    }
    return sky;
}

StudioRig studioRigAt(const show::TimelineFrame& frame) {
    StudioRig rig = studioRig(frame.effect);
    if (frame.effect == show::Effect::Portal) {
        const ChapterMarks& m = landmarks().portal;
        const StudioRig night = studioRig(show::Effect::StudioNight);
        const float s = smoothBetween(m.downbeat, m.accent, frame.showSeconds);
        rig = {rig.lamp + (night.lamp - rig.lamp) * s, rig.screen + (night.screen - rig.screen) * s};
    }
    return rig;
}

float studioExposureAt(const show::TimelineFrame& frame) {
    const Landmarks& l = landmarks();
    const double t = frame.showSeconds;
    const float own = studioExposureEV(frame.effect);
    switch (frame.effect) {
    case show::Effect::Flyby:
        // Through the glass the picture fills the frame at about the brightness crt.comp showed it at (a
        // display gain near 1); as the pull-out leaves the screen the exposure opens up to the dark room by
        // A + 0.25 s, so the room is there on the accent. (Before D the Studio is not shown: it closes down
        // over the approach, as the camera nears the picture.)
        {
            const float glass =
                t < l.flyby.downbeat
                    ? smoothBetween(l.flyby.downbeat - 2 * l.flyby.bar, l.flyby.downbeat - 0.5, t)
                    : 1.f - smoothBetween(l.flyby.downbeat, l.flyby.accent + 0.25, t);
            return own + (kFlybyGlassEV - own) * glass;
        }
    case show::Effect::Portal: {
        // Behind the glass the exposure opens up with the rig to STUDIO NIGHT's, which the camera lands in.
        const float night = studioExposureEV(show::Effect::StudioNight);
        return own + (night - own) * smoothBetween(l.portal.downbeat, l.portal.accent, t);
    }
    case show::Effect::StudioNight: {
        const ChapterMarks& m = l.night;
        return own + (kNightOutsideEV - own) * smoothBetween(m.downbeat - 6 * m.bar, m.downbeat - 3 * m.bar, t);
    }
    case show::Effect::Reboot: {
        const ChapterMarks& m = l.reboot;
        return own + (kRebootGlassEV - own) * smoothBetween(m.downbeat - 3 * m.bar, m.downbeat - 0.5, t);
    }
    case show::Effect::Return: {
        // Before the cut to the close-up the camera comes in from the grey dawn outside: the exposure is
        // kDawnEV more open, for the dark wall round the window.
        if (t < l.ret.downbeat)
            return own + kDawnEV;
        // Over the last bar, to REBOOT's, which the close-up runs on into.
        const float reboot = studioExposureEV(show::Effect::Reboot);
        return own + (reboot - own) * smoothBetween(l.ret.end - l.ret.bar, l.ret.end, t);
    }
    default:
        return own;
    }
}

Studio::Studio(SceneDetail) {
    static_.meshes = buildMeshes();
    static_.materials = buildMaterials(studioRig(show::Effect::Return));
    std::array<rt::RectLight, kLightCount> lights{};
    std::uint32_t townInstance = 0;

    auto add = [&](std::uint32_t mesh, std::uint32_t material, const rt::Mat34& transform,
                   std::uint32_t light = rt::kNoLight) {
        instances_.push_back({mesh, material, transform, light});
    };
    auto block = [&](std::uint32_t material, Vec3 min, Vec3 max) {
        add(kUnitBox, material, place((min + max) * 0.5f, max - min));
    };

    // --- The room: floor, ceiling, four walls (the back one, concrete, around the window), a slim dark
    // skirting.
    block(kFloorTiles, {kRoomLeft - kWall, -kWall, kRoomBack - kWall},
          {kRoomRight + kWall, 0.f, kRoomFront + kWall});
    block(kCeilingPaint, {kRoomLeft - kWall, kRoomHeight, kRoomBack - kWall},
          {kRoomRight + kWall, kRoomHeight + kWall, kRoomFront + kWall});
    block(kWallPaint, {kRoomLeft - kWall, 0.f, kRoomBack}, {kRoomLeft, kRoomHeight, kRoomFront});
    block(kWallPaint, {kRoomRight, 0.f, kRoomBack}, {kRoomRight + kWall, kRoomHeight, kRoomFront});
    block(kWallPaint, {kRoomLeft, 0.f, kRoomFront}, {kRoomRight, kRoomHeight, kRoomFront + kWall});
    const float back0 = kRoomBack - kWall, back1 = kRoomBack;
    block(kConcreteWall, {kRoomLeft, 0.f, back0}, {kWindowLeft, kRoomHeight, back1});
    block(kConcreteWall, {kWindowRight, 0.f, back0}, {kRoomRight, kRoomHeight, back1});
    block(kConcreteWall, {kWindowLeft, 0.f, back0}, {kWindowRight, kWindowBottom, back1});
    block(kConcreteWall, {kWindowLeft, kWindowTop, back0}, {kWindowRight, kRoomHeight, back1});
    const float skirt = 0.04f, skirtDepth = 0.008f;
    block(kSkirting, {kRoomLeft, 0.f, back1}, {kRoomRight, skirt, back1 + skirtDepth});
    block(kSkirting, {kRoomLeft, 0.f, back1}, {kRoomLeft + skirtDepth, skirt, kRoomFront});
    block(kSkirting, {kRoomRight - skirtDepth, 0.f, back1}, {kRoomRight, skirt, kRoomFront});

    // --- The window: a slim black frame in the opening with a cross of glazing bars, a sill; the portal
    // fills it.
    {
        const float f = 0.03f, z0 = back0 + 0.03f, z1 = back0 + 0.08f;
        block(kWindowFrame, {kWindowLeft, kWindowBottom, z0}, {kWindowLeft + f, kWindowTop, z1});
        block(kWindowFrame, {kWindowRight - f, kWindowBottom, z0}, {kWindowRight, kWindowTop, z1});
        block(kWindowFrame, {kWindowLeft, kWindowBottom, z0}, {kWindowRight, kWindowBottom + f, z1});
        block(kWindowFrame, {kWindowLeft, kWindowTop - f, z0}, {kWindowRight, kWindowTop, z1});
        // The mullion 40 % across (the moon's ray through the window's centre passes a pane), the transom
        // 62 % up.
        const float mx = kWindowLeft + 0.4f * (kWindowRight - kWindowLeft),
                    my = kWindowBottom + 0.62f * (kWindowTop - kWindowBottom);
        const float bar = 0.012f;
        block(kWindowFrame, {mx - bar, kWindowBottom, z0 + 0.01f}, {mx + bar, kWindowTop, z1 - 0.01f});
        block(kWindowFrame, {kWindowLeft, my - bar, z0 + 0.01f}, {kWindowRight, my + bar, z1 - 0.01f});
        block(kConcreteWall, {kWindowLeft - 0.04f, kWindowBottom - 0.025f, back0 + 0.02f},
              {kWindowRight + 0.04f, kWindowBottom, back1 + 0.05f});
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
        // The town's glow, facing in through the window towards the right wall (not behind the screen).
        const rt::Mat34 town =
            compose(translate(kTownAt), rotationFromTo({0, 0, 1}, normalize(kTownAim - kTownAt)));
        add(kTownMesh, kTownGlow, town, kTownLight);
        townInstance = static_cast<std::uint32_t>(instances_.size() - 1);
        behindHidden_.push_back(townInstance); // behind the screen the room is not lit by it
        lights[kTownLight] = rectLightForQuad(town, kTownWidth, kTownHeight, rt::LightKind::Emitter,
                                              emitted(static_.materials[kTownGlow]), kTownWeight);
        lights[kTownLight].gate = kWindowLight;
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
    behindHidden_.push_back(static_cast<std::uint32_t>(instances_.size() - 1)); // the foot
    add(kMonitorFront, kBeige, translate({0.f, bodyY, bodyZ}));
    behindHidden_.push_back(static_cast<std::uint32_t>(instances_.size() - 1));
    add(kMonitorRear, kBeige, translate({0.f, bodyY + 0.01f, bodyZ - kMonitorHalf[2] - 0.08f}));
    behindHidden_.push_back(static_cast<std::uint32_t>(instances_.size() - 1));
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
    screenInstance_ = static_cast<std::uint32_t>(instances_.size() - 1);
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
        const Vec3 chrome = kChromeAt;
        add(kCylinder, kStandWood, place(chrome + Vec3{0, 0.006f, 0}, {0.028f, 0.012f, 0.028f}));
        add(kSphere, kChrome,
            place(chrome + Vec3{0, 0.012f + kChromeRadius - 0.006f, 0},
                  {kChromeRadius, kChromeRadius, kChromeRadius}));
        const Vec3 glass = kGlassAt;
        add(kCylinder, kStandWood, place(glass + Vec3{0, 0.005f, 0}, {0.026f, 0.01f, 0.026f}));
        add(kSphere, kGlass,
            place(glass + Vec3{0, 0.01f + kGlassRadius - 0.007f, 0},
                  {kGlassRadius, kGlassRadius, kGlassRadius}));
    }

    // --- The skyline beyond the window: dark towers 300 to 900 m out over the town, across the directions
    // the cameras see through the window, each turned to face the room, with a few lit windows and floors. A
    // fixed hash places them (deterministic).
    {
        std::uint32_t state = 0x5EEDu;
        const auto next = [&] {
            state = state * 1664525u + 1013904223u;
            return static_cast<float>(state >> 8) / 16777216.f;
        };
        const Vec3 window{0.5f * (kWindowLeft + kWindowRight), 0.f, kRoomBack};
        constexpr int kTowers = 56;
        for (int i = 0; i < kTowers; ++i) {
            // a: the bearing from straight back (-z), negative to the left (-x).
            const float a = -1.5f + 2.6f * (static_cast<float>(i) + 0.15f + 0.7f * next()) / kTowers;
            const float distance = 300.f + 600.f * next();
            const Vec3 at = window + Vec3{std::sin(a), 0.f, -std::cos(a)} * distance;
            const float width = 8.f + 16.f * next(), depth = 10.f + 16.f * next();
            const float top = -25.f + 60.f * next() * next(); // relative to the room's floor
            constexpr float kBase = -120.f;
            const rt::Mat34 tower = compose(translate({at.x, 0.f, at.z}), rotate({0, 1, 0}, -a));
            add(kUnitBox, kBuilding,
                compose(tower, place({0.f, 0.5f * (kBase + top), 0.f}, {width, top - kBase, depth})));
            const int lit = 2 + static_cast<int>(5.f * next());
            for (int k = 0; k < lit; ++k) {
                const float y = top - 2.f - 40.f * next();
                const bool strip = next() < 0.4f;
                const float w = strip ? width * (0.3f + 0.5f * next()) : 1.6f;
                const float x = (0.5f * width - 0.5f * w - 0.6f) * (2.f * next() - 1.f);
                add(kUnitBox, kCityWindow,
                    compose(tower, place({x, y, 0.5f * depth + 0.05f}, {w, 1.3f, 0.1f})));
            }
        }
    }

    // --- An office chair pushed back from the desk into the moonbeam (the wide shots' foreground
    // silhouette): a five-star base on casters, a gas lift, the seat and the backrest in dark fabric.
    {
        const rt::Mat34 chair = compose(translate({-0.95f, 0.f, 0.85f}), rotate({0, 1, 0}, -0.6f));
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
        add(kShade, kLampPaint, shade);
        add(kShadeInner, kShadeInside, shade);
        add(kSphere, kLampPaint, compose(head, scale({0.016f, 0.016f, 0.016f})));
        const rt::Mat34 emitter =
            compose(shade, compose(translate({0.f, kLampRecess, 0.f}), rotate({1, 0, 0}, 0.5f * kPi)));
        add(kEmitter, kLampEmitter, emitter, kLampLight);
        lights[kLampLight] =
            rectLightForQuad(emitter, kLampEmitterSize, kLampEmitterSize, rt::LightKind::Emitter,
                             emitted(static_.materials[kLampEmitter]), kLampWeight);
    }

    lights_.assign(lights.begin(), lights.end());
    buildBehindScreen();
}

// --- Behind the screen -------------------------------------------------------------------------------------

namespace {

// The dark box behind the screen, open only through the picture: wide enough that no camera inside sees
// past it, clear of the lamp's arm and the moonbeam on the left and of the case below.
constexpr float kBoxLeft = -0.38f, kBoxRight = 0.38f, kBoxBottom = 0.905f, kBoxTop = 1.4f;
constexpr float kBoxBack = kRoomBack - 0.005f, kBoxFront = kMonitorFrontZ - 0.0005f, kBoxWall = 0.01f;

// FLYBY's starfield (retro/flyby.cpp), mirrored here: the 1993 stars in their own view space, a camera at
// the origin looking along +z with a 42 degree vertical field of view on the 320 x 200 screen at a pixel
// aspect of 1.2; each star keeps its x and y while it streams from z = 10 towards the camera at 4 units a
// second. Behind the glass the 1993 camera's origin is the screen's centre and a unit is kStarUnit metres, so
// the camera crosses the glass where the 1993 camera sat, and the stars stand where the 1993 frame drew them
// on D.
constexpr double kStarFar = 10.0, kStarNear = 0.6, kStarSpeed = 4.0, kStarExit = 1.42;
constexpr std::uint32_t kStarSeed = 1993;
constexpr float kStarUnit = 0.045f;
constexpr std::uint32_t kStarRuns = 4;

double unitHash(std::uint32_t h) {
    return static_cast<double>(h) / 4294967296.0;
}

} // namespace

bool studioBehindScreen(const Camera& camera) {
    const Vec3 e = camera.eye;
    return e.z < kScreenZ && e.z > kBoxBack && e.x > kBoxLeft && e.x < kBoxRight && e.y > kBoxBottom &&
           e.y < kBoxTop;
}

void Studio::buildBehindScreen() {
    behindInstances_.clear();
    for (std::uint32_t i = 0; i < instances_.size(); ++i) {
        if (std::find(behindHidden_.begin(), behindHidden_.end(), i) != behindHidden_.end())
            continue;
        SceneInstance in = instances_[i];
        if (i == screenInstance_) {
            // Thin glass, and no light: seen from the room it shows nothing the screen's light does not.
            in.material = kScreenBack;
            in.light = rt::kNoLight;
        }
        behindInstances_.push_back(in);
    }
    const auto block = [&](Vec3 min, Vec3 max) {
        behindInstances_.push_back({kUnitBox, kVoid, place((min + max) * 0.5f, max - min)});
    };
    const float w = kBoxWall;
    block({kBoxLeft - w, kBoxBottom - w, kBoxBack - w}, {kBoxRight + w, kBoxTop + w, kBoxBack});
    block({kBoxLeft - w, kBoxBottom - w, kBoxBack}, {kBoxLeft, kBoxTop + w, kBoxFront});
    block({kBoxRight, kBoxBottom - w, kBoxBack}, {kBoxRight + w, kBoxTop + w, kBoxFront});
    block({kBoxLeft, kBoxBottom - w, kBoxBack}, {kBoxRight, kBoxBottom, kBoxFront});
    block({kBoxLeft, kBoxTop, kBoxBack}, {kBoxRight, kBoxTop + w, kBoxFront});
    // The front, round the picture.
    const float l = -0.5f * kScreenWidth, r = 0.5f * kScreenWidth;
    const float b = kScreenCentreY - 0.5f * kScreenHeight, t = kScreenCentreY + 0.5f * kScreenHeight;
    const float f0 = kBoxFront - 0.002f, f1 = kBoxFront;
    block({kBoxLeft, kBoxBottom, f0}, {l, kBoxTop, f1});
    block({r, kBoxBottom, f0}, {kBoxRight, kBoxTop, f1});
    block({l, kBoxBottom, f0}, {r, b, f1});
    block({l, t, f0}, {r, kBoxTop, f1});

    // The stars where FLYBY's table has them on PORTAL's D, and as each would stand at the same depth on its
    // next kStarRuns - 1 runs (each run takes a new direction): 2026 shows four times the 1993 field. Less
    // those the camera's way in and out passes within 1.5 cm of.
    const Landmarks& marks = landmarks();
    const double travelled = kStarSpeed * (marks.portal.downbeat - marks.portal.start);
    const double focalY = 100.0 / std::tan(21.0 * std::numbers::pi / 180.0);
    const double focalX = focalY * 1.2;
    std::vector<Vec3> way;
    for (int k = 0; k <= 200; ++k) {
        const double t = marks.portal.downbeat - 0.2 + (2 * marks.portal.bar + 0.4) * k / 200.0;
        way.push_back(alongPath(cachedKeys(kPortalPath), t).eye);
    }
    for (std::uint32_t k = 0; k < kStarCount * kStarRuns; ++k) {
        const std::uint32_t i = k % kStarCount, run0 = k / kStarCount;
        const std::uint32_t seed = hash32(kStarSeed * 7919u + i * 2u);
        const double r0 = 0.03 + 0.97 * unitHash(seed);
        const double end = std::max(kStarNear, r0 * kStarFar / kStarExit);
        const double run = kStarFar - end;
        const double along = unitHash(hash32(seed + 1u)) * run + travelled;
        const auto now = static_cast<std::uint32_t>(std::floor(along / run));
        const double z = kStarFar - (along - static_cast<double>(now) * run);
        const std::uint32_t cycle = now + run0;
        const double angle = 2 * std::numbers::pi * unitHash(hash32(seed ^ (cycle * 0x632be5abu + 0x51u)));
        const double x = r0 * std::cos(angle) * kStarFar * 160.0 / focalX;
        const double y = r0 * std::sin(angle) * kStarFar * 100.0 / focalY;
        const Vec3 centre =
            screenCentre() +
            Vec3{static_cast<float>(x), static_cast<float>(y), -static_cast<float>(z)} * kStarUnit;
        bool clear = true;
        for (const Vec3& p : way)
            clear = clear && length(centre - p) > 0.015f;
        if (!clear)
            continue;
        // A little variety in size and colour: blue-white to warm white, as the 1993 ramp's top (240, 240,
        // 255) is the bluest.
        const auto u = static_cast<float>(unitHash(hash32(seed + 7u + 131u * run0)));
        const auto v = static_cast<float>(unitHash(hash32(seed + 11u + 131u * run0)));
        rt::SphereRecord star{};
        star.center[0] = centre.x;
        star.center[1] = centre.y;
        star.center[2] = centre.z;
        star.prevCenter[0] = centre.x;
        star.prevCenter[1] = centre.y;
        star.prevCenter[2] = centre.z;
        star.radius = 0.0004f + 0.0006f * u * u;
        setColor(star.color, Vec3{0.85f + 0.15f * v, 0.9f, 1.f - 0.18f * v});
        star.emission = 6.f + 10.f * (1.f - u);
        star.material = kStar;
        static_.spheres.push_back(star);
    }
    static_.capacity.spheres = static_cast<std::uint32_t>(static_.spheres.size());
    SceneInstance stars{0, kStar, identity()};
    stars.kind = rt::GeometryKind::Spheres;
    stars.firstSphere = 0;
    stars.sphereCount = static_cast<std::uint32_t>(static_.spheres.size());
    behindInstances_.push_back(stars);
}

SceneFrame Studio::frame(const show::TimelineFrame& frame) const {
    SceneFrame f;
    f.camera = studioCamera(frame);
    f.instances = studioBehindScreen(f.camera) ? behindInstances_ : instances_;
    f.lights = lights_;
    f.exposureEV = studioExposureAt(frame);
    f.clampIndirect = kClampIndirect;
    // The frame's light rig: its material table, and the lights' emissions from it.
    const StudioRig rig = studioRigAt(frame);
    f.materials = buildMaterials(rig);
    f.lights[kScreenLight].emission[0] = f.lights[kScreenLight].emission[1] =
        f.lights[kScreenLight].emission[2] = f.materials[kScreenGlass].emissionScale;
    setColor(f.lights[kLampLight].emission, emitted(f.materials[kLampEmitter]));
    if (frame.effect == show::Effect::Return || frame.effect == show::Effect::Reboot) {
        // Dawn: the moon has set.
        f.materials[kMoonGlow].emissionScale = 0.f;
        setColor(f.lights[kMoonLight].emission, Vec3{0.f, 0.f, 0.f});
    }
    f.environment = studioEnvironmentAt(frame);
    return f;
}

// --- The epoch cards
// ----------------------------------------------------------------------------------------------

std::string Studio::nowCard(show::Effect effect) const {
    const std::string year = "2026";
    switch (effect) {
    case show::Effect::Finale:
        return year + kCardDot + "EVERY SCENE" + kCardDot + "ONE PATH TRACER";
    case show::Effect::Portal:
        return year + kCardDot + "OUT OF THE SCREEN";
    case show::Effect::StudioNight:
        return year + kCardDot + "THE STUDIO" + kCardDot + groupThousands(instances_.size()) + " INSTANCES" +
               kCardDot + "LIT BY ONE SCREEN AND ONE MOON";
    case show::Effect::Return:
        return year + kCardDot + "RETURN" + kCardDot + "THE STUDIO AT DAWN";
    case show::Effect::Reboot:
        return year + kCardDot + "THROUGH THE GLASS";
    default:
        return {};
    }
}

} // namespace evo::modern
