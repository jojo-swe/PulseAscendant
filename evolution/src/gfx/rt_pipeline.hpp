#pragma once

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/descriptors.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/sbt.hpp"

#include <volk.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace evo::gfx {

// The SPIR-V of a ray tracing pipeline: one raygen, the miss shaders in miss-index order, and one closest-hit
// per triangle hit group in SBT order (hit group k = an instance's hitGroup k). No any-hit or intersection
// shaders: all geometry is opaque triangles.
struct RtShaders {
    std::filesystem::path raygen;
    std::vector<std::filesystem::path> misses, closestHits;
};

// A ray tracing pipeline with its descriptor set layout (set 0, the given bindings, visible to the raygen,
// miss and closest-hit stages), a push constant range for the same stages, and its shader binding table:
// computeSbtLayout(ctx.rayTracingProperties(), misses, closestHits) in a device-local buffer whose start is
// aligned to shaderGroupBaseAlignment (usage SHADER_BINDING_TABLE | SHADER_DEVICE_ADDRESS), filled at
// construction through an immediate submit (setup work). Group order: raygen, the misses, the hit groups.
// maxPipelineRayRecursionDepth is 1: only the raygen traces; miss and closest-hit shaders must not.
//
// Lifetime as for a ComputePipeline: the pipeline, its SBT and the sets a frame binds are used until that
// frame's submit completes, so declare the FrameRing after its pipelines. The first bind() ties the
// pipeline to that ring (see DescriptorAllocator).
class RayTracingPipeline {
public:
    static constexpr std::uint32_t kMaxRecursionDepth = 1;

    // Entry points are "main". Throws std::invalid_argument for no miss or no closest-hit shader, a miss or
    // closest-hit shader that traces a ray (OpTraceRayKHR) or any shader that executes a callable
    // (OpExecuteCallableKHR), found by scanning the SPIR-V, or a pushConstantBytes that is not a multiple of
    // 4; std::runtime_error naming a SPIR-V file that cannot load; and what computeSbtLayout throws.
    RayTracingPipeline(Context& ctx, const RtShaders& shaders, std::span<const Binding> bindings,
                       std::uint32_t pushConstantBytes);
    ~RayTracingPipeline();
    RayTracingPipeline(const RayTracingPipeline&) = delete;
    RayTracingPipeline& operator=(const RayTracingPipeline&) = delete;

    // Binds the pipeline and a descriptor set written with `writes` (see writeDescriptors, which throws
    // std::invalid_argument for writes that do not match the bindings; nothing is recorded then). Between
    // ring.begin() and ring.submit() only (std::logic_error otherwise). A pipeline with no bindings takes no
    // writes and binds the pipeline alone.
    void bind(VkCommandBuffer cmd, FrameRing& ring, std::span<const DescriptorWrite> writes);
    // Throws std::invalid_argument for more bytes than the push constant range, or a size that is not a
    // multiple of 4. Zero bytes record nothing.
    void push(VkCommandBuffer cmd, const void* data, std::uint32_t bytes);
    // One raygen invocation per pixel of width x height, through the pipeline's SBT; after bind().
    void traceRays(VkCommandBuffer cmd, std::uint32_t width, std::uint32_t height);

    const SbtLayout& sbtLayout() const { return sbtLayout_; }
    VkDeviceAddress sbtAddress() const { return sbtAddress_; }
    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }
    const DescriptorLayout& descriptorLayout() const { return setLayout_; }

private:
    void destroy() noexcept;

    Context& ctx_;
    std::uint32_t pushConstantBytes_ = 0;
    DescriptorLayout setLayout_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    SbtLayout sbtLayout_{};
    Buffer sbt_;
    VkDeviceAddress sbtAddress_ = 0;
    VkStridedDeviceAddressRegionKHR raygenRegion_{}, missRegion_{}, hitRegion_{}, callableRegion_{};
    DescriptorAllocator descriptors_;
};

} // namespace evo::gfx
