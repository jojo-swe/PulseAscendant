#include "rt/particles.hpp"

#include "gfx/paths.hpp"
#include "gfx/vk_check.hpp"
#include "rt/gpu_types.hpp"

#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace evo::rt {

namespace {

using gfx::vkCheck;

// particles_common.glsl's push constants.
struct ParticlePush {
    std::uint32_t count;
    float seconds, previousSeconds;
    std::uint32_t material;
    float params[ParticleField::kParams];
};
static_assert(sizeof(ParticlePush) == 80);

const std::array<gfx::Binding, 2> kBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, // SphereRecord[]
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, // VkAabbPositionsKHR[] (6 floats each)
};

constexpr VkPipelineStageFlags2 kBuild = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
constexpr VkPipelineStageFlags2 kCompute = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags2 kTracers = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | kCompute;
constexpr VkAccessFlags2 kAsRead = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
constexpr VkAccessFlags2 kAsWrite = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

struct Access {
    VkPipelineStageFlags2 srcStage;
    VkAccessFlags2 srcAccess;
    VkPipelineStageFlags2 dstStage;
    VkAccessFlags2 dstAccess;
};
void memoryBarriers(VkCommandBuffer cmd, std::initializer_list<Access> accesses) {
    std::vector<VkMemoryBarrier2> barriers;
    for (const Access& a : accesses) {
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = a.srcStage;
        barrier.srcAccessMask = a.srcAccess;
        barrier.dstStageMask = a.dstStage;
        barrier.dstAccessMask = a.dstAccess;
        barriers.push_back(barrier);
    }
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
    dependency.pMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(cmd, &dependency);
}

// The geometry of a slot's BLAS build: its AABBs, opaque.
VkAccelerationStructureGeometryKHR aabbGeometry(VkDeviceAddress aabbs) {
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.aabbs = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR};
    geometry.geometry.aabbs.data.deviceAddress = aabbs;
    geometry.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);
    return geometry;
}

VkAccelerationStructureBuildGeometryInfoKHR buildInfo(const VkAccelerationStructureGeometryKHR& geometry,
                                                      bool refit) {
    VkAccelerationStructureBuildGeometryInfoKHR build{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
                  (refit ? VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : 0u);
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    return build;
}

std::uint32_t checkedCapacity(std::uint32_t capacity) {
    if (capacity == 0)
        throw std::invalid_argument("ParticleField: capacity 0");
    return capacity;
}

} // namespace

ParticleField::ParticleField(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t capacity,
                             const std::filesystem::path& shader, bool refit)
    : ctx_(ctx), ring_(ring), capacity_(checkedCapacity(capacity)), refit_(refit) {
    setShader(shader);
    slots_ = createSlots(capacity_);
    ringAlive_ = ring.onBegin([](std::uint32_t) {});
}

ParticleField::~ParticleField() {
    // A frame in flight may still build or trace a slot's BLAS. A ring that is already gone waited itself.
    if (ringAlive_.active()) {
        try {
            ring_.waitAll(); // shutdown
        } catch (...) {
        }
    }
    destroySlots(slots_);
}

