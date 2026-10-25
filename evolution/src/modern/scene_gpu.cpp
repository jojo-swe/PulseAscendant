#include "modern/scene_gpu.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>

namespace evo::modern {

namespace {

const SceneStatic& checked(const SceneStatic& scene) {
    if (scene.materials.empty())
        throw std::invalid_argument("SceneGpu: a scene needs at least one material");
    if (scene.materials.size() > scene.capacity.materials)
        throw std::invalid_argument("SceneGpu: " + std::to_string(scene.materials.size()) +
                                    " materials, more than the scene's capacity of " +
                                    std::to_string(scene.capacity.materials));
    for (std::size_t i = 0; i < scene.materials.size(); ++i)
        if (scene.materials[i].materialClass >= rt::kHitGroupsPerKind)
            throw std::invalid_argument("SceneGpu: material " + std::to_string(i) + " has class " +
                                        std::to_string(scene.materials[i].materialClass) +
                                        ", which is no hit group");
    if (scene.spheres.size() > scene.capacity.spheres)
        throw std::invalid_argument("SceneGpu: " + std::to_string(scene.spheres.size()) +
                                    " static spheres, more than the scene's capacity of " +
                                    std::to_string(scene.capacity.spheres));
    if (!scene.spheres.empty())
        throw std::invalid_argument("SceneGpu: static spheres need the sphere BLAS (step 5, Task 8)");
    return scene;
}

gfx::Buffer materialBuffer(gfx::Context& ctx, const std::vector<rt::Material>& materials) {
    const auto bytes = std::as_bytes(std::span(materials));
    return gfx::createDeviceBuffer(
        ctx, {bytes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "static materials"}, bytes);
}

bool usesRetroScreen(const std::vector<rt::Material>& materials) {
    return std::any_of(materials.begin(), materials.end(),
                       [](const rt::Material& m) { return m.texture == rt::kRetroScreenTexture; });
}

// Equal within a relative 1e-5 (rounding between two ways of computing one value).
bool nearlyEqual(float a, float b) {
    return std::abs(double(a) - double(b)) <= 1e-5 * std::max(std::abs(double(a)), std::abs(double(b)));
}

Vec3 vec3(const float v[3]) {
    return {v[0], v[1], v[2]};
}

// NEE uses a light's emission, area, kind and facing; a BSDF ray that hits the light's instance uses that
// instance's material and facing. Multiple importance sampling weighs the two as estimators of one integral,
// so they must describe the same emitter, or the picture is silently biased (step 4, Task 7 review I1). A
// camera-only emitter (Task 6 of step 5) is never a light: the integrator ignores its emission after a rough
// vertex, where NEE would add it. Expects the instances' mesh, material and light indices checked.
void checkLights(const SceneFrame& frame, const std::vector<rt::Material>& materials) {
    for (std::size_t i = 0; i < frame.lights.size(); ++i) {
        const rt::RectLight& l = frame.lights[i];
        const std::string what = "light " + std::to_string(i) + " ";
        if (l.kind > static_cast<std::uint32_t>(rt::LightKind::Tree))
            throw std::invalid_argument(what + "has kind " + std::to_string(l.kind) +
                                        ", which is no LightKind");
        if (l.kind == static_cast<std::uint32_t>(rt::LightKind::Tree))
            throw std::invalid_argument(what + "is a light tree, which needs step 5's Task 12");
        const float area = length(cross(vec3(l.edgeU), vec3(l.edgeV)));
        if (!nearlyEqual(l.area, area))
            throw std::invalid_argument(what + "has area " + std::to_string(l.area) +
                                        ", but |edgeU x edgeV| is " + std::to_string(area));
        if (l.gate != rt::kNoLight &&
            (l.gate >= frame.lights.size() ||
             frame.lights[l.gate].kind != static_cast<std::uint32_t>(rt::LightKind::Portal)))
            throw std::invalid_argument(what + "is gated by light " + std::to_string(l.gate) +
                                        ", which is no Portal light of the frame");
    }
    for (std::size_t i = 0; i < frame.instances.size(); ++i) {
        const SceneInstance& in = frame.instances[i];
        if (in.light == rt::kNoLight)
            continue;
        const rt::RectLight& l = frame.lights[in.light];
        const rt::Material& m = materials[in.material];
        const std::string what =
            "instance " + std::to_string(i) + " is light " + std::to_string(in.light) + ", ";
        const auto kind = static_cast<rt::LightKind>(l.kind);
        if (kind == rt::LightKind::Portal)
            throw std::invalid_argument(what + "a portal: an opening, not a surface");
        if (linearDeterminant(in.transform) < 0.f)
            throw std::invalid_argument(what + "but its transform mirrors: it emits from the light's back");
        if ((m.flags & rt::kMaterialCameraOnlyEmission) != 0u)
            throw std::invalid_argument(what + "but its material's emission is camera-only (never a light)");
        const bool screen = m.texture == rt::kRetroScreenTexture;
        if (kind == rt::LightKind::Emitter) {
            if (screen)
                throw std::invalid_argument(what + "an Emitter, but its material shows the retro screen");
            for (int c = 0; c < 3; ++c)
                if (!nearlyEqual(l.emission[c], m.emission[c] * m.emissionScale))
                    throw std::invalid_argument(what + "whose emission is not its material's emission * "
                                                       "emissionScale");
        } else {
            if (!screen || m.materialClass != static_cast<std::uint32_t>(rt::MaterialClass::Emissive))
                throw std::invalid_argument(what + "a RetroScreen, but its material is no Emissive material "
                                                   "with the retro screen texture");
            for (int c = 0; c < 3; ++c)
                if (!nearlyEqual(l.emission[c], m.emissionScale))
                    throw std::invalid_argument(what +
                                                "a RetroScreen, whose emission is not (s, s, s) for its "
                                                "material's emissionScale s");
        }
    }
}

} // namespace

void checkSceneFrame(const SceneStatic& scene, const SceneFrame& frame) {
    const SceneCapacity& capacity = scene.capacity;
    auto tooMany = [](const char* what, std::size_t count, std::uint32_t most) {
        return std::length_error(std::to_string(count) + " " + what + ", more than the scene's capacity of " +
                                 std::to_string(most));
    };
    if (frame.instances.size() > capacity.instances)
        throw tooMany("instances", frame.instances.size(), capacity.instances);
    if (frame.lights.size() > capacity.lights)
        throw tooMany("lights", frame.lights.size(), capacity.lights);
    if (frame.materials.size() > capacity.materials)
        throw tooMany("materials", frame.materials.size(), capacity.materials);
    const std::vector<rt::Material>& materials = frame.materials.empty() ? scene.materials : frame.materials;
    const bool staticRetro = usesRetroScreen(scene.materials);
    for (std::size_t i = 0; i < frame.materials.size(); ++i) {
        const rt::Material& m = frame.materials[i];
        if (m.materialClass >= rt::kHitGroupsPerKind)
            throw std::invalid_argument("the frame's material " + std::to_string(i) + " has class " +
                                        std::to_string(m.materialClass) + ", which is no hit group");
        if (m.texture == rt::kRetroScreenTexture && !staticRetro)
            throw std::invalid_argument("the frame's material " + std::to_string(i) +
                                        " shows the retro screen, but the scene's static table does not "
                                        "(the scene is traced without the retro image)");
    }
    for (std::size_t i = 0; i < frame.instances.size(); ++i) {
        const SceneInstance& in = frame.instances[i];
        if (in.kind != rt::GeometryKind::Triangles)
            throw std::invalid_argument("instance " + std::to_string(i) +
                                        " is a set of spheres, which needs step 5's Task 8");
        if (in.mesh >= scene.meshes.size() || in.material >= materials.size() ||
            (in.light != rt::kNoLight && in.light >= frame.lights.size()))
            throw std::out_of_range("instance " + std::to_string(i) +
                                    " names a mesh, material or light that does not exist");
    }
    checkLights(frame, materials);
    packEnvironment(frame.environment); // throws for an invalid environment
}

SceneGpu::SceneGpu(gfx::Context& ctx, const SceneStatic& scene)
    : source_(&checked(scene)), samplesRetro_(usesRetroScreen(scene.materials)), store_(ctx, scene.meshes),
      blases_(ctx, store_), materials_(materialBuffer(ctx, scene.materials)) {}

VkDeviceSize SceneGpu::bytes() const {
    return store_.vertices().bytes() + store_.indices().bytes() + store_.meshRecords().bytes() +
           blases_.bytes() + materials_.bytes();
}

} // namespace evo::modern
