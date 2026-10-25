#pragma once

#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace evo::gfx {

// GPU time per pass, from timestamp queries: a query pool per ring slot (two queries per scope), reset in the
// command buffer at the frame's first scope and read when the ring reuses the slot, so reading never waits.
// Scopes nest (begin/end pairs) and come back in the order they began, each the time between its two
// timestamps in milliseconds, written at ALL_COMMANDS (the commands before have finished).
//
// A queue family without timestamps (timestampValidBits 0), a zero timestampPeriod, or `enabled` false gives
// a disabled set: begin() and end() record nothing and nothing is ever reported, so a renderer can time its
// passes unconditionally. Differences are taken modulo 2^timestampValidBits.
//
// Lifetime: belongs to the ring it was made with; its destructor waits for that ring's submits while the ring
// lives (the pools must outlive the frames that wrote them).
class GpuTimers {
public:
    using Scopes = std::vector<std::pair<std::string, double>>; // (name, milliseconds), in begin order

    GpuTimers(Context& ctx, FrameRing& ring, std::uint32_t maxScopes = 16, bool enabled = true);
    ~GpuTimers();
    GpuTimers(const GpuTimers&) = delete;
    GpuTimers& operator=(const GpuTimers&) = delete;

    bool enabled() const { return enabled_; }
    std::uint32_t validBits() const { return validBits_; } // the queue family's timestampValidBits
    double periodNs() const { return periodNs_; }          // nanoseconds per timestamp tick

    // Opens a scope in the frame being recorded on the ring. Throws std::logic_error outside ring.begin() /
    // submit() and std::length_error past maxScopes scopes in one frame (both also when disabled, so misuse
    // shows on any machine).
    void begin(VkCommandBuffer cmd, const char* name);
    // Closes the innermost open scope. Throws std::logic_error when none is open.
    void end(VkCommandBuffer cmd);

    // The scopes of the newest frame read so far (empty before the first, and always when disabled). A scope
    // left open at submit is left out.
    const Scopes& lastCompleted() const { return last_; }
    // Every frame read since the last call, oldest first (at most kMaxQueued kept: the oldest go first).
    std::vector<Scopes> takeCompleted();
    // Reads every slot whose frame was submitted but not read yet, oldest first. Only after the ring's frames
    // completed (ring.waitAll()): the read waits for the results otherwise.
    void collect();

    static constexpr std::size_t kMaxQueued = 256;

private:
    struct Slot {
        VkQueryPool pool = VK_NULL_HANDLE;
        std::vector<std::string> names; // per scope, in begin order
        std::vector<bool> closed;
        std::vector<std::uint32_t> open; // the stack of open scopes
        bool reset = false;              // the pool was reset in this frame's command buffer
        bool pending = false;            // written by a frame not read yet
        std::uint64_t serial = 0;        // the frame's number, to read pending slots in order
    };

    Slot& recordingSlot(const char* what);
    void read(std::uint32_t slot);

    Context& ctx_;
    FrameRing& ring_;
    std::uint32_t maxScopes_;
    bool enabled_ = false;
    std::uint32_t validBits_ = 0;
    double periodNs_ = 0.0;
    std::array<Slot, FrameRing::kFramesInFlight> slots_;
    std::uint64_t serial_ = 0;
    Scopes last_;
    std::deque<Scopes> queued_;
    FrameRing::Subscription ringBegin_; // active while ring_ lives
};

} // namespace evo::gfx
