#include "modern/medium_volume.hpp"

#include "gfx/paths.hpp"
#include "gfx/sync.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace evo::modern {

namespace {

// medium_common.glsl's push constants (scalar).
struct BakePush {
    std::uint32_t size[3];
    float seconds;
    float params[MediumVolume::kParams];
};
static_assert(sizeof(BakePush) == 80);

// medium_majorant.comp's push constants.
struct MajorantPush {
    std::uint32_t size[3];
    std::uint32_t pairs;
    std::uint32_t cells[3];
    std::uint32_t pad;
};
static_assert(sizeof(MajorantPush) == 32);

const std::array<gfx::Binding, 1> kBakeBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, // the density image
};
const std::array<gfx::Binding, 2> kMajorantBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},  // the density image (read)
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, // the packed majorant halves
};

constexpr VkPipelineStageFlags2 kCompute = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
constexpr std::uint32_t kMajorantLocalSize = 64;

std::array<std::uint32_t, 3> checkedSize(std::array<std::uint32_t, 3> size) {
    for (std::uint32_t n : size)
        if (n == 0 || n > kMaxMediumGrid)
            throw std::invalid_argument("MediumVolume: a size of " + std::to_string(n) +
                                        " voxels on an axis (1 to " + std::to_string(kMaxMediumGrid) + ")");
    return size;
}

std::uint32_t cellCount(std::array<std::uint32_t, 3> cells) {
    return cells[0] * cells[1] * cells[2];
}

gfx::Image image3D(gfx::Context& ctx, std::array<std::uint32_t, 3> size, VkFormat format,
                   VkImageUsageFlags usage, const char* name) {
    gfx::ImageDesc desc{size[0], size[1], format, usage, 1, name};
    desc.viewType = VK_IMAGE_VIEW_TYPE_3D;
    desc.depth = size[2];
    return gfx::Image(ctx, desc);
}

} // namespace

MediumVolume::MediumVolume(gfx::Context& ctx, gfx::FrameRing& ring, std::array<std::uint32_t, 3> maxSize,
                           const std::filesystem::path& shader)
    : ctx_(ctx), ring_(ring), capacity_(checkedSize(maxSize)) {
    setShader(shader);
    majorantPipeline_ =
        std::make_unique<gfx::ComputePipeline>(ctx_, gfx::shaderDir() / "modern_medium_majorant.comp.spv",
                                               kMajorantBindings, std::uint32_t(sizeof(MajorantPush)));
    Resources r = createResources(capacity_);
    density_ = std::move(r.density);
    majorant_ = std::move(r.majorant);
    majorantWords_ = std::move(r.majorantWords);
    ringAlive_ = ring.onBegin([](std::uint32_t) {});
}

MediumVolume::~MediumVolume() {
    // A frame in flight may still bake or sample the images. A ring that is already gone waited itself.
    if (ringAlive_.active()) {
        try {
            ring_.waitAll(); // shutdown
        } catch (...) {
        }
    }
}

MediumVolume::Resources MediumVolume::createResources(std::array<std::uint32_t, 3> size) const {
    const std::array<std::uint32_t, 3> cells = majorantSize(size);
    Resources r;
    r.density =
        image3D(ctx_, size, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                "medium density");
    r.majorant = image3D(ctx_, cells, VK_FORMAT_R16_SFLOAT,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                         "medium majorant");
    const VkDeviceSize words = (VkDeviceSize{cellCount(cells)} + 1) / 2;
    r.majorantWords =
        gfx::Buffer(ctx_, {words * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           false, "medium majorant words"});
    return r;
}

void MediumVolume::reserve(std::array<std::uint32_t, 3> maxSize) {
    checkedSize(maxSize);
    const std::array<std::uint32_t, 3> grown = {std::max(capacity_[0], maxSize[0]),
                                                std::max(capacity_[1], maxSize[1]),
                                                std::max(capacity_[2], maxSize[2])};
    if (grown == capacity_)
        return;
    // A queue idle, as PathTracer::reserve(): frames in flight may still use the images, and synchronization
    // validation credits only this kind of wait when their memory is reused.
    vkQueueWaitIdle(ctx_.queue()); // setup
    Resources r = createResources(grown);
    density_ = std::move(r.density);
    majorant_ = std::move(r.majorant);
    majorantWords_ = std::move(r.majorantWords);
    capacity_ = grown;
}

