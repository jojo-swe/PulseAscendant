#pragma once

#include "gfx/context.hpp"

#include <volk.h>

#include <vk_mem_alloc.h>

#include <cstddef>
#include <span>

namespace evo::gfx {

struct BufferDesc {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    bool hostVisible = false;   // mapped for the buffer's lifetime (uploads, readbacks); else device-local
    const char* name = "";      // shown by the validation layer and debuggers
    VkDeviceSize alignment = 0; // a power of two the buffer's start (and device address) is aligned to;
                                // 0 = what the buffer's memory requirements ask for
};

// A VMA buffer. Throws std::invalid_argument for a size of 0 or an alignment that is not a power of two. A
// host-visible buffer is persistently mapped and may be non-coherent: flush() after the CPU writes it and
// invalidate() before the CPU reads what the GPU wrote.
class Buffer {
public:
    Buffer() = default;
    Buffer(Context& ctx, const BufferDesc& desc);
    ~Buffer();
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    void* mapped() const { return mapped_; } // nullptr unless host-visible
    VkBuffer handle() const { return buffer_; }
    VkDeviceSize size() const { return size_; }
    void flush() const;
    void invalidate() const;
    // Needs VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT; throws std::logic_error otherwise.
    VkDeviceAddress address() const;

private:
    void destroy() noexcept;

    Context* ctx_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    VkBufferUsageFlags usage_ = 0;
    void* mapped_ = nullptr;
};

// A device-local buffer of `desc` (which must not be host-visible) filled with `data` from its start through
// a staging copy in an immediate submit, for setup work: the copy is complete and visible to every later
// command when this returns. Adds TRANSFER_DST to the usage. Throws std::invalid_argument for more data than
// desc.size or a host-visible desc.
Buffer createDeviceBuffer(Context& ctx, const BufferDesc& desc, std::span<const std::byte> data);

} // namespace evo::gfx
