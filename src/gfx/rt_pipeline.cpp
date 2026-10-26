#include "gfx/rt_pipeline.hpp"

#include "gfx/shader.hpp"
#include "gfx/vk_check.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace evo::gfx {

namespace {

constexpr VkShaderStageFlags kRtStages = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
                                         VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                                         VK_SHADER_STAGE_INTERSECTION_BIT_KHR;

// Checked before anything is created.
std::uint32_t checkedPushConstantBytes(const RtShaders& shaders, std::uint32_t bytes) {
    if (shaders.misses.empty())
        throw std::invalid_argument("RayTracingPipeline: no miss shader");
    if (shaders.closestHits.empty())
        throw std::invalid_argument("RayTracingPipeline: no closest-hit shader");
    if (bytes % 4 != 0)
        throw std::invalid_argument("RayTracingPipeline: push constant size " + std::to_string(bytes) +
                                    " is not a multiple of 4");
    return bytes;
}

// Shader modules destroyed when the pipeline has been created (or creation failed).
struct Modules {
    explicit Modules(VkDevice device) : device(device) {}
    ~Modules() {
        for (VkShaderModule module : modules)
            vkDestroyShaderModule(device, module, nullptr);
    }
    Modules(const Modules&) = delete;
    Modules& operator=(const Modules&) = delete;

