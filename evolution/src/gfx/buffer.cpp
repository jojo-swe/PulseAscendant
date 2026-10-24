#include "gfx/buffer.hpp"

#include "gfx/vk_check.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace evo::gfx {

Buffer::Buffer(Context& ctx, const BufferDesc& desc) : ctx_(&ctx), size_(desc.size), usage_(desc.usage) {
    if (desc.size == 0)
        throw std::invalid_argument("Buffer '" + std::string(desc.name) + "' needs a positive size");

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
    vkCheck(vmaCreateBuffer(ctx.allocator(), &bufferInfo, &allocInfo, &buffer_, &allocation_, &allocation),
            "vmaCreateBuffer");
    mapped_ = allocation.pMappedData;
    if (desc.hostVisible && !mapped_) {
        destroy();
        throw std::runtime_error("Buffer '" + std::string(desc.name) + "' is not host-mapped");
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

} // namespace evo::gfx
