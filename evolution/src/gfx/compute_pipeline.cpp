#include "gfx/compute_pipeline.hpp"

#include "gfx/shader.hpp"
#include "gfx/vk_check.hpp"

#include <map>
#include <stdexcept>
#include <string>

namespace evo::gfx {

namespace {

constexpr std::uint32_t kSetsPerPool = 64;

bool isImageDescriptor(VkDescriptorType type) {
    switch (type) {
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return true;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return false;
    default:
        throw std::invalid_argument("ComputePipeline: unsupported descriptor type " +
                                    std::to_string(static_cast<int>(type)));
    }
}

} // namespace

ComputePipeline::ComputePipeline(Context& ctx, const std::filesystem::path& spv,
                                 std::span<const Binding> bindings, std::uint32_t pushConstantBytes)
    : ctx_(ctx), bindings_(bindings.begin(), bindings.end()), pushConstantBytes_(pushConstantBytes) {
    const VkDevice device = ctx.device();
    const std::string name = spv.filename().string();
    try {
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
        std::map<VkDescriptorType, std::uint32_t> perType;
        for (const Binding& binding : bindings_) {
            isImageDescriptor(binding.type); // rejects unsupported types
            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = binding.binding;
            layoutBinding.descriptorType = binding.type;
            layoutBinding.descriptorCount = 1;
            layoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            layoutBindings.push_back(layoutBinding);
            ++perType[binding.type];
        }
        for (const auto& [type, count] : perType)
            poolSizes_.push_back({type, count * kSetsPerPool});

        VkDescriptorSetLayoutCreateInfo setLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setLayoutInfo.bindingCount = static_cast<std::uint32_t>(layoutBindings.size());
        setLayoutInfo.pBindings = layoutBindings.data();
        vkCheck(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &setLayout_),
                "vkCreateDescriptorSetLayout");

        VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantBytes};
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &setLayout_;
        layoutInfo.pushConstantRangeCount = pushConstantBytes > 0 ? 1 : 0;
        layoutInfo.pPushConstantRanges = pushConstantBytes > 0 ? &pushRange : nullptr;
        vkCheck(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout_), "vkCreatePipelineLayout");

        VkShaderModule module = loadShaderModule(device, spv);
        VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = module;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = layout_;
        const VkResult result =
            vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
        vkDestroyShaderModule(device, module, nullptr);
        vkCheck(result, ("vkCreateComputePipelines(" + name + ")").c_str());
    } catch (...) {
        destroy();
        throw;
    }
    ctx.setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(pipeline_), name.c_str());
}

ComputePipeline::~ComputePipeline() {
    destroy();
}

void ComputePipeline::destroy() noexcept {
    subscription_ = {}; // the ring must not call back into a half-destroyed pipeline
    const VkDevice device = ctx_.device();
    for (SlotPools& slot : slots_) {
        for (VkDescriptorPool pool : slot.pools)
            vkDestroyDescriptorPool(device, pool, nullptr);
        slot = {};
    }
    if (pipeline_)
        vkDestroyPipeline(device, pipeline_, nullptr);
    if (layout_)
        vkDestroyPipelineLayout(device, layout_, nullptr);
    if (setLayout_)
        vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
}

void ComputePipeline::attach(FrameRing& ring) {
    if (subscription_.belongsTo(ring))
        return;
    if (subscription_.active())
        throw std::logic_error("a ComputePipeline is bound with one FrameRing only");
    // A ring this pipeline was bound with before is gone, and it waited for its submits: every set is free.
    for (std::uint32_t slot = 0; slot < FrameRing::kFramesInFlight; ++slot)
        resetSlot(slot);
    subscription_ = ring.onBegin([this](std::uint32_t slot) { resetSlot(slot); });
}

void ComputePipeline::resetSlot(std::uint32_t slot) {
    SlotPools& pools = slots_[slot];
    for (VkDescriptorPool pool : pools.pools)
        vkCheck(vkResetDescriptorPool(ctx_.device(), pool, 0), "vkResetDescriptorPool");
    pools.current = 0;
    pools.usedInCurrent = 0;
}

VkDescriptorPool ComputePipeline::createPool() {
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = kSetsPerPool;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes_.size());
    poolInfo.pPoolSizes = poolSizes_.data();
    VkDescriptorPool pool = VK_NULL_HANDLE;
    vkCheck(vkCreateDescriptorPool(ctx_.device(), &poolInfo, nullptr, &pool), "vkCreateDescriptorPool");
    return pool;
}

VkDescriptorSet ComputePipeline::allocateSet(std::uint32_t slot) {
    SlotPools& pools = slots_[slot];
    for (;;) {
        if (pools.usedInCurrent == kSetsPerPool) {
            ++pools.current;
            pools.usedInCurrent = 0;
        }
        if (pools.current == pools.pools.size())
            pools.pools.push_back(createPool());

        VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setInfo.descriptorPool = pools.pools[pools.current];
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &setLayout_;
        VkDescriptorSet set = VK_NULL_HANDLE;
        const VkResult result = vkAllocateDescriptorSets(ctx_.device(), &setInfo, &set);
        if (result == VK_SUCCESS) {
            ++pools.usedInCurrent;
            return set;
        }
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
            vkCheck(result, "vkAllocateDescriptorSets");
        pools.usedInCurrent = kSetsPerPool; // full for the rest of the frame
    }
}

void ComputePipeline::bind(VkCommandBuffer cmd, FrameRing& ring,
                           std::span<const VkDescriptorImageInfo> images,
                           std::span<const VkDescriptorBufferInfo> buffers) {
    if (!ring.recording())
        throw std::logic_error("ComputePipeline::bind() outside FrameRing::begin()/submit()");

    std::size_t imageCount = 0, bufferCount = 0;
    for (const Binding& binding : bindings_)
        ++(isImageDescriptor(binding.type) ? imageCount : bufferCount);
    if (images.size() != imageCount || buffers.size() != bufferCount)
        throw std::invalid_argument("ComputePipeline::bind() needs " + std::to_string(imageCount) +
                                    " images and " + std::to_string(bufferCount) + " buffers, got " +
                                    std::to_string(images.size()) + " and " + std::to_string(buffers.size()));

    attach(ring);
    const VkDescriptorSet set = allocateSet(ring.slot());

    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(bindings_.size());
    std::size_t nextImage = 0, nextBuffer = 0;
    for (const Binding& binding : bindings_) {
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = binding.binding;
        write.descriptorCount = 1;
        write.descriptorType = binding.type;
        if (isImageDescriptor(binding.type))
            write.pImageInfo = &images[nextImage++];
        else
            write.pBufferInfo = &buffers[nextBuffer++];
        writes.push_back(write);
    }
    vkUpdateDescriptorSets(ctx_.device(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0,
                           nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set, 0, nullptr);
}

void ComputePipeline::push(VkCommandBuffer cmd, const void* data, std::uint32_t bytes) {
    if (bytes > pushConstantBytes_)
        throw std::invalid_argument("ComputePipeline::push() of " + std::to_string(bytes) +
                                    " bytes into a range of " + std::to_string(pushConstantBytes_));
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, bytes, data);
}

void ComputePipeline::dispatch(VkCommandBuffer cmd, std::uint32_t width, std::uint32_t height,
                               std::uint32_t localX, std::uint32_t localY) {
    vkCmdDispatch(cmd, (width + localX - 1) / localX, (height + localY - 1) / localY, 1);
}

std::size_t ComputePipeline::descriptorPoolCount() const {
    std::size_t count = 0;
    for (const SlotPools& slot : slots_)
        count += slot.pools.size();
    return count;
}

} // namespace evo::gfx
