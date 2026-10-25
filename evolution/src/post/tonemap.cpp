#include "post/tonemap.hpp"

#include "gfx/paths.hpp"
#include "gfx/sync.hpp"
#include "post/bloom.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace evo::post {

namespace {

// Must match the push_constant block of shaders/post/tonemap.comp (56 bytes).
struct TonemapPush {
    std::uint32_t width;
    std::uint32_t height;
    float exposureScale; // 2^exposureEV
    float bloomWeight;
    float splitTone;
    float contrast;
    float shadowDesaturation;
    float grain;
    float liftR, liftG, liftB;
    std::uint32_t grainFrame;
    float vignette;
    float paletteWeight;
};
static_assert(sizeof(TonemapPush) == 56);

constexpr std::array<gfx::Binding, 4> kBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
};

void require(const gfx::Image& image, VkFormat format, const char* what) {
    if (image.format() != format || image.viewFormat() != format)
        throw std::invalid_argument(std::string("Tonemap: the ") + what + " image has format " +
                                    std::to_string(image.viewFormat()) + ", expected " +
                                    std::to_string(format));
}

} // namespace

Tonemap::Tonemap(gfx::Context& ctx, gfx::FrameRing& ring)
    : ctx_(ctx), ring_(ring),
      pipeline_(ctx, gfx::shaderDir() / "post_tonemap.comp.spv", kBindings, sizeof(TonemapPush)),
      dummyBloom_(ctx, {1, 1, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, 1, "tonemap no bloom"}),
      palette_(ctx, {sizeof(GradePaletteTable), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     false, "tonemap palette"}) {}

Tonemap::~Tonemap() = default;

gfx::Image& Tonemap::recordBloom(VkCommandBuffer cmd, gfx::Image& hdr, float anamorphic) {
    const VkExtent2D size = hdr.extent();
    if (!bloom_)
        bloom_ = std::make_unique<Bloom>(ctx_, ring_, size.width, size.height);
    else if (bloom_->image().extent().width != (size.width + 1) / 2 ||
             bloom_->image().extent().height != (size.height + 1) / 2)
        bloom_->resize(size.width, size.height);
    bloom_->record(cmd, hdr, anamorphic);
    return bloom_->image();
}

VkDeviceSize Tonemap::bloomBytes() const {
    return bloom_ ? bloom_->imageBytes() : 0;
}

void Tonemap::record(VkCommandBuffer cmd, gfx::Image& hdr, gfx::Image& ldr, float exposureEV) {
    ToneInputs inputs;
    inputs.exposureEV = exposureEV;
    record(cmd, hdr, ldr, inputs);
}

void Tonemap::record(VkCommandBuffer cmd, gfx::Image& hdr, gfx::Image& ldr, const ToneInputs& inputs) {
    require(hdr, VK_FORMAT_R16G16B16A16_SFLOAT, "HDR");
    require(ldr, VK_FORMAT_R8G8B8A8_UNORM, "LDR");
    const VkExtent2D size = ldr.extent();
    if (hdr.extent().width != size.width || hdr.extent().height != size.height)
        throw std::invalid_argument("Tonemap: the HDR image is " + std::to_string(hdr.extent().width) + "x" +
                                    std::to_string(hdr.extent().height) + ", the LDR image " +
                                    std::to_string(size.width) + "x" + std::to_string(size.height));
    const Grade& grade = inputs.grade;
    if (grade.palette != 0.f && inputs.palette == nullptr)
        throw std::invalid_argument("Tonemap: a palette weight without a palette");
    if (inputs.bloomWeight != 0.f) {
        if (inputs.bloom == nullptr)
            throw std::invalid_argument("Tonemap: a bloom weight without a bloom picture");
        require(*inputs.bloom, VK_FORMAT_R16G16B16A16_SFLOAT, "bloom");
        const VkExtent2D b = inputs.bloom->extent();
        if (b.width != (size.width + 1) / 2 || b.height != (size.height + 1) / 2)
            throw std::invalid_argument("Tonemap: the bloom picture is " + std::to_string(b.width) + "x" +
                                        std::to_string(b.height) + ", expected half of " +
                                        std::to_string(size.width) + "x" + std::to_string(size.height));
    }

    if (grade.palette != 0.f) {
        // The table goes into the command buffer, after every earlier tonemap's reads of it.
        const GradePaletteTable table = gradePaletteTable(*inputs.palette);
        if (paletteUsed_)
            gfx::bufferBarrier2(cmd, palette_.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                                VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdUpdateBuffer(cmd, palette_.handle(), 0, sizeof table, &table);
        gfx::bufferBarrier2(cmd, palette_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        paletteUsed_ = true;
    }

    gfx::Image& bloom = inputs.bloomWeight != 0.f ? *inputs.bloom : dummyBloom_;
    hdr.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    bloom.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    ldr.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const std::array<VkDescriptorImageInfo, 3> images = {
        VkDescriptorImageInfo{VK_NULL_HANDLE, hdr.view(), VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{VK_NULL_HANDLE, ldr.view(), VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{VK_NULL_HANDLE, bloom.view(), VK_IMAGE_LAYOUT_GENERAL},
    };
    const std::array<VkDescriptorBufferInfo, 1> buffers = {
        VkDescriptorBufferInfo{palette_.handle(), 0, VK_WHOLE_SIZE}};
    pipeline_.bind(cmd, ring_, images, buffers);
    const TonemapPush push{size.width,
                           size.height,
                           std::exp2(inputs.exposureEV),
                           inputs.bloomWeight,
                           grade.splitTone,
                           grade.contrast,
                           grade.shadowDesaturation,
                           grade.grain,
                           grade.lift[0],
                           grade.lift[1],
                           grade.lift[2],
                           grade.grainFrame,
                           grade.vignette,
                           grade.palette};
    pipeline_.push(cmd, &push, sizeof push);
    pipeline_.dispatch(cmd, size.width, size.height);
}

} // namespace evo::post
