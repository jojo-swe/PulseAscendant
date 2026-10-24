// Small scenes for the path tracer tests, built in code: an emitter quad, a Cornell box, a furnace sphere, a
// floor under one rectangular light (an analytic case), a plane for the guides, a retro screen over a floor
// and a stress scene of the cases that tend to produce NaNs and fireflies.

#pragma once

#include "modern/math.hpp"
#include "modern/mesh_builder.hpp"
#include "modern/scene.hpp"
#include "rt/gpu_types.hpp"

#include <numbers>
#include <vector>

namespace evo::test {

using modern::Vec3;

struct TestScene {
    modern::SceneStatic statics;
    modern::SceneFrame frame;
};

inline float radians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.f;
}

// translate(t) * rotate(axis, degrees) * scale(s).
inline rt::Mat34 place(Vec3 t, Vec3 axis, float degrees, Vec3 s) {
    return modern::compose(modern::translate(t),
                           modern::compose(modern::rotate(axis, radians(degrees)), modern::scale(s)));
}

const Vec3 kX{1, 0, 0}, kY{0, 1, 0}, kZ{0, 0, 1};

// A diffuse (Lambert, no specular: IOR 1) material.
inline rt::Material lambert(Vec3 color) {
    rt::Material m;
    m.baseColor[0] = color.x;
    m.baseColor[1] = color.y;
    m.baseColor[2] = color.z;
    m.ior = 1.f;
    m.roughness = 1.f;
    return m;
}

// An emitter that reflects nothing (black Lambert), class Emissive.
inline rt::Material emitter(Vec3 emission, float scale = 1.f) {
    rt::Material m = lambert({0, 0, 0});
    m.emission[0] = emission.x;
    m.emission[1] = emission.y;
    m.emission[2] = emission.z;
    m.emissionScale = scale;
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Emissive);
    return m;
}

inline rt::Material metal(Vec3 color, float roughness) {
    rt::Material m;
    m.baseColor[0] = color.x;
    m.baseColor[1] = color.y;
    m.baseColor[2] = color.z;
    m.metallic = 1.f;
    m.roughness = roughness;
    return m;
}

inline rt::Material glass(float ior, float roughness) {
    rt::Material m;
    m.baseColor[0] = m.baseColor[1] = m.baseColor[2] = 1.f;
    m.ior = ior;
    m.roughness = roughness;
    m.transmission = 1.f;
    m.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Glass);
    return m;
}

// The mesh indices every scene below uses.
enum : std::uint32_t { kQuadMesh = 0, kCubeMesh = 1, kSphereMesh = 2 };

inline std::vector<rt::Mesh> basicMeshes() {
    return {modern::quad(1.f, 1.f), modern::box({0.5f, 0.5f, 0.5f}), modern::uvSphere(1.f, 16, 32)};
}

// (a) A 1 x 1 emitter quad of emission 2 at 2 m, facing the camera; the light it is.
inline TestScene emitterQuadScene() {
    TestScene s;
    s.statics.meshes = basicMeshes();
    s.statics.materials = {emitter({2, 2, 2})};
    const rt::Mat34 t = place({0, 0, -2}, kY, 0, {1, 1, 1});
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 40.f};
    s.frame.instances = {{kQuadMesh, 0, t, 0}};
    s.frame.lights = {modern::rectLightForQuad(t, 1, 1, rt::LightKind::Emitter, {2, 2, 2}, 1.f)};
    return s;
}

