#include "gfx/buffer.hpp"

#include "gfx/sync.hpp"
#include "gfx/vk_check.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace evo::gfx {

Buffer::Buffer(Context& ctx, const BufferDesc& desc) : ctx_(&ctx), size_(desc.size), usage_(desc.usage) {
    if (desc.size == 0)
        throw std::invalid_argument("Buffer '" + std::string(desc.name) + "' needs a positive size");
    if (desc.alignment & (desc.alignment - 1))
        throw std::invalid_argument("Buffer '" + std::string(desc.name) + "': alignment " +
                                    std::to_string(desc.alignment) + " is not a power of two");

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = desc.size;
    bufferInfo.usage = desc.usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    if (desc.hostVisible) {
        // Random access, because the same kind of buffer serves uploads and readbacks.
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }
    VmaAllocationInfo allocation{};
    if (desc.alignment > 0)
        vkCheck(vmaCreateBufferWithAlignment(ctx.allocator(), &bufferInfo, &allocInfo, desc.alignment,
                                             &buffer_, &allocation_, &allocation),
                "vmaCreateBufferWithAlignment");
    else
        vkCheck(
            vmaCreateBuffer(ctx.allocator(), &bufferInfo, &allocInfo, &buffer_, &allocation_, &allocation),
            "vmaCreateBuffer");
    mapped_ = allocation.pMappedData;
    if (desc.hostVisible && !mapped_) {
        destroy();
        throw std::runtime_error("Buffer '" + std::string(desc.name) + "' is not host-mapped");
    }

    // The promise the alignment exists for: acceleration structure inputs and scratch at aligned addresses.
    if (desc.alignment > 0 && (desc.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) &&
        address() % desc.alignment != 0) {
        const VkDeviceAddress misaligned = address();
        destroy();
        throw std::runtime_error("Buffer '" + std::string(desc.name) + "': device address " +
                                 std::to_string(misaligned) + " is not aligned to " +
                                 std::to_string(desc.alignment));
    }

    vmaSetAllocationName(ctx.allocator(), allocation_, desc.name);
    ctx.setObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(buffer_), desc.name);
}

Buffer::~Buffer() {
    destroy();
}

Buffer::Buffer(Buffer&& other) noexcept
    : ctx_(std::exchange(other.ctx_, nullptr)), buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)), size_(std::exchange(other.size_, 0)),
      usage_(std::exchange(other.usage_, 0)), mapped_(std::exchange(other.mapped_, nullptr)) {}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        destroy();
        ctx_ = std::exchange(other.ctx_, nullptr);
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        size_ = std::exchange(other.size_, 0);
        usage_ = std::exchange(other.usage_, 0);
        mapped_ = std::exchange(other.mapped_, nullptr);
    }
    return *this;
}

void Buffer::destroy() noexcept {
    if (ctx_ && buffer_)
        vmaDestroyBuffer(ctx_->allocator(), buffer_, allocation_);
    ctx_ = nullptr;
    buffer_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    mapped_ = nullptr;
}

void Buffer::flush() const {
    // A no-op on HOST_COHERENT memory.
    vkCheck(vmaFlushAllocation(ctx_->allocator(), allocation_, 0, VK_WHOLE_SIZE), "vmaFlushAllocation");
}

void Buffer::invalidate() const {
    vkCheck(vmaInvalidateAllocation(ctx_->allocator(), allocation_, 0, VK_WHOLE_SIZE),
            "vmaInvalidateAllocation");
}

VkDeviceAddress Buffer::address() const {
    if (!(usage_ & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))
        throw std::logic_error("Buffer::address() needs VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT");
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = buffer_;
    return vkGetBufferDeviceAddress(ctx_->device(), &info);
}

Buffer createDeviceBuffer(Context& ctx, const BufferDesc& desc, std::span<const std::byte> data) {
    if (desc.hostVisible)
        throw std::invalid_argument("createDeviceBuffer('" + std::string(desc.name) +
                                    "'): the buffer must be device-local");
    if (data.size() > desc.size)
        throw std::invalid_argument("createDeviceBuffer('" + std::string(desc.name) +
                                    "'): " + std::to_string(data.size()) + " bytes do not fit in " +
                                    std::to_string(desc.size));
    BufferDesc deviceDesc = desc;
    deviceDesc.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Buffer buffer(ctx, deviceDesc);
    if (data.empty())
        return buffer;

    const std::string stagingName = "staging for " + std::string(desc.name);
    Buffer staging(ctx, {data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, stagingName.c_str()});
    std::memcpy(staging.mapped(), data.data(), data.size());
    staging.flush();
    ctx.immediate([&](VkCommandBuffer cmd) { // setup
        const VkBufferCopy region{0, 0, data.size()};
        vkCmdCopyBuffer(cmd, staging.handle(), buffer.handle(), 1, &region);
        // Every later command, in this submit or any after it, sees the copied bytes.
        bufferBarrier2(cmd, buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                       VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    });
    return buffer;
}

} // namespace evo::gfx
