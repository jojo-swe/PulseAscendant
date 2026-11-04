#pragma once

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "retro/scene.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <vector>

namespace evo::render {

// Draws a retro frame's scene (retro/scene.hpp) into the 320x200 R8_UINT palette index image: the triangles
// with shaders/retro/polys.comp, then the points with shaders/retro/points.comp, both in list order over
// whatever the image holds (the effect's shader or its background).
//
// The primitives go to a host-visible buffer of the ring slot being recorded, which the CPU rewrites only
// after begin() has waited for that slot's previous frame (the ring's onBegin callback frees the slot's
// buffers); each draw of a frame takes the slot's next buffer, made the first time a frame needs it, each
// large enough for kMaxTriangles and kMaxPoints. The points' per-pixel order buffer is device-local and
// shared by every draw, behind barriers.
//
// The ring must outlive the rasteriser; the destructor waits for it before the pipelines and buffers go.
class SceneRaster {
public:
    SceneRaster(gfx::Context& ctx, gfx::FrameRing& ring);
    ~SceneRaster();
    SceneRaster(const SceneRaster&) = delete;
    SceneRaster& operator=(const SceneRaster&) = delete;

    // Records the scene's passes into the ring's current command buffer and leaves the image in GENERAL.
    // Throws std::length_error for more than kMaxTriangles triangles or kMaxPoints points,
    // std::invalid_argument for a vertex or point outside the guard band (kGuardMinX... in pixels) or an
    // image that is not 320x200 R8_UINT, and std::logic_error outside the ring's begin()/submit().
    void draw(VkCommandBuffer cmd, gfx::Image& indexImage, const retro::RetroScene& scene);

private:
    struct SlotBuffers {
        std::vector<gfx::Buffer> buffers; // one per draw of the frame
        std::size_t used = 0;             // buffers taken this frame
    };

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    gfx::ComputePipeline polys_;
    gfx::ComputePipeline points_;
    gfx::Buffer order_; // points: the number + 1 of the last point on each pixel, 0 for none
    std::array<SlotBuffers, gfx::FrameRing::kFramesInFlight> slots_;
    gfx::FrameRing::Subscription subscription_; // last: unregistered first
};

} // namespace evo::render