// (b) A closed Cornell box, x and z in [-1, 1], y in [0, 2]: 0.8 white floor, ceiling, back and front walls,
// red left and green right walls, a white block and a rough metal block, and a 0.5 x 0.5 ceiling light of
// emission 12. The camera stands inside, just in front of the front wall.
inline TestScene cornellBox() {
    TestScene s;
    s.statics.meshes = basicMeshes();
    s.statics.materials = {lambert({0.8f, 0.8f, 0.8f}), lambert({0.8f, 0.1f, 0.1f}),
                           lambert({0.1f, 0.8f, 0.1f}), emitter({12, 12, 12}),
                           metal({0.9f, 0.9f, 0.9f}, 0.35f)};
    const Vec3 wall{2, 2, 1};
    auto& in = s.frame.instances;
    in.push_back({kQuadMesh, 0, place({0, 0, 0}, kX, -90, wall)}); // floor, facing +y
    in.push_back({kQuadMesh, 0, place({0, 2, 0}, kX, 90, wall)});  // ceiling, facing -y
    in.push_back({kQuadMesh, 0, place({0, 1, -1}, kY, 0, wall)});  // back, facing +z
    in.push_back({kQuadMesh, 0, place({0, 1, 1}, kY, 180, wall)}); // front, facing -z
    in.push_back({kQuadMesh, 1, place({-1, 1, 0}, kY, 90, wall)}); // left, red, facing +x
    in.push_back({kQuadMesh, 2, place({1, 1, 0}, kY, -90, wall)}); // right, green, facing -x
    in.push_back({kCubeMesh, 4, place({-0.35f, 0.6f, -0.3f}, kY, 20, {0.55f, 1.2f, 0.55f})});
    in.push_back({kCubeMesh, 0, place({0.4f, 0.3f, 0.25f}, kY, -18, {0.6f, 0.6f, 0.6f})});
    const rt::Mat34 light = place({0, 1.995f, 0}, kX, 90, {0.5f, 0.5f, 1});
    in.push_back({kQuadMesh, 3, light, 0});
    s.frame.lights = {modern::rectLightForQuad(light, 1, 1, rt::LightKind::Emitter, {12, 12, 12}, 1.f)};
    s.frame.camera = {{0, 1, 0.95f}, {0, 1, -1}, {0, 1, 0}, 70.f};
    return s;
}

// (c) The camera at the centre of a closed unit sphere whose surface is 0.5 diffuse with emission 1 (no
// light: it emits on both sides). Every path gains exactly 0.5^k at its k-th bounce.
inline TestScene furnaceScene() {
    TestScene s;
    s.statics.meshes = basicMeshes();
    rt::Material m = lambert({0.5f, 0.5f, 0.5f});
    m.emission[0] = m.emission[1] = m.emission[2] = 1.f;
    s.statics.materials = {m};
    s.frame.instances = {{kSphereMesh, 0, modern::identity()}};
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 60.f};
    return s;
}

// A floor (material 0, given) at y = 0 under a 1 x 1 light of emission kAnalyticEmission at y = 1 facing
// down, all inside a black box (nothing else reflects or emits). The camera looks straight down from y = 0.5.
inline constexpr float kAnalyticEmission = 3.f;
inline constexpr float kAnalyticLightHeight = 1.f;

inline TestScene analyticLightScene(const rt::Material& floor) {
    TestScene s;
    s.statics.meshes = basicMeshes();
    s.statics.materials = {floor, lambert({0, 0, 0}),
                           emitter({kAnalyticEmission, kAnalyticEmission, kAnalyticEmission})};
    const rt::Mat34 light = place({0, kAnalyticLightHeight, 0}, kX, 90, {1, 1, 1});
    s.frame.instances = {{kCubeMesh, 1, place({0, 1, 0}, kY, 0, {5, 3, 5})},
                         {kQuadMesh, 0, place({0, 0, 0}, kX, -90, {4, 4, 1})},
                         {kQuadMesh, 2, light, 0}};
    s.frame.lights = {modern::rectLightForQuad(
        light, 1, 1, rt::LightKind::Emitter, {kAnalyticEmission, kAnalyticEmission, kAnalyticEmission}, 1.f)};
    s.frame.camera = {{0, 0.5f, 0}, {0, 0, 0}, {0, 0, -1}, 60.f};
    return s;
}

// The analytic scene with two lights instead of one: a 0.8 x 0.8 light of emission kTwoLightEmission[0] and a
// 0.6 x 1 light of emission kTwoLightEmission[1] at y = 1, selected 1 : 3 (a wrong selection probability on
// either side of MIS biases the floor).
inline constexpr float kTwoLightEmission[2] = {4.f, 1.5f};

