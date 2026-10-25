#include "modern/splats.hpp"

#include "gfx/paths.hpp"
#include "modern/scene.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

namespace evo::modern {

namespace {

// Must match the push_constant block of shaders/modern/splat_common.glsl (128 bytes, the Vulkan minimum of
// maxPushConstantsSize).
struct SplatPush {
    float cameraPosition[3];
    float tanHalfFovY;
    float cameraForward[3];
    float aspect;
    float cameraRight[3];
    float seconds;
    std::uint32_t count, width;
    std::uint32_t heightAndTaps; // the height, and the footprint's half-width above bit 16 (0: 5x5)
    float fixedPointScale;
    float params[16];
};
static_assert(sizeof(SplatPush) == 128);

// Must match the push_constant block of shaders/modern/splat_resolve.comp (16 bytes).
struct ResolvePush {
    std::uint32_t width, height;
    float fixedPointScale;
    float areaScale; // (W * H) / (1920 * 1080): reference units to radiance at this size
};
static_assert(sizeof(ResolvePush) == 16);

// The splat shaders: the three accumulators, the overflow bits and the depth guide. The resolve: the same
// four and the emission guide.
constexpr std::array<gfx::Binding, 5> kBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, gfx::Binding{3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
};

constexpr std::uint32_t kSplatGroupSize = 256; // local_size_x of splat_common.glsl

std::uint32_t checkedSize(std::uint32_t size) {
    if (size == 0)
        throw std::invalid_argument("SplatPass: an empty size");
    return size;
}

bool validScale(float scale) {
    return std::isfinite(scale) && scale > 0.f;
}

void store(float out[3], Vec3 v) {
    out[0] = v.x;
    out[1] = v.y;
    out[2] = v.z;
}

void require(const gfx::Image& image, VkFormat format, std::uint32_t width, std::uint32_t height,
             const char* what) {
    if (image.format() != format || image.viewFormat() != format)
        throw std::invalid_argument(std::string("SplatPass: the ") + what + " image has format " +
                                    std::to_string(image.viewFormat()) + ", expected " +
                                    std::to_string(format));
    if (image.extent().width != width || image.extent().height != height)
        throw std::invalid_argument(std::string("SplatPass: the ") + what + " image is " +
                                    std::to_string(image.extent().width) + "x" +
                                    std::to_string(image.extent().height) + ", the pass " +
                                    std::to_string(width) + "x" + std::to_string(height));
}

VkDescriptorImageInfo storage(const gfx::Image& image) {
    return {VK_NULL_HANDLE, image.view(), VK_IMAGE_LAYOUT_GENERAL};
}

} // namespace

void checkSplats(const std::optional<SplatFrame>& splats, std::uint32_t capacity,
                 const std::filesystem::path& shader) {
    if (!splats)
        return;
    if (shader.empty())
        throw std::invalid_argument("the frame has splats, but the scene has no splat shader");
    if (splats->count > capacity)
        throw std::length_error(std::to_string(splats->count) +
                                " splats, more than the scene's capacity of " + std::to_string(capacity));
    if (!validScale(splats->fixedPointScale))
        throw std::invalid_argument("the splats' fixedPointScale must be finite and > 0, got " +
                                    std::to_string(splats->fixedPointScale));
    if (splats->windowFootprint != 3 && splats->windowFootprint != 5)
        throw std::invalid_argument("the splats' windowFootprint must be 3 or 5, got " +
                                    std::to_string(splats->windowFootprint));
    for (std::size_t i = 0; i < splats->params.size(); ++i)
        if (!std::isfinite(splats->params[i]))
            throw std::invalid_argument("the splats' params[" + std::to_string(i) + "] is not finite");
}

SplatPass::SplatPass(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height)
    : ctx_(ctx), ring_(ring), width_(checkedSize(width)), height_(checkedSize(height)),
      resolve_(ctx, gfx::shaderDir() / "modern_splat_resolve.comp.spv", kBindings, sizeof(ResolvePush)) {
    createImages();
}

SplatPass::~SplatPass() = default;

void SplatPass::createImages() {
    // Transfer: the clear every frame (and tests read them back).
    constexpr VkImageUsageFlags usage =
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const char* names[3] = {"splat red", "splat green", "splat blue"};
    for (std::size_t c = 0; c < 3; ++c)
        accumulators_[c] = gfx::Image(ctx_, {width_, height_, VK_FORMAT_R32_UINT, usage, 1, names[c]});
    overflow_ = gfx::Image(ctx_, {width_, height_, VK_FORMAT_R32_UINT, usage, 1, "splat overflow"});
}

void SplatPass::resize(std::uint32_t width, std::uint32_t height) {
    width_ = checkedSize(width);
    height_ = checkedSize(height);
    createImages();
}

VkDeviceSize SplatPass::imageBytes() const {
    VkDeviceSize bytes = overflow_.bytes();
    for (const gfx::Image& image : accumulators_)
        bytes += image.bytes();
    return bytes;
}

gfx::ComputePipeline& SplatPass::pipeline(const std::filesystem::path& shader) {
    const std::filesystem::path file = shader.is_absolute() ? shader : gfx::shaderDir() / shader;
    auto found = pipelines_.find(file);
    if (found == pipelines_.end()) {
        auto made = std::make_unique<gfx::ComputePipeline>(ctx_, file, kBindings, sizeof(SplatPush));
        found = pipelines_.emplace(file, std::move(made)).first;
    }
    return *found->second;
}

void SplatPass::prepare(const std::filesystem::path& shader) {
    pipeline(shader);
}

void SplatPass::record(VkCommandBuffer cmd, const std::filesystem::path& shader, const SplatFrame& splats,
                       const Camera& camera, gfx::Image& depth, gfx::Image& emission, double seconds,
                       std::uint32_t footprint) {
    if (footprint != 3 && footprint != 5)
        throw std::invalid_argument("SplatPass: a footprint of " + std::to_string(footprint) +
                                    " pixels, not 3 or 5");
    if (height_ > 0xFFFFu)
        throw std::invalid_argument("SplatPass: a height over 65535");
    if (splats.count > kMaxSplats)
        throw std::length_error("SplatPass: " + std::to_string(splats.count) + " splats, more than " +
                                std::to_string(kMaxSplats) + " in one dispatch");
    if (!validScale(splats.fixedPointScale))
        throw std::invalid_argument("SplatPass: the fixedPointScale must be finite and > 0, got " +
                                    std::to_string(splats.fixedPointScale));
    require(depth, VK_FORMAT_R32_SFLOAT, width_, height_, "depth");
    require(emission, VK_FORMAT_R16G16B16A16_SFLOAT, width_, height_, "emission");
    if (splats.count == 0)
        return;
    gfx::ComputePipeline& splat = pipeline(shader); // may throw: nothing is recorded yet

    // Clear the accumulators: nothing carries over from another frame.
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VkClearColorValue zero{};
    const std::array<gfx::Image*, 4> images = {&accumulators_[0], &accumulators_[1], &accumulators_[2],
                                               &overflow_};
    for (gfx::Image* image : images) {
        image->transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdClearColorImage(cmd, image->handle(), VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
    }

    // The points: integer atomics into the accumulators, depth-tested against the guide.
    for (gfx::Image* image : images)
        image->transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    depth.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    const std::array<VkDescriptorImageInfo, 5> splatImages = {
        storage(accumulators_[0]), storage(accumulators_[1]), storage(accumulators_[2]), storage(overflow_),
        storage(depth)};
    splat.bind(cmd, ring_, splatImages, {});
    const CameraBasis basis = cameraBasis(camera);
    SplatPush push{};
    store(push.cameraPosition, camera.eye);
    store(push.cameraForward, basis.forward);
    store(push.cameraRight, basis.right);
    push.tanHalfFovY = std::tan(camera.fovYDegrees * std::numbers::pi_v<float> / 360.f);
    push.aspect = static_cast<float>(width_) / static_cast<float>(height_);
    push.seconds = static_cast<float>(seconds);
    push.count = splats.count;
    push.width = width_;
    push.heightAndTaps = height_ | (footprint == 3 ? 1u << 16 : 0u);
    push.fixedPointScale = splats.fixedPointScale;
    std::copy(splats.params.begin(), splats.params.end(), push.params);
    splat.push(cmd, &push, sizeof push);
    const std::uint32_t rows = (splats.count + kSplatRowWidth - 1) / kSplatRowWidth;
    splat.dispatch(cmd, std::min(splats.count, kSplatRowWidth), rows, kSplatGroupSize, 1);

    // The resolve: the sums into the emission guide.
    for (gfx::Image* image : images)
        image->transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                          VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    emission.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const std::array<VkDescriptorImageInfo, 5> resolveImages = {
        storage(accumulators_[0]), storage(accumulators_[1]), storage(accumulators_[2]), storage(overflow_),
        storage(emission)};
    resolve_.bind(cmd, ring_, resolveImages, {});
    const ResolvePush resolvePush{width_, height_, splats.fixedPointScale,
                                  static_cast<float>(double(width_) * height_ / kSplatReferencePixels)};
    resolve_.push(cmd, &resolvePush, sizeof resolvePush);
    resolve_.dispatch(cmd, width_, height_);
}

} // namespace evo::modern
