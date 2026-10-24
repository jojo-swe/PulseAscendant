#pragma once

#include "gfx/context.hpp"

#include <volk.h>

#include <vk_mem_alloc.h>

namespace evo::gfx {

struct BufferDesc {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    bool hostVisible = false; // mapped for the buffer's lifetime (uploads, readbacks); else device-local
    const char* name = "";    // shown by the validation layer and debuggers
};

// A VMA buffer. A host-visible buffer is persistently mapped and may be non-coherent: flush() after the CPU
// writes it and invalidate() before the CPU reads what the GPU wrote.
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

} // namespace evo::gfx
