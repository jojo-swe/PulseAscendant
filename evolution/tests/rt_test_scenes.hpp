// Small scenes for the path tracer tests, built in code: an emitter quad, a Cornell box, a furnace sphere, a
// floor under one rectangular light (an analytic case), a plane for the guides, a retro screen over a floor
// and a stress scene of the cases that tend to produce NaNs and fireflies.

#pragma once

#include "modern/math.hpp"
#include "modern/mesh_builder.hpp"
#include "modern/scene.hpp"
#include "rt/gpu_types.hpp"

#include <algorithm>
#include <cmath>
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

// (e) a camera grazing a huge checker floor, mirror and all-delta coated spheres, IOR 1.0001
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

// --- the integrator (integrator_test): regularisation, the clamp, camera-only emitters, dispersion
// ---------

// A quad(1, 1) placed at `centre`, facing `normal` (its +z), `width` along normal x up's direction and
// `height` along the rest (up: +y, or +z for a normal along y). Never mirrors.
inline rt::Mat34 facing(Vec3 centre, Vec3 normal, float width, float height) {
    const Vec3 n = modern::normalize(normal);
    const Vec3 up = std::abs(n.y) > 0.99f ? kZ : kY;
    const Vec3 x = modern::normalize(modern::cross(up, n));
    const Vec3 y = modern::cross(n, x);
    return {{{x.x * width, y.x * height, n.x, centre.x},
             {x.y * width, y.y * height, n.y, centre.y},
             {x.z * width, y.z * height, n.z, centre.z}}};
}

// A black box around everything (inside it nothing reflects or emits; the sky never shows).
inline modern::SceneInstance blackEnclosure(std::uint32_t blackMaterial, Vec3 centre, Vec3 size) {
    return {kCubeMesh, blackMaterial, place(centre, kY, 0, size)};
}

// A triangular glass prism with flat faces: an equilateral cross-section of side 1 in the xz plane, centred
// on the origin, its base face in the plane x = -1 / (2 sqrt 3) and its apex at +x, extruded along y from
// -height / 2 to height / 2. Counter-clockwise from outside.
inline rt::Mesh prismMesh(float height) {
    const float b = 0.5f / std::sqrt(3.f), apex = 1.f / std::sqrt(3.f), h = 0.5f * height;
    const Vec3 a0{apex, -h, 0}, b0{-b, -h, 0.5f}, c0{-b, -h, -0.5f};
    const Vec3 a1{apex, h, 0}, b1{-b, h, 0.5f}, c1{-b, h, -0.5f};
    rt::Mesh mesh;
    // One flat polygon (3 or 4 corners in order around it), wound so that its normal points away from the
    // prism's axis.
    auto face = [&](std::vector<Vec3> corners) {
        Vec3 n = modern::normalize(modern::cross(corners[1] - corners[0], corners[2] - corners[0]));
        Vec3 mid{0, 0, 0};
        for (const Vec3& c : corners)
            mid = mid + c / float(corners.size());
        if (modern::dot(n, Vec3{mid.x, mid.y, mid.z}) < 0) {
            std::reverse(corners.begin(), corners.end());
            n = -n;
        }
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const Vec3& c : corners)
            mesh.vertices.push_back({{c.x, c.y, c.z}, {n.x, n.y, n.z}, {0, 0}});
        for (std::uint32_t i = 1; i + 1 < corners.size(); ++i)
            mesh.indices.insert(mesh.indices.end(), {first, first + i, first + i + 1});
    };
    face({a0, b0, c0});
    face({a1, b1, c1});
    face({a0, a1, b1, b0});
    face({b0, b1, c1, c0});
    face({c0, c1, a1, a0});
    return mesh;
}

// (a) The mirror caustic box: a 0.8 diffuse floor under a soft 2 x 2 ceiling panel (emission 1.5) and a small
// lamp (0.1 x 0.1, emission kCausticLamp) 1.5 m above it, both lights facing down, and a vertical 0.95
// mirror at x = 0.8 facing -x, all in a black box. The floor in front of the mirror is also lit by the
// lamp's mirror image: camera -> floor -> mirror -> lamp, a caustic path NEE cannot reach through a smooth
// mirror, found by BSDF sampling alone as rare samples of 0.76 * kCausticLamp.
inline constexpr float kCausticLamp = 200.f;