    VkDevice device;
    std::vector<VkShaderModule> modules;
};

VkRayTracingShaderGroupCreateInfoKHR generalGroup(std::uint32_t stage) {
    VkRayTracingShaderGroupCreateInfoKHR group{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    group.generalShader = stage;
    group.closestHitShader = VK_SHADER_UNUSED_KHR;
    group.anyHitShader = VK_SHADER_UNUSED_KHR;
    group.intersectionShader = VK_SHADER_UNUSED_KHR;
    return group;
}

VkRayTracingShaderGroupCreateInfoKHR triangleHitGroup(std::uint32_t closestHitStage) {
    VkRayTracingShaderGroupCreateInfoKHR group{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    group.generalShader = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = closestHitStage;
    group.anyHitShader = VK_SHADER_UNUSED_KHR; // opaque geometry: no any-hit
    group.intersectionShader = VK_SHADER_UNUSED_KHR;
    return group;
}

// An AABB hit group: the intersection shader reports hits, the closest-hit shades them.
VkRayTracingShaderGroupCreateInfoKHR proceduralHitGroup(std::uint32_t closestHitStage,
                                                        std::uint32_t intersectionStage) {
    VkRayTracingShaderGroupCreateInfoKHR group{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
    group.generalShader = VK_SHADER_UNUSED_KHR;
    group.closestHitShader = closestHitStage;
    group.anyHitShader = VK_SHADER_UNUSED_KHR; // opaque geometry: no any-hit
    group.intersectionShader = intersectionStage;
    return group;
}

std::uint32_t hitGroupCount(const RtShaders& shaders) {
    return static_cast<std::uint32_t>(shaders.closestHits.size() + shaders.proceduralHits.size());
}

VkStridedDeviceAddressRegionKHR region(VkDeviceAddress base, const SbtRegion& r) {
    return {base + r.offset, r.stride, r.size};
}

} // namespace

RayTracingPipeline::RayTracingPipeline(Context& ctx, const RtShaders& shaders,
                                       std::span<const Binding> bindings, std::uint32_t pushConstantBytes)
    : ctx_(ctx), pushConstantBytes_(checkedPushConstantBytes(shaders, pushConstantBytes)),
      setLayout_(ctx, bindings, kRtStages),
      sbtLayout_(computeSbtLayout(ctx.rayTracingProperties(),
                                  static_cast<std::uint32_t>(shaders.misses.size()), hitGroupCount(shaders))),
      descriptors_(ctx) {
    const VkDevice device = ctx.device();
    const RayTracingProperties& properties = ctx.rayTracingProperties();
    const std::string name = shaders.raygen.filename().string();
    try {
        const VkDescriptorSetLayout setLayout = setLayout_.handle();
        const VkPushConstantRange pushRange{kRtStages, 0, pushConstantBytes};
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &setLayout;
        layoutInfo.pushConstantRangeCount = pushConstantBytes > 0 ? 1 : 0;
        layoutInfo.pPushConstantRanges = pushConstantBytes > 0 ? &pushRange : nullptr;
        vkCheck(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout_), "vkCreatePipelineLayout");

        // Stages and groups: raygen, the misses, one triangle hit group per closest-hit, the procedural hit
        // groups.
        Modules modules(device);
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        // Every stage gets the same constants (a shader ignores the ids it does not declare).
        std::vector<VkSpecializationMapEntry> entries;
        for (std::uint32_t i = 0; i < shaders.specialization.size(); ++i)
            entries.push_back({i, i * std::uint32_t(sizeof(std::uint32_t)), sizeof(std::uint32_t)});
        const VkSpecializationInfo specialization{static_cast<std::uint32_t>(entries.size()), entries.data(),
                                                  shaders.specialization.size() * sizeof(std::uint32_t),
                                                  shaders.specialization.data()};
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
        const auto addStage = [&](const std::filesystem::path& spv, VkShaderStageFlagBits stage) {
            const std::vector<std::uint32_t> words = loadSpirv(spv);
            // Recursion depth 1, mechanically: only the raygen traces, and nothing calls a callable (there is
            // no callable region). Neither lavapipe nor the validation layer enforces
            // maxPipelineRayRecursionDepth; an RTX GPU would hang. The NV opcodes (OpTraceNV,
            // OpTraceRayMotionNV, OpExecuteCallableNV, OpHitObject*NV) are not checked: they need NV
            // extensions, which this project never enables.
            if (stage != VK_SHADER_STAGE_RAYGEN_BIT_KHR && spirvHasOpcode(words, kSpirvOpTraceRayKHR))
                throw std::invalid_argument("RayTracingPipeline: " + spv.filename().string() +
                                            " traces a ray, but only the raygen may (recursion depth 1)");
            if (spirvHasOpcode(words, kSpirvOpExecuteCallableKHR))
                throw std::invalid_argument("RayTracingPipeline: " + spv.filename().string() +
                                            " executes a callable shader; the pipeline has none");
            modules.modules.push_back(createShaderModule(device, words, spv));
            VkPipelineShaderStageCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            info.stage = stage;
            info.module = modules.modules.back();
            info.pName = "main";
            info.pSpecializationInfo = entries.empty() ? nullptr : &specialization;
            stages.push_back(info);
            return static_cast<std::uint32_t>(stages.size() - 1);
        };
        groups.push_back(generalGroup(addStage(shaders.raygen, VK_SHADER_STAGE_RAYGEN_BIT_KHR)));
        for (const auto& miss : shaders.misses)
            groups.push_back(generalGroup(addStage(miss, VK_SHADER_STAGE_MISS_BIT_KHR)));
        for (const auto& closestHit : shaders.closestHits)
            groups.push_back(triangleHitGroup(addStage(closestHit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)));
        for (const ProceduralHitGroup& hit : shaders.proceduralHits) {
            const std::uint32_t closestHit = addStage(hit.closestHit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
            groups.push_back(proceduralHitGroup(
                closestHit, addStage(hit.intersection, VK_SHADER_STAGE_INTERSECTION_BIT_KHR)));
        }

        missGroups_ = static_cast<std::uint32_t>(shaders.misses.size());
        triangleGroups_ = static_cast<std::uint32_t>(shaders.closestHits.size());
        proceduralGroups_ = static_cast<std::uint32_t>(shaders.proceduralHits.size());
        VkRayTracingPipelineCreateInfoKHR pipelineInfo{
            VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
        pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.groupCount = static_cast<std::uint32_t>(groups.size());
        pipelineInfo.pGroups = groups.data();
        pipelineInfo.maxPipelineRayRecursionDepth = kMaxRecursionDepth; // only the raygen traces
        pipelineInfo.layout = layout_;
        vkCheck(vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, ctx.pipelineCache(), 1, &pipelineInfo,
                                               nullptr, &pipeline_),
                ("vkCreateRayTracingPipelinesKHR(" + name + ")").c_str());

        // The table: each group's handle at the start of its record, the rest zero.
        const std::uint32_t handleSize = properties.shaderGroupHandleSize;
        std::vector<std::byte> handles(groups.size() * handleSize);
        vkCheck(vkGetRayTracingShaderGroupHandlesKHR(device, pipeline_, 0, pipelineInfo.groupCount,
                                                     handles.size(), handles.data()),
                "vkGetRayTracingShaderGroupHandlesKHR");
        std::vector<std::byte> table(sbtLayout_.totalBytes);
        const auto place = [&](std::size_t group, VkDeviceSize offset) {
            std::memcpy(table.data() + offset, handles.data() + group * handleSize, handleSize);
        };
        place(0, sbtLayout_.raygen.offset);
        const std::size_t missCount = shaders.misses.size();
        for (std::size_t i = 0; i < missCount; ++i)
            place(1 + i, sbtLayout_.miss.offset + i * sbtLayout_.miss.stride);
        for (std::size_t k = 0; k < hitGroupCount(shaders); ++k)
            place(1 + missCount + k, sbtLayout_.hit.offset + k * sbtLayout_.hit.stride);

        BufferDesc sbtDesc{};
        sbtDesc.size = sbtLayout_.totalBytes;
        sbtDesc.usage =
            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        sbtDesc.name = "shader binding table";
        sbtDesc.alignment = properties.shaderGroupBaseAlignment;
        sbt_ = createDeviceBuffer(ctx, sbtDesc, table); // setup: an immediate submit
        sbtAddress_ = sbt_.address();
        // Every region's start must be on the base alignment; the layout keeps offsets on it, so the buffer's
        // start must be too (VMA honours the alignment, but a misaligned table only fails on some GPUs).
        if (sbtAddress_ % properties.shaderGroupBaseAlignment != 0)
            throw std::logic_error(
                "RayTracingPipeline: SBT address not aligned to shaderGroupBaseAlignment " +
                std::to_string(properties.shaderGroupBaseAlignment));
        raygenRegion_ = region(sbtAddress_, sbtLayout_.raygen);
        missRegion_ = region(sbtAddress_, sbtLayout_.miss);
        hitRegion_ = region(sbtAddress_, sbtLayout_.hit);
        callableRegion_ = {0, 0, 0};
    } catch (...) {
        destroy();
        throw;
    }
    ctx.setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(pipeline_), name.c_str());
}

RayTracingPipeline::~RayTracingPipeline() {
    destroy();
}

void RayTracingPipeline::destroy() noexcept {
    const VkDevice device = ctx_.device();
    if (pipeline_)
        vkDestroyPipeline(device, pipeline_, nullptr);
    if (layout_)
        vkDestroyPipelineLayout(device, layout_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
}

void RayTracingPipeline::bind(VkCommandBuffer cmd, FrameRing& ring, std::span<const DescriptorWrite> writes) {
    if (!ring.recording())
        throw std::logic_error("RayTracingPipeline::bind() outside FrameRing::begin()/submit()");
    if (setLayout_.bindings().empty()) {
        if (!writes.empty())
            throw std::invalid_argument("RayTracingPipeline::bind(): the pipeline has no bindings to write");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_);
        return;
    }
    // Allocated and written before anything is recorded, so a refused write records nothing.
    const VkDescriptorSet set = descriptors_.allocate(ring, setLayout_);
    writeDescriptors(ctx_, set, setLayout_, writes);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, layout_, 0, 1, &set, 0, nullptr);
}

void RayTracingPipeline::push(VkCommandBuffer cmd, const void* data, std::uint32_t bytes) {
    if (bytes > pushConstantBytes_ || bytes % 4 != 0)
        throw std::invalid_argument("RayTracingPipeline::push() of " + std::to_string(bytes) +
                                    " bytes into a range of " + std::to_string(pushConstantBytes_) +
                                    " (a multiple of 4 is required)");
    if (bytes == 0)
        return; // a zero-sized vkCmdPushConstants is invalid
    vkCmdPushConstants(cmd, layout_, kRtStages, 0, bytes, data);
}

void RayTracingPipeline::traceRays(VkCommandBuffer cmd, std::uint32_t width, std::uint32_t height) {
    vkCmdTraceRaysKHR(cmd, &raygenRegion_, &missRegion_, &hitRegion_, &callableRegion_, width, height, 1);
}

RayTracingPipeline::StackSizes RayTracingPipeline::stackSizes() const {
    StackSizes sizes;
    const VkDevice device = ctx_.device();
    const auto size = [&](std::uint32_t group, VkShaderGroupShaderKHR shader) {
        return vkGetRayTracingShaderGroupStackSizeKHR(device, pipeline_, group, shader);
    };
    sizes.raygen = size(0, VK_SHADER_GROUP_SHADER_GENERAL_KHR);
    for (std::uint32_t i = 0; i < missGroups_; ++i)
        sizes.miss = std::max(sizes.miss, size(1 + i, VK_SHADER_GROUP_SHADER_GENERAL_KHR));
    for (std::uint32_t k = 0; k < triangleGroups_ + proceduralGroups_; ++k) {
        const std::uint32_t group = 1 + missGroups_ + k;
        sizes.closestHit = std::max(sizes.closestHit, size(group, VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR));
        if (k >= triangleGroups_)
            sizes.intersection =
                std::max(sizes.intersection, size(group, VK_SHADER_GROUP_SHADER_INTERSECTION_KHR));
    }
    return sizes;
}

} // namespace evo::gfx