inline TestScene twoLightScene() {
    TestScene s;
    s.statics.meshes = basicMeshes();
    const float e0 = kTwoLightEmission[0], e1 = kTwoLightEmission[1];
    s.statics.materials = {lambert({0.5f, 0.5f, 0.5f}), lambert({0, 0, 0}), emitter({e0, e0, e0}),
                           emitter({e1, e1, e1})};
    const rt::Mat34 a = place({-0.6f, kAnalyticLightHeight, 0}, kX, 90, {0.8f, 0.8f, 1});
    const rt::Mat34 b = place({0.7f, kAnalyticLightHeight, 0.2f}, kX, 90, {0.6f, 1, 1});
    s.frame.instances = {{kCubeMesh, 1, place({0, 1, 0}, kY, 0, {5, 3, 5})},
                         {kQuadMesh, 0, place({0, 0, 0}, kX, -90, {4, 4, 1})},
                         {kQuadMesh, 2, a, 0},
                         {kQuadMesh, 3, b, 1}};
    s.frame.lights = {modern::rectLightForQuad(a, 1, 1, rt::LightKind::Emitter, {e0, e0, e0}, 1.f),
                      modern::rectLightForQuad(b, 1, 1, rt::LightKind::Emitter, {e1, e1, e1}, 3.f)};
    s.frame.camera = {{0, 0.5f, 0}, {0, 0, 0}, {0, 0, -1}, 60.f};
    return s;
}

// A black box, x in [-1, 2], y in [-3, 1.5], z in [-2, 2], with a window: a hole in its x = 2 wall, y in
// [kWindowBottom, kWindowTop], z in [-kWindowHalfWidth, kWindowHalfWidth], and a portal light in it. Inside,
// a 0.5 diffuse panel at x = 0 faces the window (+x); the camera at x = 0.8 looks at the panel. Every
// direction from the panel's visible part through the window points below the horizon, where the sky is
// smooth (dark ground and the city's glow, no stars and no moon), so a few samples per pixel converge.
// stacked: a second, larger portal 0.4 m behind the first that every ray through the window crosses too (the
// two are selected 1 : 1); the light reaching the panel is the same.
inline constexpr float kWindowBottom = -2.2f, kWindowTop = -0.8f, kWindowHalfWidth = 0.8f;
inline constexpr float kWindowX = 2.f;
inline constexpr float kPanelAlbedo = 0.5f;

