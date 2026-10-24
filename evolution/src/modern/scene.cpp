#include "modern/scene.hpp"

#include <cmath>
#include <stdexcept>

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
    return light;
}

CameraBasis cameraBasis(const Camera& camera) {
    const Vec3 forward = normalize(camera.target - camera.eye);
    const Vec3 right = normalize(cross(forward, camera.up));
    return {right, cross(right, forward), forward};
}

} // namespace evo::modern
