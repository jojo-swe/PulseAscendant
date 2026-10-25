#pragma once

// The GPU side of one prepared scene (step 5, Task 1): what the shared path tracer binds when it traces that
// scene. Small (geometry, BLASes, materials), so the window prepares every scene up front and a chapter
// start never hitches; the render targets, the pipeline and the TLAS are the one PathTracer's.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "modern/scene.hpp"
#include "rt/accel.hpp"
#include "rt/geometry.hpp"
#include "rt/spheres.hpp"

#include <volk.h>

#include <cstdint>
#include <memory>

namespace evo::modern {

// Throws std::invalid_argument, naming what is wrong, for a frame the path tracer cannot trace with the
// scene's static side: more instances, lights or (per-frame) materials than the scene's capacity
// (std::length_error), an instance naming a mesh, material or light that does not exist (std::out_of_range),
// a material whose class is no hit group, a per-frame table that shows the retro screen in a scene whose
// static table does not, a sphere instance that is not one of the scene's sphere ranges (or, dynamic, not
// the frame's whole particle field with its material) or that is a light, a static sphere whose material
// does not exist (std::out_of_range) or has another class than its instance's, a particle field beyond
// capacity.particles (std::length_error), without a particle shader or with a material that does not exist
// (std::out_of_range), a light of no LightKind or of kind Tree (Task 12), whose
// area is not |edgeU x edgeV| or whose gate is no Portal light, and a light that disagrees with the instance
// that is its surface (see SceneInstance). CPU only: PathTracer::trace() runs it before recording anything,
// and every scene task's sweep (checkSceneSweep) runs it over every frame of its chapters.
void checkSceneFrame(const SceneStatic& scene, const SceneFrame& frame);

// One scene's geometry store (every mesh in one vertex and one index buffer), its compacted BLASes, its
// static spheres (their buffer and one AABB BLAS per sphere range, Task 8) and its static material table,
// built at setup (immediate submits that wait).
//
// Lifetime: `scene` (the ModernScene's static side) must outlive this object, and this object must outlive
// every frame that traced it: declare it before the PathTracer, whose TLAS waits for the ring's frames when
// it goes (the TLAS's instances point at these BLASes, and the shaders read these buffers).
class SceneGpu {
public:
    // Throws std::invalid_argument for no materials, a material whose class is no hit group, more materials
    // than capacity.materials, more static spheres than capacity.spheres, sphere ranges without spheres, and
    // what GeometryStore and SphereBlas throw.
    SceneGpu(gfx::Context& ctx, const SceneStatic& scene);
    SceneGpu(const SceneGpu&) = delete;
    SceneGpu& operator=(const SceneGpu&) = delete;

    const SceneStatic& source() const { return *source_; }
    const rt::GeometryStore& geometry() const { return store_; }
    const rt::BlasSet& blases() const { return blases_; }
    // The static spheres' buffer and BLASes; null for a scene without static spheres.
    const rt::SphereBlas* spheres() const { return spheres_.get(); }
    // The static material table (a device-local storage buffer), bound when a frame has no table of its own.
    const gfx::Buffer& staticMaterials() const { return materials_; }
    // Whether a static material shows the retro screen (kRetroScreenTexture): the trace then samples the
    // retro image it is given, else a black texture of the tracer's own.
    bool samplesRetro() const { return samplesRetro_; }
    // The device memory this scene holds, as allocated (VMA): vertices, indices, mesh records, the compacted
    // BLASes, the static spheres (buffer and BLASes) and the material table. A task that adds GPU data to the
    // scene adds it here: shared_tracer_test (b) compares it with the allocator's statistics around
    // prepare().
    VkDeviceSize bytes() const;

private:
    const SceneStatic* source_;
    bool samplesRetro_ = false;
    rt::GeometryStore store_;
    rt::BlasSet blases_;
    std::unique_ptr<rt::SphereBlas> spheres_;
    gfx::Buffer materials_;
};

} // namespace evo::modern