inline TestScene windowScene(bool stacked) {
    TestScene s;
    s.statics.meshes = basicMeshes();
    s.statics.materials = {lambert({kPanelAlbedo, kPanelAlbedo, kPanelAlbedo}), lambert({0, 0, 0})};
    auto& in = s.frame.instances;
    in.push_back({kQuadMesh, 0, place({0, 0, 0}, kY, 90, {1.2f, 1.2f, 1})}); // the panel, facing +x
    // Black walls, each a little larger than its face so that no crack lets light in at the edges.
    auto wallX = [&](float x, float y0, float y1, float z0, float z1) { // in the plane x
        in.push_back(
            {kQuadMesh, 1, place({x, 0.5f * (y0 + y1), 0.5f * (z0 + z1)}, kY, 90, {z1 - z0, y1 - y0, 1})});
    };
    auto wallY = [&](float y, float x0, float x1, float z0, float z1) {
        in.push_back(
            {kQuadMesh, 1, place({0.5f * (x0 + x1), y, 0.5f * (z0 + z1)}, kX, 90, {x1 - x0, z1 - z0, 1})});
    };
    auto wallZ = [&](float z, float x0, float x1, float y0, float y1) {
        in.push_back(
            {kQuadMesh, 1, place({0.5f * (x0 + x1), 0.5f * (y0 + y1), z}, kY, 0, {x1 - x0, y1 - y0, 1})});
    };
    const float m = 0.05f; // overlap
    wallX(-1, -3 - m, 1.5f + m, -2 - m, 2 + m);
    wallY(1.5f, -1 - m, kWindowX + m, -2 - m, 2 + m);
    wallY(-3, -1 - m, kWindowX + m, -2 - m, 2 + m);
    wallZ(-2, -1 - m, kWindowX + m, -3 - m, 1.5f + m);
    wallZ(2, -1 - m, kWindowX + m, -3 - m, 1.5f + m);
    // The window wall: four strips around the hole.
    wallX(kWindowX, kWindowTop, 1.5f + m, -2 - m, 2 + m);
    wallX(kWindowX, -3 - m, kWindowBottom, -2 - m, 2 + m);
    wallX(kWindowX, kWindowBottom - m, kWindowTop + m, -2 - m, -kWindowHalfWidth);
    wallX(kWindowX, kWindowBottom - m, kWindowTop + m, kWindowHalfWidth, 2 + m);
    // The portal: the hole, facing into the box (-x).
    const float h = kWindowTop - kWindowBottom, w = 2 * kWindowHalfWidth;
    s.frame.lights.push_back(modern::rectLightForQuad(
        place({kWindowX, 0.5f * (kWindowBottom + kWindowTop), 0}, kY, -90, {w, h, 1}), 1, 1,
        rt::LightKind::Portal, {0, 0, 0}, 1.f));
    if (stacked)
        s.frame.lights.push_back(
            modern::rectLightForQuad(place({kWindowX + 0.4f, -1.65f, 0}, kY, -90, {2.2f, 2.3f, 1}), 1, 1,
                                     rt::LightKind::Portal, {0, 0, 0}, 1.f));
    s.frame.camera = {{0.8f, 0, 0}, {0, 0, 0}, {0, 1, 0}, 60.f};
    return s;
}

// (g) A 20 x 20 plane at z = -3 facing the camera at the origin (roughness 0.6).
inline constexpr float kPlaneRoughness = 0.6f;

inline TestScene planeScene() {
    TestScene s;
    s.statics.meshes = basicMeshes();
    rt::Material m = lambert({0.5f, 0.5f, 0.5f});
    m.roughness = kPlaneRoughness;
    s.statics.materials = {m};
    s.frame.instances = {{kQuadMesh, 0, place({0, 0, -3}, kY, 0, {20, 20, 1})}};
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 40.f};
    return s;
}

// (h) A 0.8 x 0.6 retro screen (emissionScale kScreenScale, the RetroScreen light) standing on a white floor
// inside a black box; the camera looks down at both.
inline constexpr float kScreenScale = 2.f;
inline const Vec3 kScreenCentre{0, 0.4f, -1.5f};
inline const Vec3 kFloorPatch{0, 0, -0.9f};
// Floor points in front of the screen's left and right edges and just in front of its bottom edge.
inline const Vec3 kFloorLeft{-0.3f, 0, -1.2f}, kFloorRight{0.3f, 0, -1.2f}, kFloorNear{0, 0, -1.35f};

inline TestScene retroScreenScene() {
    TestScene s;
    s.statics.meshes = basicMeshes();
    rt::Material screen = emitter({0, 0, 0}, kScreenScale);
    screen.texture = rt::kRetroScreenTexture;
    s.statics.materials = {lambert({0.8f, 0.8f, 0.8f}), screen, lambert({0, 0, 0})};
    const rt::Mat34 t = place(kScreenCentre, kY, 0, {0.8f, 0.6f, 1});
    s.frame.instances = {{kCubeMesh, 2, place({0, 1, 0}, kY, 0, {8, 4, 8})},
                         {kQuadMesh, 0, place({0, 0, 0}, kX, -90, {6, 6, 1})},
                         {kQuadMesh, 1, t, 0}};
    s.frame.lights = {modern::rectLightForQuad(t, 1, 1, rt::LightKind::RetroScreen,
                                               {kScreenScale, kScreenScale, kScreenScale}, 1.f)};
    s.frame.camera = {{0, 0.9f, 0.6f}, {0, 0.2f, -1.5f}, {0, 1, 0}, 50.f};
    return s;
}

