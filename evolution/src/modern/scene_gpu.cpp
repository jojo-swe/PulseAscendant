#include "modern/scene_gpu.hpp"

#include "rt/light_tree.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
    if (scene.spheres.empty() && !scene.sphereRanges.empty())
        throw std::invalid_argument("SceneGpu: sphere ranges, but no static spheres");
    return scene;
}

gfx::Buffer materialBuffer(gfx::Context& ctx, const std::vector<rt::Material>& materials) {
    const auto bytes = std::as_bytes(std::span(materials));
    return gfx::createDeviceBuffer(
        ctx, {bytes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, "static materials"}, bytes);
}

std::unique_ptr<rt::SphereBlas> sphereBlas(gfx::Context& ctx, const SceneStatic& scene) {
    if (scene.spheres.empty())
        return nullptr;
    return std::make_unique<rt::SphereBlas>(ctx, scene.spheres, scene.sphereRanges);
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

// A transform's linear part applied to v (a column of it for a unit axis).
Vec3 transformNormalFree(const rt::Mat34& m, Vec3 v) {
    return transformPoint(m, v) - transformPoint(m, {0, 0, 0});
}

// NEE uses a light's emission, area, kind and facing; a BSDF ray that hits the light's instance uses that
// instance's material and facing. Multiple importance sampling weighs the two as estimators of one integral,
// so they must describe the same emitter, or the picture is silently biased (step 4, Task 7 review I1).
// Expects the instances' mesh, material and light indices checked.
void checkLights(const SceneFrame& frame, const std::vector<rt::Material>& materials) {
    for (std::size_t i = 0; i < frame.lights.size(); ++i) {
        const rt::RectLight& l = frame.lights[i];
        const std::string what = "light " + std::to_string(i) + " ";
        if (l.kind > static_cast<std::uint32_t>(rt::LightKind::Tree))
            throw std::invalid_argument(what + "has kind " + std::to_string(l.kind) +
                                        ", which is no LightKind");
        if (l.kind == static_cast<std::uint32_t>(rt::LightKind::Tree) && l.gate != rt::kNoLight)
            throw std::invalid_argument(what + "is a light tree, which no portal gates");
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
        if (kind == rt::LightKind::Tree) { // checkTreeLights checks the rest
            if (in.kind != rt::GeometryKind::Spheres)
                throw std::invalid_argument(what + "a light tree, but no set of spheres");
            continue;
        }
        if (kind == rt::LightKind::Portal)
            throw std::invalid_argument(what + "a portal: an opening, not a surface");
        if (linearDeterminant(in.transform) < 0.f)
            throw std::invalid_argument(what + "but its transform mirrors: it emits from the light's back");
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

// A sphere instance: one of the scene's ranges of static spheres, or the frame's whole particle field with
// its material; never a light.
void checkSphereInstance(const SceneStatic& scene, const SceneFrame& frame,
                         const std::vector<rt::Material>& materials, std::size_t i) {
    const SceneInstance& in = frame.instances[i];
    const std::string what = "instance " + std::to_string(i) + " ";
    if (in.material >= materials.size())
        throw std::out_of_range(what + "names a material that does not exist");
    if (in.light != rt::kNoLight &&
        (in.light >= frame.lights.size() ||
         frame.lights[in.light].kind != static_cast<std::uint32_t>(rt::LightKind::Tree)))
        throw std::invalid_argument(what + "is a set of spheres, which is no light but the light tree");
    const std::string range =
        "(" + std::to_string(in.firstSphere) + ", " + std::to_string(in.sphereCount) + ")";
    if (in.dynamicSpheres) {
        if (!frame.particles)
            throw std::invalid_argument(what + "places the particle field, but the frame has none");
        if (in.firstSphere != 0 || in.sphereCount != frame.particles->count)
            throw std::invalid_argument(what + "places particles " + range + ", not the whole field of " +
                                        std::to_string(frame.particles->count) + " (its one BLAS)");
        if (in.material != frame.particles->material)
            throw std::invalid_argument(what + "places the particle field with material " +
                                        std::to_string(in.material) + ", not the field's " +
                                        std::to_string(frame.particles->material));
        return;
    }
    const std::vector<rt::SphereRange> ranges =
        rt::sphereRangesOf(scene.sphereRanges, static_cast<std::uint32_t>(scene.spheres.size()));
    if (scene.spheres.empty() || std::find(ranges.begin(), ranges.end(),
                                           rt::SphereRange{in.firstSphere, in.sphereCount}) == ranges.end())
        throw std::invalid_argument(what + "places static spheres " + range +
                                    ", which is no sphere range of the scene (one AABB BLAS each)");
}

// Every static sphere a sphere instance places exists in the material table with that instance's class (its
// hit group shades every sphere of the instance). Each (range, class) once per frame.
void checkStaticSphereClasses(const SceneStatic& scene, const SceneFrame& frame,
                              const std::vector<rt::Material>& materials) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> checked; // (first sphere, class)
    for (std::size_t i = 0; i < frame.instances.size(); ++i) {
        const SceneInstance& in = frame.instances[i];
        if (in.kind != rt::GeometryKind::Spheres || in.dynamicSpheres)
            continue;
        const std::uint32_t materialClass = materials[in.material].materialClass;
        const std::pair<std::uint32_t, std::uint32_t> key{in.firstSphere, materialClass};
        if (std::find(checked.begin(), checked.end(), key) != checked.end())
            continue;
        checked.push_back(key);
        for (std::uint32_t k = in.firstSphere; k < in.firstSphere + in.sphereCount; ++k) {
            const std::uint32_t m = scene.spheres[k].material;
            if (m >= materials.size())
                throw std::out_of_range("static sphere " + std::to_string(k) + " names material " +
                                        std::to_string(m) + ", which does not exist");
            if (materials[m].materialClass != materialClass)
                throw std::invalid_argument(
                    "static sphere " + std::to_string(k) + " has material " + std::to_string(m) +
                    " of class " + std::to_string(materials[m].materialClass) + ", but instance " +
                    std::to_string(i) + " places it with class " + std::to_string(materialClass));
        }
    }
}
// The light tree (Task 12): at most one RectLight of kind Tree, and then tree lights (none without one), at
// most capacity.treeLights; exactly one instance, a sphere instance, is that light, and tree light k is its
// sphere firstSphere + k, within its spheres (the particle field's count when dynamic); the instance scales
// uniformly; every light has a finite centre, a radius > 0, an emission and a scale >= 0, and for static
// spheres the centre, radius and emission treeLightsOf gives (a dynamic field's are the scene's C++ mirror).
void checkTreeLights(const SceneStatic& scene, const SceneFrame& frame,
                     const std::vector<rt::Material>& materials) {
    std::uint32_t tree = rt::kNoLight;
    for (std::size_t i = 0; i < frame.lights.size(); ++i)
        if (frame.lights[i].kind == static_cast<std::uint32_t>(rt::LightKind::Tree)) {
            if (tree != rt::kNoLight)
                throw std::invalid_argument("lights " + std::to_string(tree) + " and " + std::to_string(i) +
                                            " are both light trees (a frame has one)");
            tree = static_cast<std::uint32_t>(i);
        }
    const std::vector<TreeLightFrame>& lights = frame.treeLights;
    if (tree == rt::kNoLight) {
        if (!lights.empty())
            throw std::invalid_argument(std::to_string(lights.size()) +
                                        " tree lights, but no light of kind Tree stands for them");
        return;
    }
    const std::string what = "light tree " + std::to_string(tree) + " ";
    if (lights.empty())
        throw std::invalid_argument(what + "has no tree lights");
    if (lights.size() > scene.capacity.treeLights)
        throw std::length_error(std::to_string(lights.size()) +
                                " tree lights, more than the scene's capacity of " +
                                std::to_string(scene.capacity.treeLights));
    if (lights.size() > rt::kMaxTreeLights)
        throw std::length_error(std::to_string(lights.size()) + " tree lights, more than " +
                                std::to_string(rt::kMaxTreeLights));
    const SceneInstance* owner = nullptr;
    for (const SceneInstance& in : frame.instances)
        if (in.light == tree) {
            if (owner)
                throw std::invalid_argument(what +
                                            "is more than one instance (its lights are one set of spheres)");
            owner = &in;
        }
    if (!owner)
        throw std::invalid_argument(what + "is no instance's light (its spheres are the tree's lights)");
    const SceneInstance& in = *owner;
    const std::uint32_t available = in.dynamicSpheres ? frame.particles->count : in.sphereCount;
    if (lights.size() > available)
        throw std::invalid_argument(what + "has " + std::to_string(lights.size()) +
                                    " lights, more than the " + std::to_string(available) +
                                    " spheres of its instance");
    const float sx = length(transformNormalFree(in.transform, {1, 0, 0}));
    const float sy = length(transformNormalFree(in.transform, {0, 1, 0}));
    const float sz = length(transformNormalFree(in.transform, {0, 0, 1}));
    if (!(std::abs(sx - sy) <= 1e-4f * sx && std::abs(sx - sz) <= 1e-4f * sx))
        throw std::invalid_argument(what +
                                    "is an instance that does not scale uniformly (its spheres would be "
                                    "ellipsoids)");
    std::vector<TreeLightFrame> expected;
    if (!in.dynamicSpheres)
        expected = treeLightsOf(scene, in, materials, static_cast<std::uint32_t>(lights.size()));
    for (std::size_t k = 0; k < lights.size(); ++k) {
        const TreeLightFrame& l = lights[k];
        const char* wrong = nullptr;
        if (l.sphere != in.firstSphere + k)
            wrong = "is not sphere firstSphere + k of its instance";
        else if (!std::isfinite(l.center.x) || !std::isfinite(l.center.y) || !std::isfinite(l.center.z))
            wrong = "has a centre that is not finite";
        else if (!(l.radius > 0.f) || !std::isfinite(l.radius))
            wrong = "has a radius that is not finite and > 0";
        else if (!(l.emission.x >= 0.f && l.emission.y >= 0.f && l.emission.z >= 0.f) ||
                 !std::isfinite(l.emission.x + l.emission.y + l.emission.z))
            wrong = "has an emission that is not finite and >= 0";
        else if (!(l.scale >= 0.f) || !std::isfinite(l.scale))
            wrong = "has a scale that is not finite and >= 0";
        else if (!expected.empty()) {
            const TreeLightFrame& e = expected[k];
            const float tolerance =
                1e-4f * (1.f + std::max({std::abs(e.center.x), std::abs(e.center.y), std::abs(e.center.z)}));
            if (std::abs(l.center.x - e.center.x) > tolerance ||
                std::abs(l.center.y - e.center.y) > tolerance ||
                std::abs(l.center.z - e.center.z) > tolerance || !nearlyEqual(l.radius, e.radius))
                wrong = "is not where its sphere is (centre and radius under the instance's transform)";
            else if (!nearlyEqual(l.emission.x, e.emission.x) || !nearlyEqual(l.emission.y, e.emission.y) ||
                     !nearlyEqual(l.emission.z, e.emission.z))
                wrong =
                    "has an emission other than its sphere's radiance (colour * emission + its material's)";
        }
        if (wrong)
            throw std::invalid_argument("tree light " + std::to_string(k) + " " + wrong);
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
    if (frame.particles) {
        const ParticleFrame& p = *frame.particles;
        if (p.count > capacity.particles)
            throw tooMany("particles", p.count, capacity.particles);
        if (scene.particleShader.empty())
            throw std::invalid_argument("the frame has a particle field, but the scene no particle shader");
        if (p.material >= materials.size())
            throw std::out_of_range("the particle field's material " + std::to_string(p.material) +
                                    " does not exist");
    }
    for (std::size_t i = 0; i < frame.instances.size(); ++i) {
        const SceneInstance& in = frame.instances[i];
        if (in.kind == rt::GeometryKind::Spheres) {
            checkSphereInstance(scene, frame, materials, i);
            continue;
        }
        if (in.kind != rt::GeometryKind::Triangles)
            throw std::invalid_argument("instance " + std::to_string(i) + " has kind " +
                                        std::to_string(static_cast<std::uint32_t>(in.kind)) +
                                        ", which is no GeometryKind");
        if (in.mesh >= scene.meshes.size() || in.material >= materials.size() ||
            (in.light != rt::kNoLight && in.light >= frame.lights.size()))
            throw std::out_of_range("instance " + std::to_string(i) +
                                    " names a mesh, material or light that does not exist");
    }
    checkStaticSphereClasses(scene, frame, materials);
    checkLights(frame, materials);
    checkTreeLights(scene, frame, materials);
    packEnvironment(frame.environment); // throws for an invalid environment
    checkSplats(frame.splats, capacity.splats, scene.splatShader);
}

SceneGpu::SceneGpu(gfx::Context& ctx, const SceneStatic& scene)
    : source_(&checked(scene)), samplesRetro_(usesRetroScreen(scene.materials)), store_(ctx, scene.meshes),
      blases_(ctx, store_), spheres_(sphereBlas(ctx, scene)),
      materials_(materialBuffer(ctx, scene.materials)) {}

VkDeviceSize SceneGpu::bytes() const {
    return store_.vertices().bytes() + store_.indices().bytes() + store_.meshRecords().bytes() +
           blases_.bytes() + (spheres_ ? spheres_->bytes() : 0) + materials_.bytes();
}

} // namespace evo::modern
