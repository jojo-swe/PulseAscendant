#include "gfx/descriptors.hpp"

#include "gfx/vk_check.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace evo::gfx {

namespace {

constexpr std::uint32_t kSetsPerPool = 64;

enum class Kind { Image, Buffer, AccelerationStructure };

struct TypeInfo {
    VkDescriptorType type;
    Kind kind;
    std::uint32_t perPool; // descriptors of this type in a standard pool
    const char* name;
};

constexpr std::array<TypeInfo, 7> kTypes = {{
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, Kind::Image, 256, "STORAGE_IMAGE"},
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, Kind::Image, 128, "SAMPLED_IMAGE"},
    {VK_DESCRIPTOR_TYPE_SAMPLER, Kind::Image, 128, "SAMPLER"},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, Kind::Image, 128, "COMBINED_IMAGE_SAMPLER"},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Kind::Buffer, 128, "STORAGE_BUFFER"},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Kind::Buffer, 64, "UNIFORM_BUFFER"},
    {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, Kind::AccelerationStructure, 32,
     "ACCELERATION_STRUCTURE"},
}};

// The index of `type` in kTypes, or kTypes.size() when it is not supported.
std::size_t typeIndex(VkDescriptorType type) {
    for (std::size_t i = 0; i < kTypes.size(); ++i)
        if (kTypes[i].type == type)
            return i;
    return kTypes.size();
}

std::string describe(const Binding& binding) {
    return "binding " + std::to_string(binding.binding) + " (" + kTypes[typeIndex(binding.type)].name + "[" +
           std::to_string(binding.count) + "])";
}

// Throws std::invalid_argument unless `writes` cover every binding of `bindings` exactly once, each with the
// binding's count of descriptors of its kind and nothing else.
void checkWrites(std::span<const Binding> bindings, std::span<const DescriptorWrite> writes) {
    std::vector<bool> written(bindings.size(), false);
    for (const DescriptorWrite& write : writes) {
        const auto found = std::find_if(bindings.begin(), bindings.end(),
                                        [&](const Binding& b) { return b.binding == write.binding; });
        if (found == bindings.end())
            throw std::invalid_argument("writeDescriptors: binding " + std::to_string(write.binding) +
                                        " is not in the layout");
        const auto index = static_cast<std::size_t>(found - bindings.begin());
        if (written[index])
            throw std::invalid_argument("writeDescriptors: " + describe(*found) + " is written twice");
        written[index] = true;

        const Kind kind = kTypes[typeIndex(found->type)].kind;
        const std::size_t images = write.images.size(), buffers = write.buffers.size(),
                          structures = write.accelerationStructures.size();
        const std::size_t ofKind = kind == Kind::Image ? images : kind == Kind::Buffer ? buffers : structures;
        if (images + buffers + structures != ofKind)
            throw std::invalid_argument("writeDescriptors: " + describe(*found) + " takes " +
                                        (kind == Kind::Image    ? "images"
                                         : kind == Kind::Buffer ? "buffers"
                                                                : "acceleration structures") +
                                        " only");
        if (ofKind != found->count)
            throw std::invalid_argument("writeDescriptors: " + describe(*found) + " takes " +
                                        std::to_string(found->count) + " descriptors, got " +
                                        std::to_string(ofKind));
    }
    for (std::size_t i = 0; i < bindings.size(); ++i)
        if (!written[i])
            throw std::invalid_argument("writeDescriptors: " + describe(bindings[i]) + " is not written");
}

} // namespace

DescriptorWrite DescriptorWrite::image(std::uint32_t binding, VkDescriptorImageInfo info) {
    return {binding, {info}, {}, {}};
}

DescriptorWrite DescriptorWrite::buffer(std::uint32_t binding, VkDescriptorBufferInfo info) {
    return {binding, {}, {info}, {}};
}

DescriptorWrite DescriptorWrite::accelerationStructure(std::uint32_t binding,
                                                       VkAccelerationStructureKHR handle) {
    return {binding, {}, {}, {handle}};
}

// --- DescriptorLayout ---

