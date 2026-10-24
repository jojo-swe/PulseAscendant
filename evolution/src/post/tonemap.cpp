#include "post/tonemap.hpp"

#include "gfx/paths.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace evo::post {

namespace {

// Must match the push_constant block of shaders/post/tonemap.comp (12 bytes).
struct TonemapPush {
    std::uint32_t width;
    std::uint32_t height;
    float exposureScale; // 2^exposureEV
};
static_assert(sizeof(TonemapPush) == 12);

constexpr std::array<gfx::Binding, 2> kBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
};

void require(const gfx::Image& image, VkFormat format, const char* what) {
    if (image.format() != format || image.viewFormat() != format)
        throw std::invalid_argument(std::string("Tonemap: the ") + what + " image has format " +
                                    std::to_string(image.viewFormat()) + ", expected " +
                                    std::to_string(format));
}

} // namespace

Tonemap::Tonemap(gfx::Context& ctx, gfx::FrameRing& ring)
    : ring_(ring),
      pipeline_(ctx, gfx::shaderDir() / "post_tonemap.comp.spv", kBindings, sizeof(TonemapPush)) {}

void Tonemap::record(VkCommandBuffer cmd, gfx::Image& hdr, gfx::Image& ldr, float exposureEV) {
    require(hdr, VK_FORMAT_R16G16B16A16_SFLOAT, "HDR");
    require(ldr, VK_FORMAT_R8G8B8A8_UNORM, "LDR");
    const VkExtent2D size = ldr.extent();
    if (hdr.extent().width != size.width || hdr.extent().height != size.height)
        throw std::invalid_argument("Tonemap: the HDR image is " + std::to_string(hdr.extent().width) + "x" +
                                    std::to_string(hdr.extent().height) + ", the LDR image " +
                                    std::to_string(size.width) + "x" + std::to_string(size.height));

    hdr.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    ldr.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const std::array<VkDescriptorImageInfo, 2> images = {
        VkDescriptorImageInfo{VK_NULL_HANDLE, hdr.view(), VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{VK_NULL_HANDLE, ldr.view(), VK_IMAGE_LAYOUT_GENERAL},
    };
    pipeline_.bind(cmd, ring_, images, {});
    const TonemapPush push{size.width, size.height, std::exp2(exposureEV)};
    pipeline_.push(cmd, &push, sizeof push);
    pipeline_.dispatch(cmd, size.width, size.height);
}

} // namespace evo::post
