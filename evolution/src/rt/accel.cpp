#include "rt/accel.hpp"

#include "gfx/vk_check.hpp"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace evo::rt {

namespace {

using gfx::vkCheck;

// Where acceleration structures live, and the scratch their builds use.
constexpr VkBufferUsageFlags kStorageUsage =
    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
constexpr VkBufferUsageFlags kScratchUsage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

constexpr std::uint32_t kMax24Bits = (1u << 24) - 1;

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// An acceleration structure of `type` filling `buffer` from its start.
VkAccelerationStructureKHR createAccelerationStructure(const gfx::Context& ctx, const gfx::Buffer& buffer,
                                                       VkAccelerationStructureTypeKHR type,
                                                       const char* name) {
    VkAccelerationStructureCreateInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    info.buffer = buffer.handle();
    info.size = buffer.size();
    info.type = type;
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    vkCheck(vkCreateAccelerationStructureKHR(ctx.device(), &info, nullptr, &handle),
            "vkCreateAccelerationStructureKHR");
    ctx.setObjectName(VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, reinterpret_cast<std::uint64_t>(handle),
                      name);
    return handle;
}

// BufferDesc::alignment places the buffer's start; the device address must follow it.
VkDeviceAddress alignedAddress(const gfx::Buffer& buffer, VkDeviceSize alignment, const char* what) {
    const VkDeviceAddress address = buffer.address();
    if (address % alignment != 0)
        throw std::logic_error(std::string(what) + " at device address " + std::to_string(address) +
                               " is not aligned to " + std::to_string(alignment));
    return address;
}

VkDeviceAddress addressOf(const gfx::Context& ctx, VkAccelerationStructureKHR handle) {
    VkAccelerationStructureDeviceAddressInfoKHR info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    info.accelerationStructure = handle;
    return vkGetAccelerationStructureDeviceAddressKHR(ctx.device(), &info);
}

// Global memory barriers, recorded as one dependency.
struct Access {
    VkPipelineStageFlags2 srcStage;
    VkAccessFlags2 srcAccess;
    VkPipelineStageFlags2 dstStage;
    VkAccessFlags2 dstAccess;
};
void memoryBarriers(VkCommandBuffer cmd, std::initializer_list<Access> accesses) {
    std::vector<VkMemoryBarrier2> barriers;
    for (const Access& access : accesses) {
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = access.srcStage;
        barrier.srcAccessMask = access.srcAccess;
        barrier.dstStageMask = access.dstStage;
        barrier.dstAccessMask = access.dstAccess;
        barriers.push_back(barrier);
    }
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
    dependency.pMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(cmd, &dependency);
}

constexpr VkPipelineStageFlags2 kBuild = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
constexpr VkPipelineStageFlags2 kTracers =
    VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
constexpr VkAccessFlags2 kAsRead = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
constexpr VkAccessFlags2 kAsWrite = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

// A query pool for compacted sizes, destroyed with the scope.
class CompactedSizeQueries {
public:
    CompactedSizeQueries(const gfx::Context& ctx, std::uint32_t count) : ctx_(ctx) {
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        info.queryCount = count;
        vkCheck(vkCreateQueryPool(ctx.device(), &info, nullptr, &pool_), "vkCreateQueryPool(compacted size)");
    }
    ~CompactedSizeQueries() { vkDestroyQueryPool(ctx_.device(), pool_, nullptr); }
    CompactedSizeQueries(const CompactedSizeQueries&) = delete;
    CompactedSizeQueries& operator=(const CompactedSizeQueries&) = delete;
    VkQueryPool handle() const { return pool_; }

private:
    const gfx::Context& ctx_;
    VkQueryPool pool_ = VK_NULL_HANDLE;
};

} // namespace

// --- BlasSet ---------------------------------------------------------------------------------------------