DescriptorLayout::DescriptorLayout(Context& ctx, std::span<const Binding> bindings, VkShaderStageFlags stages)
    : ctx_(&ctx), bindings_(bindings.begin(), bindings.end()) {
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    for (std::size_t i = 0; i < bindings_.size(); ++i) {
        const Binding& binding = bindings_[i];
        if (typeIndex(binding.type) == kTypes.size())
            throw std::invalid_argument("DescriptorLayout: unsupported descriptor type " +
                                        std::to_string(static_cast<int>(binding.type)) + " at binding " +
                                        std::to_string(binding.binding));
        if (binding.count == 0)
            throw std::invalid_argument("DescriptorLayout: " + describe(binding) + " has no descriptors");
        for (std::size_t j = 0; j < i; ++j)
            if (bindings_[j].binding == binding.binding)
                throw std::invalid_argument("DescriptorLayout: binding " + std::to_string(binding.binding) +
                                            " is given twice");
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = binding.binding;
        layoutBinding.descriptorType = binding.type;
        layoutBinding.descriptorCount = binding.count;
        layoutBinding.stageFlags = stages;
        layoutBindings.push_back(layoutBinding);
    }
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = static_cast<std::uint32_t>(layoutBindings.size());
    info.pBindings = layoutBindings.data();
    vkCheck(vkCreateDescriptorSetLayout(ctx.device(), &info, nullptr, &layout_),
            "vkCreateDescriptorSetLayout");
}

DescriptorLayout::~DescriptorLayout() {
    destroy();
}

DescriptorLayout::DescriptorLayout(DescriptorLayout&& other) noexcept
    : ctx_(std::exchange(other.ctx_, nullptr)), layout_(std::exchange(other.layout_, VK_NULL_HANDLE)),
      bindings_(std::move(other.bindings_)) {}

DescriptorLayout& DescriptorLayout::operator=(DescriptorLayout&& other) noexcept {
    if (this != &other) {
        destroy();
        ctx_ = std::exchange(other.ctx_, nullptr);
        layout_ = std::exchange(other.layout_, VK_NULL_HANDLE);
        bindings_ = std::move(other.bindings_);
    }
    return *this;
}

void DescriptorLayout::destroy() noexcept {
    if (ctx_ && layout_)
        vkDestroyDescriptorSetLayout(ctx_->device(), layout_, nullptr);
    ctx_ = nullptr;
    layout_ = VK_NULL_HANDLE;
}

// --- DescriptorAllocator ---

DescriptorAllocator::DescriptorAllocator(Context& ctx) : ctx_(ctx) {}

DescriptorAllocator::~DescriptorAllocator() {
    destroy();
}

void DescriptorAllocator::destroy() noexcept {
    subscription_ = {}; // the ring must not call back into a half-destroyed allocator
    for (SlotPools& slot : slots_) {
        for (const Pool& pool : slot.pools)
            vkDestroyDescriptorPool(ctx_.device(), pool.pool, nullptr);
        slot = {};
    }
}

void DescriptorAllocator::attach(FrameRing& ring) {
    if (subscription_.belongsTo(ring))
        return;
    if (subscription_.active())
        throw std::logic_error("a DescriptorAllocator allocates for one FrameRing only");
    // A ring this allocator served before is gone, and it waited for its submits: every set is free.
    for (std::uint32_t slot = 0; slot < FrameRing::kFramesInFlight; ++slot)
        resetSlot(slot);
    subscription_ = ring.onBegin([this](std::uint32_t slot) { resetSlot(slot); });
}

void DescriptorAllocator::resetSlot(std::uint32_t slot) {
    SlotPools& pools = slots_[slot];
    for (const Pool& pool : pools.pools)
        vkCheck(vkResetDescriptorPool(ctx_.device(), pool.pool, 0), "vkResetDescriptorPool");
    pools.current = 0;
    pools.setsInCurrent = 0;
    pools.usedInCurrent = {};
}