// (e) Review Focus 8: a camera grazing a huge checker floor, mirror and all-delta coated spheres, IOR 1.0001
// glass (smooth and rough), a light shut inside a box, an emitter of emission 1e4, a light with select weight
// 0, a portal, a retro screen and a mesh with a zero-area triangle and zero vertex normals.
inline TestScene stressScene() {
    TestScene s;
    s.statics.meshes = basicMeshes();
    rt::Mesh degenerate;
    degenerate.vertices = {{{0, 0, 0}, {0, 0, 0}, {0, 0}},
                           {{1, 0, 0}, {0, 0, 0}, {1, 0}},
                           {{2, 0, 0}, {0, 0, 0}, {1, 1}},
                           {{0, 1, 0}, {0, 0, 0}, {0, 1}}};
    degenerate.indices = {0, 1, 2, 0, 1, 3}; // the first triangle has no area
    s.statics.meshes.push_back(degenerate);
    const std::uint32_t kDegenerateMesh = 3;

    rt::Material checker = lambert({0.9f, 0.9f, 0.9f});
    checker.ior = 1.5f;
    checker.roughness = 0.35f;
    checker.baseColor2[0] = checker.baseColor2[1] = checker.baseColor2[2] = 0.05f;
    checker.patternScale = 0.3f;
    checker.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Checker);
    rt::Material coated = metal({1, 1, 1}, 0.f);
    coated.coat = 1.f;
    coated.coatRoughness = 0.f;
    rt::Material wood = lambert({0.5f, 0.3f, 0.15f});
    wood.pattern = static_cast<std::uint32_t>(rt::Pattern::WoodPlanks);
    wood.baseColor2[0] = 0.3f;
    wood.baseColor2[1] = 0.15f;
    wood.baseColor2[2] = 0.07f;
    wood.coat = 0.6f;
    wood.coatRoughness = 0.15f;
    rt::Material brushed = metal({0.8f, 0.8f, 0.8f}, 0.3f);
    brushed.pattern = static_cast<std::uint32_t>(rt::Pattern::BrushedMetal);
    rt::Material screen = emitter({0, 0, 0}, 5.f);
    screen.texture = rt::kRetroScreenTexture;
    screen.coat = 1.f;
    screen.coatRoughness = 0.03f;
    s.statics.materials = {checker,
                           metal({1, 1, 1}, 0.f),
                           glass(1.0001f, 0.f),
                           glass(1.0001f, 0.3f),
                           glass(1.5f, 0.f),
                           coated,
                           emitter({1e4f, 1e4f, 1e4f}),
                           lambert({1, 1, 1}),
                           wood,
                           brushed,
                           screen,
                           glass(1.5f, 0.5f)};

    auto& in = s.frame.instances;
    auto& lights = s.frame.lights;
    in.push_back({kQuadMesh, 0, place({0, 0, -50}, kX, -90, {200, 200, 1})});
    const std::uint32_t sphereMaterials[] = {1, 2, 3, 4, 5, 11, 8, 9};
    for (std::uint32_t i = 0; i < 8; ++i)
        in.push_back(
            {kSphereMesh, sphereMaterials[i],
             place({-1.6f + 0.45f * float(i), 0.2f, -2.f - 0.7f * float(i)}, kY, 0, {0.2f, 0.2f, 0.2f})});
    // A light inside a closed box.
    in.push_back({kCubeMesh, 7, place({1.5f, 0.5f, -4}, kY, 30, {1, 1, 1})});
    const rt::Mat34 boxed = place({1.5f, 0.5f, -4}, kX, 90, {0.3f, 0.3f, 1});
    in.push_back({kQuadMesh, 6, boxed, static_cast<std::uint32_t>(lights.size())});
    lights.push_back(modern::rectLightForQuad(boxed, 1, 1, rt::LightKind::Emitter, {1e4f, 1e4f, 1e4f}, 1.f));
    // A very bright emitter in the open, seen at a grazing angle.
    const rt::Mat34 bright = place({-1, 0.05f, -6}, kX, -89.9f, {0.5f, 0.5f, 1});
    in.push_back({kQuadMesh, 6, bright, static_cast<std::uint32_t>(lights.size())});
    lights.push_back(modern::rectLightForQuad(bright, 1, 1, rt::LightKind::Emitter, {1e4f, 1e4f, 1e4f}, 1.f));
    // A light that is never selected.
    const rt::Mat34 unweighted = place({0.5f, 1.f, -3}, kX, 90, {0.4f, 0.4f, 1});
    in.push_back({kQuadMesh, 6, unweighted, static_cast<std::uint32_t>(lights.size())});
    lights.push_back(
        modern::rectLightForQuad(unweighted, 1, 1, rt::LightKind::Emitter, {1e4f, 1e4f, 1e4f}, 0.f));
    // A portal facing down onto the floor (no geometry: rays through it see the sky).
    lights.push_back(modern::rectLightForQuad(place({0, 3, -3}, kX, 90, {2, 2, 1}), 1, 1,
                                              rt::LightKind::Portal, {0, 0, 0}, 0.5f));
    // A retro screen.
    const rt::Mat34 screenT = place({0.9f, 0.3f, -2.5f}, kY, -30, {0.4f, 0.3f, 1});
    in.push_back({kQuadMesh, 10, screenT, static_cast<std::uint32_t>(lights.size())});
    lights.push_back(modern::rectLightForQuad(screenT, 1, 1, rt::LightKind::RetroScreen, {5, 5, 5}, 1.f));
    in.push_back({kDegenerateMesh, 7, place({-0.6f, 0.1f, -1.5f}, kY, 10, {0.3f, 0.3f, 0.3f})});

    s.frame.camera = {{0, 0.002f, 0}, {0, 0.001f, -50}, {0, 1, 0}, 30.f};
    return s;
}

