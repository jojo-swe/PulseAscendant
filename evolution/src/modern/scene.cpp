#include "modern/scene.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace evo::modern {

namespace {

Vec3 linearPart(const rt::Mat34& m, Vec3 v) {
    return {m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z,
            m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
            m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z};
}

void store(float out[3], Vec3 v) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

} // namespace

float linearDeterminant(const rt::Mat34& transform) {
    const Vec3 x = linearPart(transform, {1, 0, 0}), y = linearPart(transform, {0, 1, 0}),
               z = linearPart(transform, {0, 0, 1});
    return dot(cross(x, y), z);
}

rt::RectLight rectLightForQuad(const rt::Mat34& transform, float width, float height, rt::LightKind kind,
                               Vec3 emission, float selectWeight) {
    if (linearDeterminant(transform) < 0.f)
        throw std::invalid_argument("rectLightForQuad: a mirroring transform (negative determinant)");
    const Vec3 corner = transformPoint(transform, {-0.5f * width, -0.5f * height, 0.f});
    const Vec3 edgeU = linearPart(transform, {width, 0.f, 0.f});
    const Vec3 edgeV = linearPart(transform, {0.f, height, 0.f});
    rt::RectLight light{};
    store(light.corner, corner);
    store(light.edgeU, edgeU);
    store(light.edgeV, edgeV);
    store(light.emission, emission);
    light.kind = static_cast<std::uint32_t>(kind);
    light.selectWeight = selectWeight;
    light.area = length(cross(edgeU, edgeV));
    light.gate = rt::kNoLight;
    return light;
}

std::vector<TreeLightFrame> treeLightsOf(const SceneStatic& scene, const SceneInstance& instance,
                                         const std::vector<rt::Material>& materials, std::uint32_t count) {
    if (count > instance.sphereCount || std::size_t(instance.firstSphere) + count > scene.spheres.size())
        throw std::out_of_range("treeLightsOf: " + std::to_string(count) +
                                " spheres, beyond the instance's or the scene's");
    const float scale = length(linearPart(instance.transform, {1.f, 0.f, 0.f}));
    std::vector<TreeLightFrame> lights;
    lights.reserve(count);
    for (std::uint32_t k = 0; k < count; ++k) {
        const std::uint32_t index = instance.firstSphere + k;
        const rt::SphereRecord& s = scene.spheres[index];
        if (s.material >= materials.size())
            throw std::out_of_range("treeLightsOf: sphere " + std::to_string(index) + " names material " +
                                    std::to_string(s.material) + ", which does not exist");
        const rt::Material& m = materials[s.material];
        TreeLightFrame l;
        l.center = transformPoint(instance.transform, {s.center[0], s.center[1], s.center[2]});
        l.radius = s.radius * scale;
        l.emission = {s.color[0] * s.emission + m.emission[0] * m.emissionScale,
                      s.color[1] * s.emission + m.emission[1] * m.emissionScale,
                      s.color[2] * s.emission + m.emission[2] * m.emissionScale};
        l.sphere = index;
        lights.push_back(l);
    }
    return lights;
}

CameraBasis cameraBasis(const Camera& camera) {
    const Vec3 forward = normalize(camera.target - camera.eye);
    const Vec3 right = normalize(cross(forward, camera.up));
    return {right, cross(right, forward), forward};
}

} // namespace evo::modern
