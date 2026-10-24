#include "gfx/fill_pass.hpp"

#include "gfx/paths.hpp"

#include <array>
#include <cstdint>

namespace evo::gfx {

namespace {

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

FillPushConstants pushConstants(const FillParams& params, VkExtent2D extent) {
    return {extent.width,        extent.height,
            params.localSeconds, params.era,
            params.kick,         params.beatPhase,
            params.wipe,         (params.chapter & 0xffffu) | (params.transition << 16)};
}

constexpr std::array<Binding, 1> kBindings = {Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}};

} // namespace

FillPass::FillPass(Context& ctx)
    : pipeline_(ctx, shaderDir() / "fill.comp.spv", kBindings, sizeof(FillPushConstants)) {}

void FillPass::record(VkCommandBuffer cmd, FrameRing& ring, Image& target, const FillParams& params) {
    target.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, target.view(), VK_IMAGE_LAYOUT_GENERAL};
    pipeline_.bind(cmd, ring, {&image, 1}, {});
    const FillPushConstants push = pushConstants(params, target.extent());
    pipeline_.push(cmd, &push, sizeof push);
    pipeline_.dispatch(cmd, target.extent().width, target.extent().height);
}

} // namespace evo::gfx
