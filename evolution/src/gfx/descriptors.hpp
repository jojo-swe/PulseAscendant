#pragma once

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"

#include <volk.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace evo::gfx {

// One binding of a descriptor set: `count` descriptors of one type (count > 1 is an array). Supported types:
// STORAGE_IMAGE, SAMPLED_IMAGE, SAMPLER, COMBINED_IMAGE_SAMPLER, STORAGE_BUFFER, UNIFORM_BUFFER and
// ACCELERATION_STRUCTURE_KHR.
struct Binding {
    std::uint32_t binding;
    VkDescriptorType type;
    std::uint32_t count = 1;
};

// What one binding is written with: `images` for the image and sampler types, `buffers` for the buffer types,
// `accelerationStructures` for ACCELERATION_STRUCTURE_KHR (written through
// VkWriteDescriptorSetAccelerationStructureKHR in pNext); exactly the binding's count of them, the others
// empty.
struct DescriptorWrite {
    std::uint32_t binding;
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkAccelerationStructureKHR> accelerationStructures;

    static DescriptorWrite image(std::uint32_t binding, VkDescriptorImageInfo info);
    static DescriptorWrite buffer(std::uint32_t binding, VkDescriptorBufferInfo info);
    static DescriptorWrite accelerationStructure(std::uint32_t binding, VkAccelerationStructureKHR handle);
};

// An RAII VkDescriptorSetLayout of the given bindings, visible to `stages`. Throws std::invalid_argument for
// an unsupported type, a count of 0 or a binding number given twice. The layout must outlive every set
// allocated with it that is still in use.
class DescriptorLayout {
public:
    DescriptorLayout(Context& ctx, std::span<const Binding> bindings, VkShaderStageFlags stages);
    ~DescriptorLayout();
    DescriptorLayout(DescriptorLayout&& other) noexcept;
    DescriptorLayout& operator=(DescriptorLayout&& other) noexcept;
    DescriptorLayout(const DescriptorLayout&) = delete;
    DescriptorLayout& operator=(const DescriptorLayout&) = delete;

    VkDescriptorSetLayout handle() const { return layout_; }
    // In the order given to the constructor.
    std::span<const Binding> bindings() const { return bindings_; }

private:
    void destroy() noexcept;

    Context* ctx_ = nullptr;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    std::vector<Binding> bindings_;
};

// Descriptor sets for one FrameRing's frames, from pools kept per ring slot. A slot's pools are reset when
// the ring reuses the slot (its previous frame has completed), so a set lives until the end of the frame it
// was allocated in. A frame that needs more sets or descriptors than the slot's pools hold gets another
// pool; later frames reuse them. Every pool holds 64 sets and, per type, 256 storage images, 128 sampled
// images, 128 samplers, 128 combined image samplers, 128 storage buffers, 64 uniform buffers and 32
// acceleration structures (more when one set of a layout needs more).
//
// The first allocate() ties the allocator to that ring; allocating with a second live ring throws
// std::logic_error, and once the ring is gone the allocator attaches to the next ring it allocates with.
// Like a ComputePipeline, an allocator must outlive every submit that used its sets: declare the FrameRing
// after it.
class DescriptorAllocator {
public:
    explicit DescriptorAllocator(Context& ctx);
    ~DescriptorAllocator();
    DescriptorAllocator(const DescriptorAllocator&) = delete;
    DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;

    // A set of `layout` for the frame being recorded: between ring.begin() and ring.submit() only (throws
    // std::logic_error otherwise).
    VkDescriptorSet allocate(FrameRing& ring, const DescriptorLayout& layout);
    // Descriptor pools across all slots; for tests that pin that pools are reused rather than added.
    std::size_t poolCount() const;

private:
    static constexpr std::size_t kTypeCount = 7; // the supported descriptor types
    using Counts = std::array<std::uint32_t, kTypeCount>;

    struct Pool {
        VkDescriptorPool pool = VK_NULL_HANDLE;
        Counts capacity{};
    };
    struct SlotPools {
        std::vector<Pool> pools;
        std::size_t current = 0;         // pools before this one are full this frame
        std::uint32_t setsInCurrent = 0; // counted, because some drivers allocate past maxSets
        Counts usedInCurrent{};
    };

    void attach(FrameRing& ring);
    void resetSlot(std::uint32_t slot);
    Pool createPool(const Counts& needs);
    void destroy() noexcept;

    Context& ctx_;
    std::array<SlotPools, FrameRing::kFramesInFlight> slots_{};
    FrameRing::Subscription subscription_;
};

// Writes `set` (of `layout`): every binding of the layout exactly once, with as many descriptors as its
// count, of its kind. Throws std::invalid_argument, before writing anything, for a binding missing, given
// twice, not in the layout, of the wrong kind or with the wrong count.
void writeDescriptors(const Context& ctx, VkDescriptorSet set, const DescriptorLayout& layout,
                      std::span<const DescriptorWrite> writes);

} // namespace evo::gfx