inline TestScene causticBox() {
    TestScene s;
    s.statics.meshes = basicMeshes();
    s.statics.materials = {lambert({0.8f, 0.8f, 0.8f}), lambert({0, 0, 0}),
                           emitter({kCausticLamp, kCausticLamp, kCausticLamp}),
                           metal({0.95f, 0.95f, 0.95f}, 0), emitter({1.5f, 1.5f, 1.5f})};
    const rt::Mat34 lamp = facing({0, 1.5f, 0}, {0, -1, 0}, 0.1f, 0.1f);
    const rt::Mat34 panel = facing({-0.5f, 2.4f, 0}, {0, -1, 0}, 2, 2);
    s.frame.instances = {blackEnclosure(1, {0, 1.5f, 0}, {8, 4, 8}),
                         {kQuadMesh, 0, facing({0, 0, 0}, {0, 1, 0}, 4, 4)},
                         {kQuadMesh, 3, facing({0.8f, 0.75f, 0}, {-1, 0, 0}, 3, 1.5f)},
                         {kQuadMesh, 2, lamp, 0},
                         {kQuadMesh, 4, panel, 1}};
    s.frame.lights = {modern::rectLightForQuad(lamp, 1, 1, rt::LightKind::Emitter,
                                               {kCausticLamp, kCausticLamp, kCausticLamp}, 1.f),
                      modern::rectLightForQuad(panel, 1, 1, rt::LightKind::Emitter, {1.5f, 1.5f, 1.5f}, 1.f)};
    s.frame.camera = {{-0.6f, 2.f, 1.6f}, {0.2f, 0, 0}, {0, 1, 0}, 50.f};
    return s;
}

// (b) A chrome sphere (0.95, smooth) of radius 1 at z = -4 in front of the camera, and behind the camera an
// 8 x 8 lamp of emission `lamp` facing it (no light: only what the sphere reflects reaches it), in a black
// box. The sphere's centre pixel sees the lamp after one delta event.
inline TestScene chromeScene(float lamp) {
    TestScene s;
    s.statics.meshes = basicMeshes();
    s.statics.materials = {lambert({0, 0, 0}), metal({0.95f, 0.95f, 0.95f}, 0), emitter({lamp, lamp, lamp})};
    s.frame.instances = {blackEnclosure(0, {0, 0, -1}, {12, 12, 12}),
                         {kSphereMesh, 1, place({0, 0, -4}, kY, 0, {1, 1, 1})},
                         {kQuadMesh, 2, facing({0, 0, 1.5f}, {0, 0, -1}, 8, 8)}};
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 30.f};
    return s;
}

// (b) A clear glass slab (IOR 1.5, 0.3 thick) at z = -2 and behind it a 6 x 6 lamp of emission `lamp`
// facing the camera: the centre pixel sees the lamp after two delta events.
inline TestScene slabLampScene(float lamp) {
    TestScene s;
    s.statics.meshes = basicMeshes();
    s.statics.materials = {lambert({0, 0, 0}), glass(1.5f, 0), emitter({lamp, lamp, lamp})};
    s.frame.instances = {blackEnclosure(0, {0, 0, -2}, {12, 12, 12}),
                         {kCubeMesh, 1, place({0, 0, -2}, kY, 0, {3, 3, 0.3f})},
                         {kQuadMesh, 2, facing({0, 0, -4}, {0, 0, 1}, 6, 6)}};
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 30.f};
    return s;
}

// (c) An LED: a 0.4 x 0.4 quad of camera-only emission `led` (no light) at (0, 1, -4) facing the camera,
// standing over a perfect mirror floor (metal 1, smooth), beside a diffuse desk block lit by a lamp (a
// light) above and in front of it, in a black box. The camera sees the LED directly and its image in the
// floor.
inline const Vec3 kLedCentre{0, 1, -4}, kDeskFront{1.3f, 0.5f, -3.5f};
inline constexpr float kLedSize = 0.4f;

inline TestScene ledScene(float led) {
    TestScene s;
    s.statics.meshes = basicMeshes();
    rt::Material ledMaterial = emitter({led, led, led});
    ledMaterial.flags = rt::kMaterialCameraOnlyEmission;
    s.statics.materials = {lambert({0, 0, 0}), metal({1, 1, 1}, 0), ledMaterial, lambert({0.6f, 0.5f, 0.4f}),
                           emitter({20, 20, 20})};
    const rt::Mat34 lamp = facing({1.3f, 2.5f, -2.5f}, {0, -1, -0.6f}, 0.5f, 0.5f);
    s.frame.instances = {blackEnclosure(0, {0, 2, -3}, {10, 6, 14}),
                         {kQuadMesh, 1, facing({0, 0, -4}, {0, 1, 0}, 8, 12)},
                         {kQuadMesh, 2, facing(kLedCentre, {0, 0, 1}, kLedSize, kLedSize)},
                         {kCubeMesh, 3, place({1.3f, 0.5f, -4}, kY, 0, {1, 1, 1})},
                         {kQuadMesh, 4, lamp, 0}};
    s.frame.lights = {modern::rectLightForQuad(lamp, 1, 1, rt::LightKind::Emitter, {20, 20, 20}, 1.f)};
    s.frame.camera = {{0, 1, 0}, {0, 0.6f, -4}, {0, 1, 0}, 60.f};
    return s;
}

