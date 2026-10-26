#include "render/scene_raster.hpp"

#include "gfx/paths.hpp"
#include "gfx/sync.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace evo::render {

namespace {

// Must match the push_constant blocks of shaders/retro/polys.comp and points.comp.
struct PolysPushConstants {
    std::uint32_t count;
};
struct PointsPushConstants {
    std::uint32_t count;
    std::uint32_t pass; // 0 claims the pixels, 1 writes them
};

constexpr std::array<gfx::Binding, 2> kPolysBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
};
constexpr std::array<gfx::Binding, 3> kPointsBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
};

constexpr std::uint32_t kWidth = retro::kScreenWidth, kHeight = retro::kScreenHeight;
// A draw's buffer: room for the most triangles, then for the most points (offsets well aligned: 64 KiB).
constexpr VkDeviceSize kTriangleBytes = retro::kMaxTriangles * sizeof(retro::RetroTriangle);
constexpr VkDeviceSize kPointBytes = retro::kMaxPoints * sizeof(retro::RetroPoint);
constexpr VkDeviceSize kOrderBytes = VkDeviceSize{kWidth} * kHeight * sizeof(std::uint32_t);
constexpr std::uint32_t kPointGroup = 64; // points.comp's local_size_x

void validate(const gfx::Image& image, const retro::RetroScene& scene) {
    if (image.format() != VK_FORMAT_R8_UINT || image.extent().width != kWidth ||
        image.extent().height != kHeight)
        throw std::invalid_argument("SceneRaster: the target must be the 320x200 R8_UINT index image");
    try {
        retro::checkScene(scene);
    } catch (const std::length_error& error) {
        throw std::length_error(std::string("SceneRaster: ") + error.what());
    } catch (const std::invalid_argument& error) {
        throw std::invalid_argument(std::string("SceneRaster: ") + error.what());
    }
}

} // namespace

SceneRaster::SceneRaster(gfx::Context& ctx, gfx::FrameRing& ring)
    : ctx_(ctx), ring_(ring),
      polys_(ctx, gfx::shaderDir() / "retro_polys.comp.spv", kPolysBindings, sizeof(PolysPushConstants)),
      points_(ctx, gfx::shaderDir() / "retro_points.comp.spv", kPointsBindings, sizeof(PointsPushConstants)),
      order_(ctx, {kOrderBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false,
                   "retro point order"}),
      // begin() runs this once the slot's previous frame is done: its buffers are free to rewrite.
      subscription_(ring.onBegin([this](std::uint32_t slot) { slots_[slot].used = 0; })) {}

SceneRaster::~SceneRaster() {
    // Frames in flight may still read the primitive buffers. Best effort, as in ~Renderer.
    try {
        ring_.waitAll(); // shutdown
    } catch (const std::exception&) {
    }
}

void SceneRaster::draw(VkCommandBuffer cmd, gfx::Image& indexImage, const retro::RetroScene& scene) {
    if (!ring_.recording())
        throw std::logic_error("SceneRaster: draw outside FrameRing::begin()/submit()");
    validate(indexImage, scene);
    if (scene.triangles.empty() && scene.points.empty())
        return;

    // This draw's buffer in the slot being recorded.
    SlotBuffers& slot = slots_[ring_.slot()];
    if (slot.used == slot.buffers.size())
        slot.buffers.emplace_back(ctx_, gfx::BufferDesc{kTriangleBytes + kPointBytes,
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true,
                                                        "retro scene primitives"});
    const gfx::Buffer& buffer = slot.buffers[slot.used++];
    auto* bytes = static_cast<std::uint8_t*>(buffer.mapped());
    // Host writes before the submit are visible to the GPU through the submit itself.
    if (!scene.triangles.empty())
        std::memcpy(bytes, scene.triangles.data(), scene.triangles.size() * sizeof(retro::RetroTriangle));
    if (!scene.points.empty())
        std::memcpy(bytes + kTriangleBytes, scene.points.data(),
                    scene.points.size() * sizeof(retro::RetroPoint));
    buffer.flush();

    const VkDescriptorImageInfo image{VK_NULL_HANDLE, indexImage.view(), VK_IMAGE_LAYOUT_GENERAL};

    // Triangles: each pixel reads what is there (glenz ORs into it) and writes the result.
    if (!scene.triangles.empty()) {
        indexImage.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                              VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const VkDescriptorBufferInfo triangles{buffer.handle(), 0, kTriangleBytes};
        polys_.bind(cmd, ring_, {&image, 1}, {&triangles, 1});
        const PolysPushConstants push{static_cast<std::uint32_t>(scene.triangles.size())};
        polys_.push(cmd, &push, sizeof push);
        polys_.dispatch(cmd, kWidth, kHeight);
    }

    // Points: the order buffer cleared (after any earlier draw's points are done with it), the pixels claimed
    // by the last point on them, then written by it.
    if (!scene.points.empty()) {
        gfx::bufferBarrier2(cmd, order_.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT);
        vkCmdFillBuffer(cmd, order_.handle(), 0, VK_WHOLE_SIZE, 0);
        gfx::bufferBarrier2(cmd, order_.handle(), VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        // The image is bound in GENERAL for both passes; pass 1 writes it after the triangles.
        indexImage.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        const std::array<VkDescriptorBufferInfo, 2> buffers = {
            VkDescriptorBufferInfo{buffer.handle(), kTriangleBytes, kPointBytes},
            VkDescriptorBufferInfo{order_.handle(), 0, kOrderBytes}};
        points_.bind(cmd, ring_, {&image, 1}, buffers);
        const auto count = static_cast<std::uint32_t>(scene.points.size());
        const PointsPushConstants claim{count, 0};
        points_.push(cmd, &claim, sizeof claim);
        points_.dispatch(cmd, count, 1, kPointGroup, 1);
        gfx::bufferBarrier2(cmd, order_.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
        const PointsPushConstants write{count, 1};
        points_.push(cmd, &write, sizeof write);
        points_.dispatch(cmd, count, 1, kPointGroup, 1);
    }
}

} // namespace evo::render
