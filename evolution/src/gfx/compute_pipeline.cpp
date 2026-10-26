#include "gfx/compute_pipeline.hpp"

#include "gfx/shader.hpp"
#include "gfx/vk_check.hpp"

#include <stdexcept>
#include <string>

namespace evo::gfx {

namespace {

bool isImageDescriptor(VkDescriptorType type) {
    return type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
           type == VK_DESCRIPTOR_TYPE_SAMPLER || type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

bool isBufferDescriptor(VkDescriptorType type) {
    return type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

std::uint32_t checkedPushConstantBytes(std::uint32_t bytes) {
    if (bytes % 4 != 0)
        throw std::invalid_argument("ComputePipeline: push constant size " + std::to_string(bytes) +
                                    " is not a multiple of 4");
    return bytes;
}

} // namespace

ComputePipeline::ComputePipeline(Context& ctx, const std::filesystem::path& spv,
                                 std::span<const Binding> bindings, std::uint32_t pushConstantBytes)
    : ctx_(ctx), pushConstantBytes_(checkedPushConstantBytes(pushConstantBytes)),
      setLayout_(ctx, bindings, VK_SHADER_STAGE_COMPUTE_BIT), descriptors_(ctx) {
    const VkDevice device = ctx.device();
    const std::string name = spv.filename().string();
    try {
        const VkDescriptorSetLayout setLayout = setLayout_.handle();
        VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushConstantBytes};
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &setLayout;
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
            vkCreateComputePipelines(device, ctx.pipelineCache(), 1, &pipelineInfo, nullptr, &pipeline_);
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
    const VkDevice device = ctx_.device();
    if (pipeline_)
        vkDestroyPipeline(device, pipeline_, nullptr);
    if (layout_)
        vkDestroyPipelineLayout(device, layout_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
}

void ComputePipeline::bind(VkCommandBuffer cmd, FrameRing& ring, std::span<const DescriptorWrite> writes) {
    if (!ring.recording())
        throw std::logic_error("ComputePipeline::bind() outside FrameRing::begin()/submit()");
    if (setLayout_.bindings().empty()) {
        if (!writes.empty())
            throw std::invalid_argument("ComputePipeline::bind(): the pipeline has no bindings to write");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
        return; // nothing to bind: no set, and no pool to allocate it from
    }
    // Allocated and written before anything is recorded, so a refused write records nothing.
    const VkDescriptorSet set = descriptors_.allocate(ring, setLayout_);
    writeDescriptors(ctx_, set, setLayout_, writes);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set, 0, nullptr);
}

void ComputePipeline::bind(VkCommandBuffer cmd, FrameRing& ring,
                           std::span<const VkDescriptorImageInfo> images,
                           std::span<const VkDescriptorBufferInfo> buffers) {
    std::size_t imageCount = 0, bufferCount = 0;
    for (const Binding& binding : setLayout_.bindings()) {
        if (isImageDescriptor(binding.type))
            imageCount += binding.count;
        else if (isBufferDescriptor(binding.type))
            bufferCount += binding.count;
        else
            throw std::invalid_argument("ComputePipeline::bind(): binding " +
                                        std::to_string(binding.binding) + " needs a DescriptorWrite");
    }
    if (images.size() != imageCount || buffers.size() != bufferCount)
        throw std::invalid_argument("ComputePipeline::bind() needs " + std::to_string(imageCount) +
                                    " images and " + std::to_string(bufferCount) + " buffers, got " +
                                    std::to_string(images.size()) + " and " + std::to_string(buffers.size()));

    std::vector<DescriptorWrite> writes;
    writes.reserve(setLayout_.bindings().size());
    std::size_t nextImage = 0, nextBuffer = 0;
    for (const Binding& binding : setLayout_.bindings()) {
        DescriptorWrite& write = writes.emplace_back();
        write.binding = binding.binding;
        if (isImageDescriptor(binding.type)) {
            write.images.assign(images.begin() + nextImage, images.begin() + nextImage + binding.count);
            nextImage += binding.count;
        } else {
            write.buffers.assign(buffers.begin() + nextBuffer, buffers.begin() + nextBuffer + binding.count);
            nextBuffer += binding.count;
        }
    }
    bind(cmd, ring, writes);
}

void ComputePipeline::push(VkCommandBuffer cmd, const void* data, std::uint32_t bytes) {
    if (bytes > pushConstantBytes_ || bytes % 4 != 0)
        throw std::invalid_argument("ComputePipeline::push() of " + std::to_string(bytes) +
                                    " bytes into a range of " + std::to_string(pushConstantBytes_) +
                                    " (a multiple of 4 is required)");
    if (bytes == 0)
        return; // a zero-sized vkCmdPushConstants is invalid
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, bytes, data);
}

void ComputePipeline::dispatch(VkCommandBuffer cmd, std::uint32_t width, std::uint32_t height,
                               std::uint32_t localX, std::uint32_t localY) {
    vkCmdDispatch(cmd, (width + localX - 1) / localX, (height + localY - 1) / localY, 1);
    ++dispatches_;
}

} // namespace evo::gfx