// (d) A flint prism (IOR 1.6, Abbe number `abbe`, smooth) seen against a thin white slit (emission
// kSlitEmission, no light) in a black box. The camera looks into the prism's first face at the angle of
// minimum deviation for IOR 1.6 (the ray inside runs parallel to the base), and the slit stands 8 m beyond
// the second face on that deviated ray: each wavelength sees the slit at its own place.
inline constexpr float kSlitEmission = 50.f;
inline const Vec3 kPrismCentre{0, 0, -3};

inline TestScene prismScene(float abbe) {
    TestScene s;
    s.statics.meshes = basicMeshes();
    s.statics.meshes.push_back(prismMesh(2.f));
    rt::Material flint = glass(1.6f, 0);
    flint.abbe = abbe;
    s.statics.materials = {lambert({0, 0, 0}), flint, emitter({kSlitEmission, kSlitEmission, kSlitEmission})};
    // Minimum deviation at IOR 1.6 through a 60 degree prism: 53.13 degrees outside, 30 inside, so the ray
    // turns by 23.13 degrees at each face, towards the base (-x).
    const float turn = std::asin(1.6f * 0.5f) - radians(30.f);
    const Vec3 in{std::sin(turn), 0, -std::cos(turn)}, out{-std::sin(turn), 0, -std::cos(turn)};
    s.frame.instances = {blackEnclosure(0, {0, 0, -4}, {30, 30, 30}),
                         {3, 1, place(kPrismCentre, kY, 0, {1, 1, 1})},
                         {kQuadMesh, 2, facing(kPrismCentre + out * 8.f, -out, 0.05f, 4)}};
    s.frame.camera = {kPrismCentre - in * 3.f, kPrismCentre, {0, 1, 0}, 10.f};
    return s;
}

// (d) A parallel slab of the same flint glass (2 x 2 x 0.4, turned 25 degrees about y) in front of a 6 x 6
// white emitter of emission 1 (no light) in a black box: every wavelength leaves parallel to how it came.
inline TestScene flintSlabScene(float abbe) {
    TestScene s;
    s.statics.meshes = basicMeshes();
    rt::Material flint = glass(1.6f, 0);
    flint.abbe = abbe;
    s.statics.materials = {lambert({0, 0, 0}), flint, emitter({1, 1, 1})};
    s.frame.instances = {blackEnclosure(0, {0, 0, -3}, {14, 14, 14}),
                         {kCubeMesh, 1, place({0, 0, -3}, kY, 25, {2, 2, 0.4f})},
                         {kQuadMesh, 2, facing({0, 0, -6}, {0, 0, 1}, 6, 6)}};
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 20.f};
    return s;
}

// (d) A white furnace through dispersive glass: a flint sphere (IOR 1.6, Abbe `abbe`, smooth, radius 1) at
// z = -3 inside a box whose walls emit 1 on both sides and reflect nothing; the camera sees only the sphere.
// Every path ends on a wall with its throughput (Fresnel splits sum to 1, the tint is white), so the lighting
// is exactly 1 in expectation, per channel, with or without dispersion.
inline TestScene dispersiveFurnace(float abbe) {
    TestScene s;
    s.statics.meshes = basicMeshes();
    rt::Material flint = glass(1.6f, 0);
    flint.abbe = abbe;
    rt::Material wall = lambert({0, 0, 0});
    wall.emission[0] = wall.emission[1] = wall.emission[2] = 1.f;
    s.statics.materials = {wall, flint};
    s.frame.instances = {blackEnclosure(0, {0, 0, 0}, {10, 10, 10}),
                         {kSphereMesh, 1, place({0, 0, -3}, kY, 0, {1, 1, 1})}};
    s.frame.camera = {{0, 0, 0}, {0, 0, -1}, {0, 1, 0}, 20.f};
    return s;
}

} // namespace evo::test