// A scene to look at (path_tracer_test --look): the Cornell box, open at the front and seen from outside,
// with a checker floor, a glass sphere, a chrome sphere and a coated wooden block.
inline TestScene lookScene() {
    TestScene s = cornellBox();
    rt::Material checker = lambert({0.85f, 0.85f, 0.8f});
    checker.ior = 1.5f;
    checker.roughness = 0.35f;
    checker.baseColor2[0] = checker.baseColor2[1] = checker.baseColor2[2] = 0.04f;
    checker.patternScale = 0.25f;
    checker.materialClass = static_cast<std::uint32_t>(rt::MaterialClass::Checker);
    rt::Material wood = lambert({0.55f, 0.32f, 0.16f});
    wood.ior = 1.5f;
    wood.pattern = static_cast<std::uint32_t>(rt::Pattern::WoodPlanks);
    wood.baseColor2[0] = 0.32f;
    wood.baseColor2[1] = 0.17f;
    wood.baseColor2[2] = 0.08f;
    wood.patternScale = 0.12f;
    wood.coat = 0.6f;
    wood.coatRoughness = 0.15f;
    s.statics.materials.push_back(checker);               // 5
    s.statics.materials.push_back(glass(1.5f, 0.f));      // 6
    s.statics.materials.push_back(metal({1, 1, 1}, 0.f)); // 7
    s.statics.materials.push_back(wood);                  // 8
    auto& in = s.frame.instances;
    in[0].material = 5;
    in[7].material = 8;
    in.push_back({kSphereMesh, 6, place({0.4f, 0.85f, 0.25f}, kY, 0, {0.25f, 0.25f, 0.25f})});
    in.push_back({kSphereMesh, 7, place({-0.45f, 0.2f, 0.5f}, kY, 0, {0.2f, 0.2f, 0.2f})});
    in.erase(in.begin() + 3); // no front wall: the camera stands outside the open box
    s.frame.camera = {{0, 1, 3.4f}, {0, 0.95f, 0}, {0, 1, 0}, 38.f};
    return s;
}

} // namespace evo::test
