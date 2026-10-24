#pragma once

#include "gfx/context.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace evo::gfx {

// The frames in flight. Each slot has its own command pool and primary command buffer; one timeline
// semaphore counts every submit, and a slot is reused only once the timeline shows its last submit done.
// Headless runs record, submit and wait(value) for the frame they read back; the window keeps two frames
// in flight and never waits for the whole GPU.
//
//     VkCommandBuffer cmd = ring.begin();   // waits for this slot's previous frame, resets it
//     ...record passes...
//     std::uint64_t done = ring.submit();   // the timeline value that marks this frame complete
//
// The destructor waits for every submit, so declare a ring after the images and buffers its frames use:
// it is then destroyed first and they outlive the GPU work.
class FrameRing {
public:
    static constexpr std::uint32_t kFramesInFlight = 2;

    // A registration of a callback that begin() runs for the slot it is about to reuse, after that slot's
    // previous submit completed. Unregisters when destroyed; harmless if the ring is gone first.
    class Subscription {
    public:
        Subscription() = default;
        ~Subscription();
        Subscription(Subscription&& other) noexcept;
        Subscription& operator=(Subscription&& other) noexcept;
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;

        // Whether the ring it was taken from is still alive.
        bool active() const { return !callbacks_.expired(); }
        bool belongsTo(const FrameRing& ring) const;

    private:
        friend class FrameRing;
        struct Callbacks;
        Subscription(std::weak_ptr<Callbacks> callbacks, std::uint64_t id)
            : callbacks_(std::move(callbacks)), id_(id) {}
        void reset() noexcept;

        std::weak_ptr<Callbacks> callbacks_;
        std::uint64_t id_ = 0;
    };

    explicit FrameRing(Context& ctx);
    ~FrameRing(); // waits for every submit, so nothing the ring recorded is still running
    FrameRing(const FrameRing&) = delete;
    FrameRing& operator=(const FrameRing&) = delete;

    // Waits (on the timeline) until this slot's previous submit completed, runs the slot callbacks, resets
    // the slot's command pool and begins recording its command buffer.
    VkCommandBuffer begin();
    // Ends recording and submits, signalling the timeline with a new value (returned) and, if given,
    // signalBinary; waits for waitBinary at waitStage (all commands when 0). Moves on to the next slot.
    std::uint64_t submit(VkSemaphore waitBinary = VK_NULL_HANDLE, VkPipelineStageFlags2 waitStage = 0,
                         VkSemaphore signalBinary = VK_NULL_HANDLE);
    // Host wait until the timeline reaches value. Throws std::logic_error for a value never submitted.
    void wait(std::uint64_t value);
    // Waits for every submit so far.
    void waitAll();

    // The slot being recorded between begin() and submit(), else the slot the next begin() uses.
    std::uint32_t slot() const { return slot_; }
    bool recording() const { return recording_; }
    VkSemaphore timeline() const { return timeline_; }

    // Registers a callback begin() runs with the slot it reuses (see Subscription). Callbacks must not
    // register or unregister others while they run.
    Subscription onBegin(std::function<void(std::uint32_t slot)> callback);

private:
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        std::uint64_t submitted = 0; // the timeline value of this slot's last submit; 0 before the first
    };

    void destroy() noexcept;

    Context& ctx_;
    VkSemaphore timeline_ = VK_NULL_HANDLE;
    std::array<Frame, kFramesInFlight> frames_{};
    std::uint32_t slot_ = 0;
    std::uint64_t lastSubmitted_ = 0;
    bool recording_ = false;
    std::shared_ptr<Subscription::Callbacks> callbacks_;
};

} // namespace evo::gfx