BlasSet::BlasSet(gfx::Context& ctx, const GeometryStore& store, bool compact) : ctx_(ctx) {
    const auto records = store.records();
    const auto count = static_cast<std::uint32_t>(records.size());
    const VkDeviceAddress vertexBase = store.vertices().address();
    const VkDeviceAddress indexBase = store.indices().address();
    const VkDeviceSize scratchAlignment = ctx.rayTracingProperties().minScratchOffsetAlignment;

    std::vector<VkAccelerationStructureGeometryKHR> geometries(count);
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> builds(count);
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(count);
    std::vector<VkDeviceSize> scratchOffsets(count);
    std::vector<VkDeviceSize> asSizes(count);
    VkDeviceSize scratchBytes = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const MeshRecord& record = records[i];
        VkAccelerationStructureGeometryKHR& geometry = geometries[i];
        geometry = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        VkAccelerationStructureGeometryTrianglesDataKHR& triangles = geometry.geometry.triangles;
        triangles = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT; // Vertex::position
        triangles.vertexData.deviceAddress = vertexBase + VkDeviceSize{record.firstVertex} * sizeof(Vertex);
        triangles.vertexStride = sizeof(Vertex);
        triangles.maxVertex = record.vertexCount - 1;
        triangles.indexType = VK_INDEX_TYPE_UINT32; // local to the mesh, so relative to its first vertex
        triangles.indexData.deviceAddress =
            indexBase + VkDeviceSize{record.firstIndex} * sizeof(std::uint32_t);

        VkAccelerationStructureBuildGeometryInfoKHR& build = builds[i];
        build = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                      VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build.geometryCount = 1;
        build.pGeometries = &geometry;

        ranges[i] = {record.indexCount / 3, 0, 0, 0};
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(ctx.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &build, &ranges[i].primitiveCount, &sizes);
        asSizes[i] = sizes.accelerationStructureSize;
        // Builds in one command run together: each gets its own scratch range, on the device's alignment.
        scratchOffsets[i] = scratchBytes;
        scratchBytes = alignUp(scratchBytes + sizes.buildScratchSize, scratchAlignment);
        builtBytes_ += sizes.accelerationStructureSize;
    }

    std::vector<Blas> built(count);
    auto destroyBuilt = [&]() noexcept {
        for (Blas& blas : built)
            if (blas.handle)
                vkDestroyAccelerationStructureKHR(ctx.device(), blas.handle, nullptr);
        built.clear(); // frees the storage buffers
    };
    try {
        gfx::BufferDesc scratchDesc{scratchBytes, kScratchUsage, false, "blas build scratch"};
        scratchDesc.alignment = scratchAlignment;
        gfx::Buffer scratch(ctx, scratchDesc);
        const VkDeviceAddress scratchAddress = alignedAddress(scratch, scratchAlignment, "BLAS scratch");
        std::vector<VkAccelerationStructureKHR> handles(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::string name = "blas " + std::to_string(i) + (compact ? " (uncompacted)" : "");
            built[i].storage = gfx::Buffer(ctx, {asSizes[i], kStorageUsage, false, name.c_str()});
            built[i].handle = createAccelerationStructure(
                ctx, built[i].storage, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, name.c_str());
            handles[i] = built[i].handle;
            builds[i].dstAccelerationStructure = built[i].handle;
            builds[i].scratchData.deviceAddress = scratchAddress + scratchOffsets[i];
        }
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rangePointers(count);
        for (std::uint32_t i = 0; i < count; ++i)
            rangePointers[i] = &ranges[i];

        if (!compact) {
            ctx.immediate([&](VkCommandBuffer cmd) { // setup: every BLAS in one submit
                vkCmdBuildAccelerationStructuresKHR(cmd, count, builds.data(), rangePointers.data());
                // Every later trace and TLAS build, in this submit or any after it, sees the BLASes.
                memoryBarriers(cmd, {{kBuild, kAsWrite, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, kAsRead}});
            });
            for (Blas& blas : built)
                blas.address = addressOf(ctx, blas.handle);
            blases_ = std::move(built);
            compactedBytes_ = builtBytes_;
            return; // the scratch buffer goes now: the submit that used it has completed
        }

        CompactedSizeQueries queries(ctx, count);
        // Every BLAS in one submit, then their compacted sizes.
        ctx.immediate([&](VkCommandBuffer cmd) { // setup
            vkCmdResetQueryPool(cmd, queries.handle(), 0, count);
            vkCmdBuildAccelerationStructuresKHR(cmd, count, builds.data(), rangePointers.data());
            memoryBarriers(cmd, {{kBuild, kAsWrite, kBuild, kAsRead}});
            vkCmdWriteAccelerationStructuresPropertiesKHR(
                cmd, count, handles.data(), VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                queries.handle(), 0);
        });
        std::vector<std::uint64_t> compactedSizes(count);
        vkCheck(vkGetQueryPoolResults(ctx.device(), queries.handle(), 0, count,
                                      compactedSizes.size() * sizeof(std::uint64_t), compactedSizes.data(),
                                      sizeof(std::uint64_t),
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                "vkGetQueryPoolResults(compacted size)");

        std::vector<Blas> compacted(count);
        try {
            for (std::uint32_t i = 0; i < count; ++i) {
                if (compactedSizes[i] == 0 || compactedSizes[i] > asSizes[i])
                    throw std::runtime_error("BlasSet: the driver reported a compacted size of " +
                                             std::to_string(compactedSizes[i]) + " for " +
                                             std::to_string(asSizes[i]) + " bytes");
                const std::string name = "blas " + std::to_string(i);
                compacted[i].storage =
                    gfx::Buffer(ctx, {compactedSizes[i], kStorageUsage, false, name.c_str()});
                compacted[i].handle = createAccelerationStructure(
                    ctx, compacted[i].storage, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, name.c_str());
                compactedBytes_ += compactedSizes[i];
            }
            ctx.immediate([&](VkCommandBuffer cmd) { // setup: the compacted copies
                for (std::uint32_t i = 0; i < count; ++i) {
                    VkCopyAccelerationStructureInfoKHR copy{
                        VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
                    copy.src = built[i].handle;
                    copy.dst = compacted[i].handle;
                    copy.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
                    vkCmdCopyAccelerationStructureKHR(cmd, &copy);
                }
                // Copies run in the build stage. Every later trace and TLAS build sees the compacted BLASes.
                memoryBarriers(cmd, {{kBuild, kAsWrite, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, kAsRead}});
            });
        } catch (...) {
            for (Blas& blas : compacted)
                if (blas.handle)
                    vkDestroyAccelerationStructureKHR(ctx.device(), blas.handle, nullptr);
            throw;
        }
        // The copy has completed (immediate waited): only now do the originals go, with the scratch buffer
        // when this scope ends.
        destroyBuilt();
        for (Blas& blas : compacted)
            blas.address = addressOf(ctx, blas.handle);
        blases_ = std::move(compacted);
    } catch (...) {
        destroyBuilt();
        throw;
    }
}

BlasSet::~BlasSet() {
    destroy();
}

void BlasSet::destroy() noexcept {
    for (Blas& blas : blases_)
        if (blas.handle)
            vkDestroyAccelerationStructureKHR(ctx_.device(), blas.handle, nullptr);
    blases_.clear();
}

VkDeviceSize BlasSet::bytes() const {
    VkDeviceSize bytes = 0;
    for (const Blas& blas : blases_)
        bytes += blas.storage.bytes();
    return bytes;
}

VkDeviceAddress BlasSet::address(std::uint32_t mesh) const {
    if (mesh >= blases_.size())
        throw std::out_of_range("BlasSet: no mesh " + std::to_string(mesh) + " (" +
                                std::to_string(blases_.size()) + " meshes)");
    return blases_[mesh].address;
}

// --- Tlas ------------------------------------------------------------------------------------------------

namespace {

// The geometry of a TLAS build reading its instances (VkAccelerationStructureInstanceKHR) at `instances`.
VkAccelerationStructureGeometryKHR instanceGeometry(VkDeviceAddress instances) {
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = instances;
    return geometry;
}

VkAccelerationStructureBuildGeometryInfoKHR
tlasBuildInfo(const VkAccelerationStructureGeometryKHR& geometry) {
    VkAccelerationStructureBuildGeometryInfoKHR build{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    return build;
}

} // namespace

Tlas::Tlas(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t maxInstances)
    : ctx_(ctx), ring_(ring), maxInstances_(maxInstances) {
    if (maxInstances == 0)
        throw std::invalid_argument("Tlas: maxInstances must be positive");

    const VkAccelerationStructureGeometryKHR geometry = instanceGeometry(0);
    const VkAccelerationStructureBuildGeometryInfoKHR build = tlasBuildInfo(geometry);
    VkAccelerationStructureBuildSizesInfoKHR sizes{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(ctx.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                            &build, &maxInstances, &sizes);

    storage_ = gfx::Buffer(ctx, {sizes.accelerationStructureSize, kStorageUsage, false, "tlas"});
    gfx::BufferDesc scratchDesc{std::max<VkDeviceSize>(sizes.buildScratchSize, 1), kScratchUsage, false,
                                "tlas build scratch"};
    scratchDesc.alignment = ctx.rayTracingProperties().minScratchOffsetAlignment;
    scratch_ = gfx::Buffer(ctx, scratchDesc);
    alignedAddress(scratch_, scratchDesc.alignment, "TLAS scratch");
    for (std::uint32_t slot = 0; slot < instanceBuffers_.size(); ++slot) {
        const std::string name = "tlas instances slot " + std::to_string(slot);
        gfx::BufferDesc desc{VkDeviceSize{maxInstances} * sizeof(VkAccelerationStructureInstanceKHR),
                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                             true, name.c_str()};
        desc.alignment = 16; // instance data must start on 16 bytes
        instanceBuffers_[slot] = gfx::Buffer(ctx, desc);
        alignedAddress(instanceBuffers_[slot], desc.alignment, "TLAS instances");
    }
    tlas_ = createAccelerationStructure(ctx, storage_, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, "tlas");
    // Tells the destructor whether the ring is still there to wait for, and starts each frame unbuilt.
    ringAlive_ = ring.onBegin([this](std::uint32_t) { builtThisFrame_ = false; });
}

Tlas::~Tlas() {
    destroy();
}

void Tlas::destroy() noexcept {
    // A frame in flight may still trace the TLAS. A ring that is already gone waited for its submits itself.
    if (ringAlive_.active()) {
        try {
            ring_.waitAll(); // shutdown
        } catch (...) {
        }
    }
    if (tlas_)
        vkDestroyAccelerationStructureKHR(ctx_.device(), tlas_, nullptr);
    tlas_ = VK_NULL_HANDLE;
}

VkDeviceSize Tlas::bytes() const {
    VkDeviceSize bytes = storage_.bytes() + scratch_.bytes();
    for (const gfx::Buffer& buffer : instanceBuffers_)
        bytes += buffer.bytes();
    return bytes;
}

void Tlas::build(VkCommandBuffer cmd, const BlasSet& blases, std::span<const Instance> instances) {
    if (!ringAlive_.active() || !ring_.recording())
        throw std::logic_error("Tlas::build() outside its ring's begin() and submit()");
    if (builtThisFrame_)
        throw std::logic_error("Tlas::build(): a second build in one frame (its traces read the first)");
    if (instances.size() > maxInstances_)
        throw std::length_error("Tlas::build(): " + std::to_string(instances.size()) +
                                " instances for at most " + std::to_string(maxInstances_));

    std::vector<VkAccelerationStructureInstanceKHR> records(instances.size());
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const Instance& instance = instances[i];
        if (instance.customIndex > kMax24Bits || instance.hitGroup > kMax24Bits)
            throw std::invalid_argument("Tlas::build(): instance " + std::to_string(i) +
                                        " has a custom index or hit group over 24 bits");
        VkAccelerationStructureInstanceKHR& record = records[i];
        std::memcpy(&record.transform, &instance.transform, sizeof record.transform);
        record.instanceCustomIndex = instance.customIndex;
        record.mask = instance.mask;
        record.instanceShaderBindingTableRecordOffset = instance.hitGroup;
        record.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        record.accelerationStructureReference = blases.address(instance.mesh);
    }

    // Nothing below throws: from here on the frame has its build.
    builtThisFrame_ = true;

    // This slot's previous frame has completed (begin() waited for it), so its instances are free to write.
    const gfx::Buffer& instanceBuffer = instanceBuffers_[ring_.slot()];
    if (!records.empty()) {
        std::memcpy(instanceBuffer.mapped(), records.data(), records.size() * sizeof records[0]);
        instanceBuffer.flush();
    }

    // The previous frame may still trace this TLAS, and its build used the same scratch: both finish first.
    memoryBarriers(cmd, {{kTracers, kAsRead, kBuild, kAsRead | kAsWrite},
                         {kBuild, kAsWrite, kBuild, kAsRead | kAsWrite}});

    const VkAccelerationStructureGeometryKHR geometry = instanceGeometry(instanceBuffer.address());
    VkAccelerationStructureBuildGeometryInfoKHR build = tlasBuildInfo(geometry);
    build.dstAccelerationStructure = tlas_;
    build.scratchData.deviceAddress = scratch_.address();
    const VkAccelerationStructureBuildRangeInfoKHR range{static_cast<std::uint32_t>(records.size()), 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR* rangePointer = &range;
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &rangePointer);

    // This frame's traces see the new TLAS.
    memoryBarriers(cmd, {{kBuild, kAsWrite, kTracers, kAsRead}});
}

} // namespace evo::rt