void MediumVolume::setShader(const std::filesystem::path& shader) {
    auto it = pipelines_.find(shader);
    if (it == pipelines_.end()) {
        const std::filesystem::path spv = shader.is_absolute() ? shader : gfx::shaderDir() / shader;
        it = pipelines_
                 .emplace(shader, std::make_unique<gfx::ComputePipeline>(ctx_, spv, kBakeBindings,
                                                                         std::uint32_t(sizeof(BakePush))))
                 .first;
    }
    current_ = it->second.get();
}

void MediumVolume::record(VkCommandBuffer cmd, const GridMediumDesc& desc, double seconds) {
    if (!ringAlive_.active() || !ring_.recording())
        throw std::logic_error("MediumVolume::record() outside its ring's begin() and submit()");
    const std::array<std::uint32_t, 3>& size = desc.size;
    for (std::uint32_t n : size)
        if (n == 0)
            throw std::invalid_argument("MediumVolume::record(): a grid of 0 voxels on an axis");
    for (int a = 0; a < 3; ++a)
        if (size[a] > capacity_[a])
            throw std::length_error("MediumVolume::record(): a grid of " + std::to_string(size[0]) + "x" +
                                    std::to_string(size[1]) + "x" + std::to_string(size[2]) +
                                    " voxels, more than the volume's capacity of " +
                                    std::to_string(capacity_[0]) + "x" + std::to_string(capacity_[1]) + "x" +
                                    std::to_string(capacity_[2]) + " (reserve() it)");

    // The bake: every voxel of the grid (the reads of the last frame's trace and majorant pass finish first).
    density_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, kCompute, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    BakePush bake{{size[0], size[1], size[2]}, static_cast<float>(seconds), {}};
    std::copy(desc.params.begin(), desc.params.end(), bake.params);
    const std::array<VkDescriptorImageInfo, 1> bakeImages = {
        VkDescriptorImageInfo{VK_NULL_HANDLE, density_.view(), VK_IMAGE_LAYOUT_GENERAL}};
    current_->bind(cmd, ring_, bakeImages, {});
    current_->push(cmd, &bake, sizeof bake);
    current_->dispatch(cmd, size[0], size[1] * size[2]);

    // The majorant of each cell into the staging words (after the last frame's copy read them), then into
    // the majorant image.
    density_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, kCompute, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    gfx::bufferBarrier2(cmd, majorantWords_.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT, kCompute, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const std::array<std::uint32_t, 3> cells = majorantSize(size);
    const std::uint32_t pairs = (cellCount(cells) + 1) / 2;
    const MajorantPush majorant{{size[0], size[1], size[2]}, pairs, {cells[0], cells[1], cells[2]}, 0};
    const std::array<VkDescriptorImageInfo, 1> majorantImages = {
        VkDescriptorImageInfo{VK_NULL_HANDLE, density_.view(), VK_IMAGE_LAYOUT_GENERAL}};
    const std::array<VkDescriptorBufferInfo, 1> majorantBuffers = {
        VkDescriptorBufferInfo{majorantWords_.handle(), 0, VK_WHOLE_SIZE}};
    majorantPipeline_->bind(cmd, ring_, majorantImages, majorantBuffers);
    majorantPipeline_->push(cmd, &majorant, sizeof majorant);
    majorantPipeline_->dispatch(cmd, pairs, 1, kMajorantLocalSize, 1);
    gfx::bufferBarrier2(cmd, majorantWords_.handle(), kCompute, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    majorant_.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {cells[0], cells[1], cells[2]};
    vkCmdCopyBufferToImage(cmd, majorantWords_.handle(), majorant_.handle(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    ++bakes_;
}

VkDeviceSize MediumVolume::bytes() const {
    return density_.bytes() + majorant_.bytes() + majorantWords_.bytes();
}

} // namespace evo::modern
