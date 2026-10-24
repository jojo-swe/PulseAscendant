#include "gfx/offscreen.hpp"

#include "gfx/paths.hpp"
#include "gfx/shader.hpp"
#include "gfx/vk_check.hpp"

#include <stdexcept>
#include <string>

namespace evo::gfx {

namespace {

constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr std::uint32_t kGroupSize = 8; // local_size_x/y in fill.comp

// Must match the push_constant block in fill.comp (32 bytes).
struct FillPushConstants {
    std::uint32_t width;
    std::uint32_t height;
    float localSeconds;
    float era;
    float kick;
    float beatPhase;
    float wipe;
    // Bits 0-15: the chapter index; bits 16-31: the show::Transition, which the shader needs to tell a Wipe
    // (split at the wipe edge) and a Portal (cross-fade) from the chapters that switch on era.
    std::uint32_t chapter;
};
static_assert(sizeof(FillPushConstants) == 32);

FillPushConstants pushConstants(const show::TimelineFrame& frame, std::uint32_t width, std::uint32_t height) {
    const auto chapter = static_cast<std::uint32_t>(frame.chapter) & 0xffffu;
    const auto transition = static_cast<std::uint32_t>(frame.transition);
    return {width,
            height,
            static_cast<float>(frame.localSeconds),
            frame.era,
            frame.kick,
            frame.beatPhase,
            frame.wipe,
            chapter | (transition << 16)};
}

const VkImageSubresourceRange kColorRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = kColorRange;

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace

Offscreen::Offscreen(Context& ctx, int width, int height) : ctx_(ctx), width_(width), height_(height) {
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("Offscreen size must be positive, got " + std::to_string(width) + "x" +
                                    std::to_string(height));
    try {
        createTargets();
        createPipeline();
    } catch (...) {
        destroy();
        throw;
    }
}

Offscreen::~Offscreen() {
    destroy();
}

void Offscreen::createTargets() {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = kFormat;
    imageInfo.extent = {static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imageAlloc{};
    imageAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vkCheck(vmaCreateImage(ctx_.allocator(), &imageInfo, &imageAlloc, &image_, &imageAllocation_, nullptr),
            "vmaCreateImage");

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kFormat;
    viewInfo.subresourceRange = kColorRange;
    vkCheck(vkCreateImageView(ctx_.device(), &viewInfo, nullptr, &view_), "vkCreateImageView");

    // Host-visible, mapped once for the object's lifetime.
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = static_cast<VkDeviceSize>(width_) * static_cast<VkDeviceSize>(height_) * 4;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo bufferAlloc{};
    bufferAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    bufferAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo allocationInfo{};
    vkCheck(vmaCreateBuffer(ctx_.allocator(), &bufferInfo, &bufferAlloc, &readbackBuffer_,
                            &readbackAllocation_, &allocationInfo),
            "vmaCreateBuffer(readback)");
    mapped_ = static_cast<const std::uint8_t*>(allocationInfo.pMappedData);
    if (!mapped_)
        throw std::runtime_error("readback buffer is not host-mapped");
}

void Offscreen::createPipeline() {
    const VkDevice device = ctx_.device();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo setLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutInfo.bindingCount = 1;
    setLayoutInfo.pBindings = &binding;
    vkCheck(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &setLayout_),
            "vkCreateDescriptorSetLayout");

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    vkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_), "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    setInfo.descriptorPool = descriptorPool_;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &setLayout_;
    vkCheck(vkAllocateDescriptorSets(device, &setInfo, &descriptorSet_), "vkAllocateDescriptorSets");

    // The shader only ever sees the image in GENERAL layout.
    VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, view_, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FillPushConstants)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    vkCheck(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout_), "vkCreatePipelineLayout");

    VkShaderModule module = loadShaderModule(device, shaderDir() / "fill.comp.spv");
    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout_;
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
    vkDestroyShaderModule(device, module, nullptr);
    vkCheck(result, "vkCreateComputePipelines(fill.comp)");
}

void Offscreen::destroy() noexcept {
    const VkDevice device = ctx_.device();
    if (pipeline_)
        vkDestroyPipeline(device, pipeline_, nullptr);
    if (pipelineLayout_)
        vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    if (descriptorPool_)
        vkDestroyDescriptorPool(device, descriptorPool_, nullptr); // frees descriptorSet_
    if (setLayout_)
        vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
    if (readbackBuffer_)
        vmaDestroyBuffer(ctx_.allocator(), readbackBuffer_, readbackAllocation_);
    if (view_)
        vkDestroyImageView(device, view_, nullptr);
    if (image_)
        vmaDestroyImage(ctx_.allocator(), image_, imageAllocation_);

    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    descriptorSet_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    readbackBuffer_ = VK_NULL_HANDLE;
    readbackAllocation_ = VK_NULL_HANDLE;
    mapped_ = nullptr;
    view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    imageAllocation_ = VK_NULL_HANDLE;
}

void Offscreen::render(double seconds) {
    render(show::sampleTimeline(seconds));
}

void Offscreen::render(const show::TimelineFrame& frame) {
    const auto width = static_cast<std::uint32_t>(width_);
    const auto height = static_cast<std::uint32_t>(height_);
    const FillPushConstants push = pushConstants(frame, width, height);

    ctx_.immediate([&](VkCommandBuffer cmd) {
        // The shader overwrites every pixel, so the previous contents are discarded (oldLayout UNDEFINED).
        // Waiting on all transfer stages orders this write after the previous frame's copy or blit.
        imageBarrier(cmd, image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                     VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout_, 0, 1, &descriptorSet_,
                                0, nullptr);
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, (width + kGroupSize - 1) / kGroupSize, (height + kGroupSize - 1) / kGroupSize, 1);

        imageBarrier(cmd, image_, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0; // tightly packed
        region.bufferImageHeight = 0;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer_, 1,
                               &region);

        // Make the copied bytes available to host reads after the submit completes.
        VkBufferMemoryBarrier2 toHost{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        toHost.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.buffer = readbackBuffer_;
        toHost.offset = 0;
        toHost.size = VK_WHOLE_SIZE;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.bufferMemoryBarrierCount = 1;
        dependency.pBufferMemoryBarriers = &toHost;
        vkCmdPipelineBarrier2(cmd, &dependency);
    });
    layout_ = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

Frame Offscreen::readback() const {
    // A no-op on HOST_COHERENT memory; required otherwise before the CPU reads what the GPU wrote.
    vkCheck(vmaInvalidateAllocation(ctx_.allocator(), readbackAllocation_, 0, VK_WHOLE_SIZE),
            "vmaInvalidateAllocation(readback)");

    Frame frame;
    frame.width = width_;
    frame.height = height_;
    const std::size_t pixels = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    frame.rgb.resize(pixels * 3);
    for (std::size_t i = 0; i < pixels; ++i) {
        frame.rgb[i * 3 + 0] = mapped_[i * 4 + 0];
        frame.rgb[i * 3 + 1] = mapped_[i * 4 + 1];
        frame.rgb[i * 3 + 2] = mapped_[i * 4 + 2];
    }
    return frame;
}

} // namespace evo::gfx