DescriptorAllocator::Pool DescriptorAllocator::createPool(const Counts& needs) {
    static_assert(kTypes.size() == kTypeCount, "a pool counts every supported descriptor type");
    Pool pool;
    std::vector<VkDescriptorPoolSize> sizes;
    for (std::size_t i = 0; i < kTypes.size(); ++i) {
        pool.capacity[i] = std::max(kTypes[i].perPool, needs[i]);
        sizes.push_back({kTypes[i].type, pool.capacity[i]});
    }
    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.maxSets = kSetsPerPool;
    info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    vkCheck(vkCreateDescriptorPool(ctx_.device(), &info, nullptr, &pool.pool), "vkCreateDescriptorPool");
    return pool;
}

VkDescriptorSet DescriptorAllocator::allocate(FrameRing& ring, const DescriptorLayout& layout) {
    if (!ring.recording())
        throw std::logic_error("DescriptorAllocator::allocate() outside FrameRing::begin()/submit()");
    attach(ring);

    Counts needs{};
    for (const Binding& binding : layout.bindings())
        needs[typeIndex(binding.type)] += binding.count;

    SlotPools& pools = slots_[ring.slot()];
    const VkDescriptorSetLayout handle = layout.handle();
    for (;;) {
        if (pools.current == pools.pools.size())
            pools.pools.push_back(createPool(needs));
        const Pool& pool = pools.pools[pools.current];

        bool fits = pools.setsInCurrent < kSetsPerPool;
        for (std::size_t i = 0; i < kTypeCount && fits; ++i)
            fits = pools.usedInCurrent[i] + needs[i] <= pool.capacity[i];
        if (fits) {
            VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            info.descriptorPool = pool.pool;
            info.descriptorSetCount = 1;
            info.pSetLayouts = &handle;
            VkDescriptorSet set = VK_NULL_HANDLE;
            const VkResult result = vkAllocateDescriptorSets(ctx_.device(), &info, &set);
            if (result == VK_SUCCESS) {
                ++pools.setsInCurrent;
                for (std::size_t i = 0; i < kTypeCount; ++i)
                    pools.usedInCurrent[i] += needs[i];
                return set;
            }
            if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
                vkCheck(result, "vkAllocateDescriptorSets");
            // An empty pool that cannot hold one set never will: no endless new pools.
            if (pools.setsInCurrent == 0)
                vkCheck(result, "vkAllocateDescriptorSets(empty pool)");
        }
        ++pools.current; // full for the rest of the frame
        pools.setsInCurrent = 0;
        pools.usedInCurrent = {};
    }
}

std::size_t DescriptorAllocator::poolCount() const {
    std::size_t count = 0;
    for (const SlotPools& slot : slots_)
        count += slot.pools.size();
    return count;
}

// --- writeDescriptors ---

void writeDescriptors(const Context& ctx, VkDescriptorSet set, const DescriptorLayout& layout,
                      std::span<const DescriptorWrite> writes) {
    const std::span<const Binding> bindings = layout.bindings();
    checkWrites(bindings, writes);

    std::vector<VkWriteDescriptorSet> vkWrites;
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> structures;
    vkWrites.reserve(writes.size());
    structures.reserve(writes.size()); // pNext points into it: no reallocation
    for (const DescriptorWrite& write : writes) {
        const Binding& binding = *std::find_if(bindings.begin(), bindings.end(),
                                               [&](const Binding& b) { return b.binding == write.binding; });
        VkWriteDescriptorSet vkWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        vkWrite.dstSet = set;
        vkWrite.dstBinding = binding.binding;
        vkWrite.descriptorCount = binding.count;
        vkWrite.descriptorType = binding.type;
        switch (kTypes[typeIndex(binding.type)].kind) {
        case Kind::Image:
            vkWrite.pImageInfo = write.images.data();
            break;
        case Kind::Buffer:
            vkWrite.pBufferInfo = write.buffers.data();
            break;
        case Kind::AccelerationStructure: {
            VkWriteDescriptorSetAccelerationStructureKHR& as = structures.emplace_back();
            as.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            as.accelerationStructureCount = binding.count;
            as.pAccelerationStructures = write.accelerationStructures.data();
            vkWrite.pNext = &as;
            break;
        }
        }
        vkWrites.push_back(vkWrite);
    }
    vkUpdateDescriptorSets(ctx.device(), static_cast<std::uint32_t>(vkWrites.size()), vkWrites.data(), 0,
                           nullptr);
}

} // namespace evo::gfx
