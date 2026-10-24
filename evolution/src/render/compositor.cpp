#include "render/compositor.hpp"

#include "gfx/paths.hpp"

#include <array>

namespace evo::render {

namespace {

// Must match the push_constant block of shaders/crt.comp and shaders/composite.comp (32 bytes).
struct CompositorPushConstants {
    std::uint32_t width;
    std::uint32_t height;
    float era;
    float wipe;
    float eraCut;
    std::uint32_t transition; // a show::Transition value
    std::uint32_t crt;        // 0 = plain pillarbox
    float localSeconds;
};
static_assert(sizeof(CompositorPushConstants) == 32);

constexpr std::array<gfx::Binding, 2> kCrtBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
};
constexpr std::array<gfx::Binding, 3> kCompositeBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
};

gfx::ImageDesc screenDesc(std::uint32_t width, std::uint32_t height) {
    return {width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT, 1, "retro screen"};
}

gfx::ImageDesc outputDesc(std::uint32_t width, std::uint32_t height) {
    return {width,
            height,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            1,
            "renderer output"};
}

VkDescriptorImageInfo storage(const gfx::Image& image) {
    return {VK_NULL_HANDLE, image.view(), VK_IMAGE_LAYOUT_GENERAL};
}

void prepareRead(VkCommandBuffer cmd, gfx::Image& image) {
    image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

void prepareWrite(VkCommandBuffer cmd, gfx::Image& image) {
    image.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

} // namespace

Compositor::Compositor(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height)
    : ctx_(ctx), ring_(ring),
      crt_(ctx, gfx::shaderDir() / "crt.comp.spv", kCrtBindings, sizeof(CompositorPushConstants)),
      composite_(ctx, gfx::shaderDir() / "composite.comp.spv", kCompositeBindings,
                 sizeof(CompositorPushConstants)),
      screen_(ctx, screenDesc(width, height)), output_(ctx, outputDesc(width, height)) {}

void Compositor::resize(std::uint32_t width, std::uint32_t height) {
    screen_ = gfx::Image(ctx_, screenDesc(width, height));
    output_ = gfx::Image(ctx_, outputDesc(width, height));
}

void Compositor::render(VkCommandBuffer cmd, gfx::Image& retro, gfx::Image& modern,
                        const show::TimelineFrame& frame, bool crt) {
    const VkExtent2D size = output_.extent();
    const CompositorPushConstants push{size.width,    size.height,
                                       frame.era,     frame.wipe,
                                       frame.eraCut,  static_cast<std::uint32_t>(frame.transition),
                                       crt ? 1u : 0u, static_cast<float>(frame.localSeconds)};

    prepareRead(cmd, retro);
    prepareWrite(cmd, screen_);
    const std::array<VkDescriptorImageInfo, 2> crtImages = {storage(retro), storage(screen_)};
    crt_.bind(cmd, ring_, crtImages, {});
    crt_.push(cmd, &push, sizeof push);
    crt_.dispatch(cmd, size.width, size.height);

    prepareRead(cmd, screen_);
    prepareRead(cmd, modern);
    prepareWrite(cmd, output_);
    const std::array<VkDescriptorImageInfo, 3> compositeImages = {storage(screen_), storage(modern),
                                                                  storage(output_)};
    composite_.bind(cmd, ring_, compositeImages, {});
    composite_.push(cmd, &push, sizeof push);
    composite_.dispatch(cmd, size.width, size.height);
}

} // namespace evo::render
