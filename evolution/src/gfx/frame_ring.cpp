#include "gfx/frame_ring.hpp"

#include "gfx/vk_check.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace evo::gfx {

struct FrameRing::Subscription::Callbacks {
    std::vector<std::pair<std::uint64_t, std::function<void(std::uint32_t)>>> list;
    std::uint64_t nextId = 1;
};

// --- Subscription
// -------------------------------------------------------------------------------------------

FrameRing::Subscription::~Subscription() {
    reset();
}

FrameRing::Subscription::Subscription(Subscription&& other) noexcept
    : callbacks_(std::move(other.callbacks_)), id_(std::exchange(other.id_, 0)) {}

FrameRing::Subscription& FrameRing::Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        reset();
        callbacks_ = std::move(other.callbacks_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

bool FrameRing::Subscription::belongsTo(const FrameRing& ring) const {
    const auto callbacks = callbacks_.lock();
    return callbacks && callbacks == ring.callbacks_;
}

void FrameRing::Subscription::reset() noexcept {
    if (const auto callbacks = callbacks_.lock()) {
        std::erase_if(callbacks->list, [this](const auto& entry) { return entry.first == id_; });
    }
    callbacks_.reset();
    id_ = 0;
}

// --- FrameRing
// ----------------------------------------------------------------------------------------------

FrameRing::FrameRing(Context& ctx) : ctx_(ctx), callbacks_(std::make_shared<Subscription::Callbacks>()) {
    const VkDevice device = ctx.device();
    try {
        VkSemaphoreTypeCreateInfo typeInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        typeInfo.initialValue = 0;
        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        semaphoreInfo.pNext = &typeInfo;
        vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &timeline_),
                "vkCreateSemaphore(timeline)");
        ctx.setObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<std::uint64_t>(timeline_),
                          "frame ring timeline");

        for (Frame& frame : frames_) {
            VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; // reset as a whole every frame
            poolInfo.queueFamilyIndex = ctx.queueFamily();
            vkCheck(vkCreateCommandPool(device, &poolInfo, nullptr, &frame.pool),
                    "vkCreateCommandPool(frame)");

            VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocInfo.commandPool = frame.pool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            vkCheck(vkAllocateCommandBuffers(device, &allocInfo, &frame.cmd),
                    "vkAllocateCommandBuffers(frame)");
        }
    } catch (...) {
        destroy();
        throw;
    }
}

FrameRing::~FrameRing() {
    destroy();
}

void FrameRing::destroy() noexcept {
    const VkDevice device = ctx_.device();
    if (timeline_ && lastSubmitted_ > 0) {
        VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &timeline_;
        waitInfo.pValues = &lastSubmitted_;
        vkWaitSemaphores(device, &waitInfo, std::numeric_limits<std::uint64_t>::max()); // best effort
    }
    for (Frame& frame : frames_) {
        if (frame.pool)
            vkDestroyCommandPool(device, frame.pool, nullptr); // frees frame.cmd
        frame = {};
    }
    if (timeline_)
        vkDestroySemaphore(device, timeline_, nullptr);
    timeline_ = VK_NULL_HANDLE;
}

VkCommandBuffer FrameRing::begin() {
    if (recording_)
        throw std::logic_error("FrameRing::begin() while a frame is recording; submit() it first");

    Frame& frame = frames_[slot_];
    wait(frame.submitted);
    // Copied, so a callback that drops its own subscription cannot invalidate the loop.
    const auto callbacks = callbacks_->list;
    for (const auto& [id, callback] : callbacks)
        callback(slot_);

    vkCheck(vkResetCommandPool(ctx_.device(), frame.pool, 0), "vkResetCommandPool(frame)");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(frame.cmd, &beginInfo), "vkBeginCommandBuffer(frame)");
    recording_ = true;
    return frame.cmd;
}

std::uint64_t FrameRing::submit(VkSemaphore waitBinary, VkPipelineStageFlags2 waitStage,
                                VkSemaphore signalBinary) {
    if (!recording_)
        throw std::logic_error("FrameRing::submit() without begin()");
    Frame& frame = frames_[slot_];
    recording_ = false; // the buffer is spent either way; the next begin() resets it
    vkCheck(vkEndCommandBuffer(frame.cmd), "vkEndCommandBuffer(frame)");

    const std::uint64_t value = lastSubmitted_ + 1;

    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = waitBinary;
    waitInfo.stageMask = waitStage ? waitStage : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    std::array<VkSemaphoreSubmitInfo, 2> signalInfos{};
    std::uint32_t signalCount = 0;
    signalInfos[signalCount] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfos[signalCount].semaphore = timeline_;
    signalInfos[signalCount].value = value;
    signalInfos[signalCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    ++signalCount;
    if (signalBinary) {
        signalInfos[signalCount] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signalInfos[signalCount].semaphore = signalBinary;
        signalInfos[signalCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        ++signalCount;
    }

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = frame.cmd;

    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.waitSemaphoreInfoCount = waitBinary ? 1 : 0;
    submitInfo.pWaitSemaphoreInfos = waitBinary ? &waitInfo : nullptr;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;
    submitInfo.signalSemaphoreInfoCount = signalCount;
    submitInfo.pSignalSemaphoreInfos = signalInfos.data();
    vkCheck(vkQueueSubmit2(ctx_.queue(), 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit2(frame)");

    frame.submitted = value;
    lastSubmitted_ = value;
    slot_ = (slot_ + 1) % kFramesInFlight;
    return value;
}

void FrameRing::split() {
    if (!recording_)
        throw std::logic_error("FrameRing::split() without begin()");
    const std::uint32_t slot = slot_;
    const std::uint64_t done = submit(); // moves on to the next slot: back to this one below
    slot_ = slot;
    wait(done);
    Frame& frame = frames_[slot_];
    vkCheck(vkResetCommandPool(ctx_.device(), frame.pool, 0), "vkResetCommandPool(frame split)");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(frame.cmd, &beginInfo), "vkBeginCommandBuffer(frame split)");
    recording_ = true;
}

void FrameRing::wait(std::uint64_t value) {
    if (value > lastSubmitted_)
        throw std::logic_error("FrameRing::wait(" + std::to_string(value) +
                               ") for a value never submitted (last " + std::to_string(lastSubmitted_) + ")");
    if (value == 0)
        return; // the timeline starts there
    VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &timeline_;
    waitInfo.pValues = &value;
    vkCheck(vkWaitSemaphores(ctx_.device(), &waitInfo, std::numeric_limits<std::uint64_t>::max()),
            "vkWaitSemaphores(frame)");
}

void FrameRing::waitAll() {
    wait(lastSubmitted_);
}

FrameRing::Subscription FrameRing::onBegin(std::function<void(std::uint32_t slot)> callback) {
    const std::uint64_t id = callbacks_->nextId++;
    callbacks_->list.emplace_back(id, std::move(callback));
    return Subscription(callbacks_, id);
}

} // namespace evo::gfx
