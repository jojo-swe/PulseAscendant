#pragma once

// The GPU side of one prepared scene (step 5, Task 1): what the shared path tracer binds when it traces that
// scene. Small (geometry, BLASes, materials), so the window prepares every scene up front and a chapter
// start never hitches; the render targets, the pipeline and the TLAS are the one PathTracer's.

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "modern/scene.hpp"
#include "rt/accel.hpp"
#include "rt/geometry.hpp"

#include <volk.h>

#include <cstdint>

namespace evo::modern {

// Throws std::invalid_argument, naming what is wrong, for a frame the path tracer cannot trace with the
// scene's static side: more instances, lights or (per-frame) materials than the scene's capacity
// (std::length_error), an instance naming a mesh, material or light that does not exist (std::out_of_range),
// a material whose class is no hit group, a per-frame table that shows the retro screen in a scene whose
// static table does not, a sphere instance (Task 8), a light of no LightKind or of kind Tree (Task 12), whose
// area is not |edgeU x edgeV| or whose gate is no Portal light, and a light that disagrees with the instance
// that is its surface (see SceneInstance). CPU only: PathTracer::trace() runs it before recording anything,
// and every scene task's sweep (checkSceneSweep) runs it over every frame of its chapters.
void checkSceneFrame(const SceneStatic& scene, const SceneFrame& frame);

// One scene's geometry store (every mesh in one vertex and one index buffer), its compacted BLASes and its
// static material table, built at setup (immediate submits that wait). Task 8 adds its static sphere BLAS.
//
// Lifetime: `scene` (the ModernScene's static side) must outlive this object, and this object must outlive
// every frame that traced it: declare it before the PathTracer, whose TLAS waits for the ring's frames when
// it goes (the TLAS's instances point at these BLASes, and the shaders read these buffers).
class SceneGpu {
public:
    // Throws std::invalid_argument for no materials, a material whose class is no hit group, more materials
    // than capacity.materials, static spheres (until Task 8 builds them) or more of them than
    // capacity.spheres, and what GeometryStore throws.
    SceneGpu(gfx::Context& ctx, const SceneStatic& scene);
    SceneGpu(const SceneGpu&) = delete;
    SceneGpu& operator=(const SceneGpu&) = delete;

    const SceneStatic& source() const { return *source_; }
    const rt::GeometryStore& geometry() const { return store_; }
    const rt::BlasSet& blases() const { return blases_; }
    // The static material table (a device-local storage buffer), bound when a frame has no table of its own.
    const gfx::Buffer& staticMaterials() const { return materials_; }
    // Whether a static material shows the retro screen (kRetroScreenTexture): the trace then samples the
    // retro image it is given, else a black texture of the tracer's own.
    bool samplesRetro() const { return samplesRetro_; }
    // The device memory this scene holds, as allocated (VMA): vertices, indices, mesh records, the compacted
    // BLASes and the material table. A task that adds GPU data to the scene (Task 8's sphere BLAS) adds it
    // here: shared_tracer_test (b) compares it with the allocator's statistics around prepare().
    VkDeviceSize bytes() const;

private:
    const SceneStatic* source_;
    bool samplesRetro_ = false;
    rt::GeometryStore store_;
    rt::BlasSet blases_;
    gfx::Buffer materials_;
};

} // namespace evo::modern