ParticleField::Slots ParticleField::createSlots(std::uint32_t capacity) const {
    // The build sizes for `capacity` AABBs; every build uses at most that many.
    const VkAccelerationStructureGeometryKHR geometry = aabbGeometry(0);
    const VkAccelerationStructureBuildGeometryInfoKHR build = buildInfo(geometry, refit_);
    VkAccelerationStructureBuildSizesInfoKHR sizes{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(ctx_.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                            &build, &capacity, &sizes);
    const VkDeviceSize scratchAlignment = ctx_.rayTracingProperties().minScratchOffsetAlignment;

    Slots slots;
    try {
        for (std::uint32_t i = 0; i < slots.size(); ++i) {
            Slot& slot = slots[i];
            const std::string suffix = " slot " + std::to_string(i);
            slot.records =
                gfx::Buffer(ctx_, {VkDeviceSize{capacity} * sizeof(SphereRecord),
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   false, ("particle spheres" + suffix).c_str()});
            gfx::BufferDesc aabbs{VkDeviceSize{capacity} * sizeof(VkAabbPositionsKHR),
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                  false, ("particle aabbs" + suffix).c_str()};
            aabbs.alignment = 16; // AABB build input: 8-byte aligned at least
            slot.aabbs = gfx::Buffer(ctx_, aabbs);
            slot.storage = gfx::Buffer(ctx_, {sizes.accelerationStructureSize,
                                              VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                              false, ("particle blas" + suffix).c_str()});
            gfx::BufferDesc scratch{
                std::max<VkDeviceSize>({sizes.buildScratchSize, refit_ ? sizes.updateScratchSize : 0, 1}),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false,
                ("particle blas scratch" + suffix).c_str()};
            scratch.alignment = scratchAlignment;
            slot.scratch = gfx::Buffer(ctx_, scratch);
            if (slot.scratch.address() % scratchAlignment != 0)
                throw std::logic_error("ParticleField: scratch not aligned to minScratchOffsetAlignment");

            VkAccelerationStructureCreateInfoKHR info{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
            info.buffer = slot.storage.handle();
            info.size = slot.storage.size();
            info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            vkCheck(vkCreateAccelerationStructureKHR(ctx_.device(), &info, nullptr, &slot.blas),
                    "vkCreateAccelerationStructureKHR(particles)");
            ctx_.setObjectName(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR,
                               reinterpret_cast<std::uint64_t>(slot.blas),
                               ("particle blas" + suffix).c_str());
            VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
            addressInfo.accelerationStructure = slot.blas;
            slot.address = vkGetAccelerationStructureDeviceAddressKHR(ctx_.device(), &addressInfo);
        }
    } catch (...) {
        for (Slot& slot : slots)
            if (slot.blas)
                vkDestroyAccelerationStructureKHR(ctx_.device(), slot.blas, nullptr);
        throw;
    }
    return slots;
}

void ParticleField::destroySlots(Slots& slots) noexcept {
    for (Slot& slot : slots) {
        if (slot.blas)
            vkDestroyAccelerationStructureKHR(ctx_.device(), slot.blas, nullptr);
        slot = Slot{};
    }
}

void ParticleField::reserve(std::uint32_t capacity) {
    if (capacity <= capacity_)
        return;
    // A queue idle, as PathTracer::reserve(): frames in flight may still use the slots, and synchronization
    // validation credits only this kind of wait when their memory is reused.
    vkQueueWaitIdle(ctx_.queue()); // setup
    Slots grown = createSlots(capacity);
    destroySlots(slots_);
    slots_ = std::move(grown);
    capacity_ = capacity;
}

void ParticleField::setShader(const std::filesystem::path& shader) {
    auto it = pipelines_.find(shader);
    if (it == pipelines_.end()) {
        const std::filesystem::path spv = shader.is_absolute() ? shader : gfx::shaderDir() / shader;
        it = pipelines_
                 .emplace(shader, std::make_unique<gfx::ComputePipeline>(ctx_, spv, kBindings,
                                                                         std::uint32_t(sizeof(ParticlePush))))
                 .first;
    }
    current_ = it->second.get();
}

void ParticleField::record(VkCommandBuffer cmd, std::uint32_t count, double seconds, double previousSeconds,
                           std::span<const float, kParams> params, std::uint32_t material) {
    if (!ringAlive_.active() || !ring_.recording())
        throw std::logic_error("ParticleField::record() outside its ring's begin() and submit()");
    if (count > capacity_)
        throw std::length_error("ParticleField::record(): " + std::to_string(count) +
                                " particles, more than the field's capacity of " + std::to_string(capacity_) +
                                " (reserve() it)");
    Slot& slot = slots_[ring_.slot()];

    // The reads of this slot's buffers and BLAS so far (an earlier slice's traces and TLAS build in this
    // frame; a copy of the records) finish before they are written again. An earlier frame on this slot
    // completed before begin() returned.
    memoryBarriers(cmd, {{kTracers, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | kAsRead, kCompute,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT},
                         {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, kCompute,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT},
                         {kBuild, VK_ACCESS_2_SHADER_READ_BIT | kAsRead | kAsWrite, kCompute,
                          VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT},
                         {kTracers | kBuild, kAsRead | kAsWrite, kBuild, kAsRead | kAsWrite}});

    if (count > 0) {
        ParticlePush push{
            count, static_cast<float>(seconds), static_cast<float>(previousSeconds), material, {}};
        std::copy(params.begin(), params.end(), push.params);
        const std::array<VkDescriptorBufferInfo, 2> buffers = {
            VkDescriptorBufferInfo{slot.records.handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{slot.aabbs.handle(), 0, VK_WHOLE_SIZE}};
        current_->bind(cmd, ring_, {}, buffers);
        current_->push(cmd, &push, sizeof push);
        current_->dispatch(cmd, count, 1, kLocalSize, 1);
    }
    // The AABBs feed the build, the records the hit shaders (and ray queries) of this frame.
    memoryBarriers(
        cmd, {{kCompute, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, kBuild, VK_ACCESS_2_SHADER_READ_BIT | kAsRead},
              {kCompute, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, kTracers | VK_PIPELINE_STAGE_2_COPY_BIT,
               VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT}});

    const VkAccelerationStructureGeometryKHR geometry = aabbGeometry(slot.aabbs.address());
    VkAccelerationStructureBuildGeometryInfoKHR build = buildInfo(geometry, refit_);
    // refit: update the slot's last build in place while it holds the same particles a moment earlier.
    const double since = seconds - slot.seconds;
    const bool update = refit_ && slot.updatable && count > 0 && count == slot.count &&
                        slot.refits < kRefitFrames && since > 0.0 && since < kRefitSeconds;
    if (update) {
        build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        build.srcAccelerationStructure = slot.blas;
    }
    build.dstAccelerationStructure = slot.blas;
    build.scratchData.deviceAddress = slot.scratch.address();
    const VkAccelerationStructureBuildRangeInfoKHR range{count, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* rangePointer = &range;
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &rangePointer);
    slot.count = count;
    slot.updatable = refit_ && count > 0;
    slot.refits = update ? slot.refits + 1 : 0;
    slot.seconds = seconds;
    refitsRecorded_ += update ? 1u : 0u;

    // The TLAS build references the BLAS, and the traces walk it.
    memoryBarriers(cmd, {{kBuild, kAsWrite, kBuild | kTracers, kAsRead}});
}

VkDeviceAddress ParticleField::blasAddress() const {
    return slots_[ring_.slot()].address;
}

const gfx::Buffer& ParticleField::records() const {
    return slots_[ring_.slot()].records;
}

std::uint32_t ParticleField::count() const {
    return slots_[ring_.slot()].count;
}

VkDeviceSize ParticleField::bytes() const {
    VkDeviceSize bytes = 0;
    for (const Slot& slot : slots_)
        bytes += slot.records.bytes() + slot.aabbs.bytes() + slot.storage.bytes() + slot.scratch.bytes();
    return bytes;
}

} // namespace evo::rt
