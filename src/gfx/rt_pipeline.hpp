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

// A procedural hit group: an intersection shader for AABB geometry and its closest-hit.
struct ProceduralHitGroup {
    std::filesystem::path closestHit, intersection;
};

// The SPIR-V of a ray tracing pipeline: one raygen, the miss shaders in miss-index order, one closest-hit per
// triangle hit group, then the procedural hit groups (analytic spheres), in SBT order: hit
// group k = an instance's hitGroup k, the triangle groups first. anyHits, when not empty, has one entry per
// triangle hit group: the any-hit of that group, or an empty path for none. All geometry is opaque, so an
// any-hit only runs for a ray traced without the opaque flag that meets an instance made not opaque.
struct RtShaders {
    std::filesystem::path raygen;
    std::vector<std::filesystem::path> misses, closestHits, anyHits;
    std::vector<ProceduralHitGroup> proceduralHits;
    // The values of specialization constants 0, 1, ... for every stage (empty: the shaders' defaults).
    std::vector<std::uint32_t> specialization;
};

// A ray tracing pipeline with its descriptor set layout (set 0, the given bindings, visible to the raygen,
// miss, closest-hit, any-hit and intersection stages), a push constant range for the same stages, and its
// shader binding table: computeSbtLayout(ctx.rayTracingProperties(), misses, closestHits + proceduralHits)
// in a device-local buffer whose start is aligned to shaderGroupBaseAlignment (usage SHADER_BINDING_TABLE |
// SHADER_DEVICE_ADDRESS), filled at construction through an immediate submit (setup work). Group order:
// raygen, the misses, the triangle hit groups, the procedural hit groups.
// maxPipelineRayRecursionDepth is 1: only the raygen traces; miss, closest-hit, any-hit and intersection
// shaders must not.
//
// Lifetime as for a ComputePipeline: the pipeline, its SBT and the sets a frame binds are used until that
// frame's submit completes, so declare the FrameRing after its pipelines. The first bind() ties the
// pipeline to that ring (see DescriptorAllocator).
class RayTracingPipeline {
public:
    static constexpr std::uint32_t kMaxRecursionDepth = 1;

    // Entry points are "main". Throws std::invalid_argument for no miss or no closest-hit shader, anyHits
    // neither empty nor one per closest-hit, a miss, closest-hit, any-hit or intersection shader that traces
    // a ray (OpTraceRayKHR) or any shader that executes a callable (OpExecuteCallableKHR), found by scanning
    // the SPIR-V, or a pushConstantBytes that is not a multiple of 4; std::runtime_error naming a SPIR-V file
    // that cannot load; and what computeSbtLayout throws.
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
    // The device memory of the shader binding table, as allocated.
    VkDeviceSize sbtBytes() const { return sbt_.bytes(); }
    VkPipeline handle() const { return pipeline_; }
    // What the driver reports a thread's ray tracing stack needs, in bytes
    // (vkGetRayTracingShaderGroupStackSizeKHR): the raygen's, and the largest closest-hit, intersection and miss
    // shader's. A large raygen stack is live state spilled around every trace (a diagnostic for --bench).
    struct StackSizes {
        VkDeviceSize raygen = 0, closestHit = 0, intersection = 0, miss = 0;
    };
    StackSizes stackSizes() const;
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
    std::uint32_t missGroups_ = 0, triangleGroups_ = 0, proceduralGroups_ = 0;
    Buffer sbt_;
    VkDeviceAddress sbtAddress_ = 0;
    VkStridedDeviceAddressRegionKHR raygenRegion_{}, missRegion_{}, hitRegion_{}, callableRegion_{};
    DescriptorAllocator descriptors_;
};

} // namespace evo::gfx
