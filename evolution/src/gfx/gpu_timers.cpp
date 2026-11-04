#include "gfx/gpu_timers.hpp"

#include "gfx/vk_check.hpp"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>

namespace evo::gfx {

GpuTimers::GpuTimers(Context& ctx, FrameRing& ring, std::uint32_t maxScopes, bool enabled)
    : ctx_(ctx), ring_(ring), maxScopes_(maxScopes) {
    if (maxScopes == 0)
        throw std::invalid_argument("GpuTimers: maxScopes must be positive");
    std::uint32_t families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice(), &families, nullptr);
    std::vector<VkQueueFamilyProperties> properties(families);
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice(), &families, properties.data());
    VkPhysicalDeviceProperties device{};
    vkGetPhysicalDeviceProperties(ctx.physicalDevice(), &device);
    validBits_ = ctx.queueFamily() < families ? properties[ctx.queueFamily()].timestampValidBits : 0u;
    periodNs_ = static_cast<double>(device.limits.timestampPeriod);
    enabled_ = enabled && validBits_ > 0 && periodNs_ > 0.0;

    if (enabled_) {
        for (Slot& slot : slots_) {
            VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = 2 * maxScopes;
            vkCheck(vkCreateQueryPool(ctx.device(), &info, nullptr, &slot.pool), "vkCreateQueryPool");
        }
    }
    // The ring reuses a slot only once its last frame completed: its results are ready without waiting.
    ringBegin_ = ring.onBegin([this](std::uint32_t slot) {
        if (slots_[slot].pending)
            read(slot);
        Slot& s = slots_[slot];
        s.names.clear();
        s.closed.clear();
        s.open.clear();
        s.reset = false;
    });
}

GpuTimers::~GpuTimers() {
    // Best effort, as in ~FrameRing: a failed wait (a lost device) has nothing left to protect.
    if (ringBegin_.active()) {
        try {
            ring_.waitAll(); // shutdown: the frames in flight may still write the pools
        } catch (const std::exception&) {
        }
    }
    for (Slot& slot : slots_)
        if (slot.pool != VK_NULL_HANDLE)
            vkDestroyQueryPool(ctx_.device(), slot.pool, nullptr);
}

GpuTimers::Slot& GpuTimers::recordingSlot(const char* what) {
    if (!ring_.recording())
        throw std::logic_error(std::string("GpuTimers::") + what + "() outside ring.begin()/submit()");
    return slots_[ring_.slot()];
}

void GpuTimers::begin(VkCommandBuffer cmd, const char* name) {
    Slot& slot = recordingSlot("begin");
    if (slot.names.size() >= maxScopes_)
        throw std::length_error("GpuTimers: more than " + std::to_string(maxScopes_) + " scopes in a frame");
    const auto index = static_cast<std::uint32_t>(slot.names.size());
    slot.names.emplace_back(name);
    slot.closed.push_back(false);
    slot.open.push_back(index);
    if (!enabled_)
        return;
    if (!slot.reset) {
        vkCmdResetQueryPool(cmd, slot.pool, 0, 2 * maxScopes_);
        slot.reset = true;
        slot.pending = true;
        slot.serial = ++serial_;
    }
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, slot.pool, 2 * index);
}

void GpuTimers::end(VkCommandBuffer cmd) {
    Slot& slot = recordingSlot("end");
    if (slot.open.empty())
        throw std::logic_error("GpuTimers::end() without an open scope");
    const std::uint32_t index = slot.open.back();
    slot.open.pop_back();
    slot.closed[index] = true;
    if (enabled_)
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, slot.pool, 2 * index + 1);
}

void GpuTimers::read(std::uint32_t index) {
    Slot& slot = slots_[index];
    slot.pending = false;
    const auto count = static_cast<std::uint32_t>(slot.names.size());
    std::vector<std::uint64_t> ticks(2 * std::size_t{count});
    if (count > 0)
        vkCheck(vkGetQueryPoolResults(
                    ctx_.device(), slot.pool, 0, 2 * count, ticks.size() * sizeof(std::uint64_t),
                    ticks.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                "vkGetQueryPoolResults");
    // Only the open scopes' end queries were never written; the closed ones are complete.
    const std::uint64_t mask = validBits_ >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << validBits_) - 1;
    Scopes scopes;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!slot.closed[i])
            continue;
        const std::uint64_t delta = (ticks[2 * i + 1] - ticks[2 * i]) & mask;
        scopes.emplace_back(slot.names[i], static_cast<double>(delta) * periodNs_ * 1e-6);
    }
    last_ = scopes;
    queued_.push_back(std::move(scopes));
    while (queued_.size() > kMaxQueued)
        queued_.pop_front();
}

std::vector<GpuTimers::Scopes> GpuTimers::takeCompleted() {
    std::vector<Scopes> out(std::make_move_iterator(queued_.begin()), std::make_move_iterator(queued_.end()));
    queued_.clear();
    return out;
}

void GpuTimers::collect() {
    if (ring_.recording())
        throw std::logic_error("GpuTimers::collect() while a frame is being recorded");
    std::vector<std::uint32_t> pending;
    for (std::uint32_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].pending)
            pending.push_back(i);
    std::sort(pending.begin(), pending.end(),
              [&](std::uint32_t a, std::uint32_t b) { return slots_[a].serial < slots_[b].serial; });
    for (const std::uint32_t slot : pending)
        read(slot);
}

} // namespace evo::gfx
