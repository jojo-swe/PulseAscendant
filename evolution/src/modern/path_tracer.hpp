#pragma once

// The 2026 side's path tracer: one raygen that loops over bounces with next-event estimation and multiple
// importance sampling against rectangular area lights (emitters, the retro screen, portals onto the sky); one
// closest-hit per material class that only evaluates its material into the payload; misses for the sky and
// for shadow rays. Only the raygen traces (recursion depth 1).

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "gfx/rt_pipeline.hpp"
#include "modern/scene.hpp"
#include "rt/accel.hpp"
#include "rt/geometry.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <optional>

namespace evo::modern {

struct TraceSettings {
    std::uint32_t spp = 1, maxBounces = 4; // maxBounces: scattering events after the primary hit
    std::uint32_t frameSeed = 0;
    bool nee = true;
    bool russianRoulette = true; // from bounce 3
    float clampIndirect = 64.f;  // luminance limit of a contribution after the first bounce
};

// Samples per trace dispatch: an export at many spp is split so that no dispatch runs long enough to trip a
// GPU watchdog (Windows TDR).
inline constexpr std::uint32_t kSppPerDispatch = 4;

// The frame seed (Global Constraints): pcgHash(sampleIndex + 0x9E3779B9 * framesSinceReset) with sampleIndex
// = llround(showSeconds * 48000), the audio sample clock (mod 2^32). Export passes framesSinceReset 0.
std::uint32_t frameSeedFor(double showSeconds, std::uint32_t framesSinceReset);

// The path tracer's outputs and guides, all at the trace size:
//   lighting  RGBA32F  mean over the frame's samples of path radiance without primary-hit emission
//   emission  RGBA16F  mean primary-hit emission (and the sky on a primary miss)
//   albedo    RGBA16F  mean primary-hit demodulation albedo (diffuse: baseColor; metal: F0; glass and miss:
//   1) normal    RGBA16F  xyz world shading normal of the primary hit facing the camera, w roughness (0 on a
//   miss) depth     R32F     linear view depth of the primary hit (centre ray, unjittered), -1 on a miss
//   motion    RGBA16F  xy = previous pixel centre - current pixel centre from the previous camera, z = 1
//   valid
// Every image is left in GENERAL after the trace's last write (Image tracks it).
struct GBuffer {
    gfx::Image lighting, emission, albedo, normal, depth, motion;
};

class PathTracer {
public:
    static constexpr std::uint32_t kMaxInstances = 1024; // the TLAS capacity
    static constexpr std::uint32_t kMaxLights = 64;

    // Builds the geometry store, the BLASes, the ray tracing pipeline (4 hit groups = MaterialClass; misses:
    // sky, shadow) and the material buffer once (setup work). A scene with no material using
    // kRetroScreenTexture does not sample the retro screen: it binds its own 1x1 black texture and ignores
    // the image trace() is given. Throws std::invalid_argument for no materials, a materialClass that is no
    // hit group or an empty size, and what GeometryStore throws.
    PathTracer(gfx::Context& ctx, gfx::FrameRing& ring, const SceneStatic& scene, std::uint32_t width,
               std::uint32_t height);
    ~PathTracer();
    PathTracer(const PathTracer&) = delete;
    PathTracer& operator=(const PathTracer&) = delete;

    // Recreates the images (waits for the queue first: a resize path).
    void resize(std::uint32_t width, std::uint32_t height);

    // Records into the frame being recorded on the ring: the TLAS build, this slot's FrameUniforms, instance
    // records and lights, then ceil(spp / kSppPerDispatch) trace dispatches (lighting, emission and albedo
    // keep a running mean across them; the other guides come from the first). previousCamera feeds the motion
    // guide (nullopt: motion invalid). retroScreen (R8G8B8A8_SRGB, mipmapped) is sampled through
    // sampledView() in GENERAL; trace() records the barrier. Throws, before recording anything,
    // std::invalid_argument for spp 0, std::length_error for more than kMaxInstances instances or kMaxLights
    // lights, std::out_of_range for an instance naming a mesh, material or light that does not exist, and
    // std::logic_error outside ring.begin()/submit().
    void trace(VkCommandBuffer cmd, const SceneFrame& frame, const std::optional<Camera>& previousCamera,
               const TraceSettings& settings, gfx::Image& retroScreen);

    GBuffer& gbuffer() { return gbuffer_; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    bool samplesRetro() const { return samplesRetro_; }

    struct Stats {
        std::uint32_t traceDispatches = 0; // in the last trace()
    };
    Stats lastStats() const { return stats_; }

private:
    void createImages();

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t width_ = 0, height_ = 0;
    std::uint32_t meshCount_ = 0;
    std::vector<std::uint32_t> materialClasses_;
    bool samplesRetro_ = false;
    Stats stats_{};

    // Declared before the TLAS so that they outlive it (and the frames that trace it).
    rt::GeometryStore store_;
    rt::BlasSet blases_;
    gfx::Buffer materials_;
    gfx::Image blackScreen_;
    gfx::RayTracingPipeline pipeline_;
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> uniforms_, instanceRecords_, lights_;
    GBuffer gbuffer_;
    // Last: destroyed first, its destructor waits for the ring's frames in flight before anything above goes.
    rt::Tlas tlas_;
};

} // namespace evo::modern
