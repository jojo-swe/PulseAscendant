#pragma once

#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"

#include <volk.h>

#include <array>
#include <cstdint>

namespace evo::post {

// Where a frame's samples fall inside each trace pixel (in trace pixels): a window `footprint` wide centred
// at the pixel centre + `jitter`. The path tracer box-filters its samples over that window (TraceSettings),
// and the upscaler places them there.
struct SubpixelWindow {
    std::array<float, 2> jitter{0.f, 0.f};
    std::array<float, 2> footprint{1.f, 1.f};
};

// The window of a frame traced at `in` and shown at `out`: the whole trace pixel, unjittered, when the sizes
// are equal, without temporal history (exports) and on the first frame of a history (framesSinceReset 0, so a
// window's reset frame equals an export at the same trace size). Otherwise one output pixel wide (footprint
// in/out per axis, at most 1) and jittered by the Halton (2, 3) sequence, 16 frames long, within the trace
// pixel: jitter = (halton(k) + c - 0.5) * (1 - footprint), k = framesSinceReset mod 16, c = 0.5 - the mean of
// halton(0..15) in that base (so the jitter averages to 0 over a cycle). A pure function of its arguments.
SubpixelWindow subpixelWindow(std::uint32_t inWidth, std::uint32_t inHeight, std::uint32_t outWidth,
                              std::uint32_t outHeight, std::uint32_t framesSinceReset, bool temporal);

struct UpscaleSettings {
    bool temporal = true;  // false: no history this frame (as after a reset); exports
    float feedback = 0.9f; // the history's weight decay per frame, in [0, 1)
    float clipGamma = 1.f; // the clip box: the current 3x3 neighbourhood's YCoCg mean +- clipGamma sigma
    std::array<float, 2> jitter{0.f, 0.f}; // this frame's SubpixelWindow::jitter
};

// The temporal upscaler (shaders/post/upscale.comp): hdrIn at the trace size -> hdrOut at the output size,
// both RGBA16F linear light; identity (one copy, no history) when the sizes are equal.
//
// Per output pixel, in the output pixel's own units:
//   spatial   the current frame Catmull-Rom upsampled at the jittered sample grid (each trace pixel's value
//             sits at its centre + jitter), clamped to the 2x2 nearest values (no ringing, never negative);
//   samples   the current frame's samples within 3x3 trace pixels, weighted exp(-8 d^2) by their distance d
//             from the pixel centre: a sharp estimate and its weight W (1 for a sample on the centre);
//   history   the previous output reprojected with the motion guide (scaled to output pixels; the motion of
//             the nearest depth in the 3x3 trace neighbourhood, so edges keep the foreground's motion),
//             Catmull-Rom sampled and clamped like the spatial term, with its accumulated weight (bilinear)
//             times `feedback`; clipped towards the 3x3 neighbourhood's YCoCg mean into its mean +-
//             clipGamma sigma box;
//   output    (history * weight + sharp * W) / (weight + W), its weight stored with it; without a usable
//             history (a reset, temporal false, invalid motion, a reprojection off the image, no weight) the
//             spatial term, with weight 1.
// The emission (the G-buffer's primary-hit emission, optional) stays out of all that: per trace pixel
// min(emission, hdr) is taken out of the input before the temporal part and its spatial upsample is added
// back to the output, so an emissive screen (the CRT) is always the current frame, never smeared by history.
// Every input is made safe first: non-finite values count as 0 (a non-finite depth as a miss, a non-finite
// motion as invalid), colours are clamped to [0, the half maximum].
//
// The history is reset by resetHistory() and resize(); it follows only from frames recorded with this
// upscaler, so frames must be recorded in submission order (as the Denoiser). Lifetime: as the Denoiser.
class Upscaler {
public:
    // Throws std::invalid_argument for a zero size or an output smaller than the input on either axis.
    Upscaler(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t inWidth, std::uint32_t inHeight,
             std::uint32_t outWidth, std::uint32_t outHeight);
    ~Upscaler();
    Upscaler(const Upscaler&) = delete;
    Upscaler& operator=(const Upscaler&) = delete;

    // New sizes (waits for the ring first: a frame in flight may still use the old history); resets.
    void resize(std::uint32_t inWidth, std::uint32_t inHeight, std::uint32_t outWidth,
                std::uint32_t outHeight);
    // The next record() uses no history.
    void resetHistory();
    // Records the pass into the frame being recorded on the ring (throws std::logic_error otherwise). hdrIn,
    // emission (may be null) and motion RGBA16F and depth R32F at the input size, hdrOut RGBA16F at the
    // output size, all with storage usage; throws std::invalid_argument, recording nothing, for another size
    // or format, or settings out of range (feedback not in [0, 1), clipGamma not positive, a jitter beyond
    // half a pixel, anything not finite). Every image ends in GENERAL.
    void record(VkCommandBuffer cmd, gfx::Image& hdrIn, gfx::Image& depth, gfx::Image& motion,
                const UpscaleSettings& settings, gfx::Image& hdrOut, gfx::Image* emission = nullptr);

    bool identity() const { return inWidth_ == outWidth_ && inHeight_ == outHeight_; }
    // After a record(): the history it wrote (RGB the temporal part of the output, A its weight; RGBA16F at
    // the output size, transfer-source usage for tests). Not written when identity().
    gfx::Image& history() { return history_[current_]; }
    // The bytes of the history images.
    VkDeviceSize imageBytes() const;

private:
    void createImages();

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t inWidth_ = 0, inHeight_ = 0, outWidth_ = 0, outHeight_ = 0;
    gfx::ComputePipeline pipeline_;
    std::array<gfx::Image, 2> history_; // ping-pong: record() reads [1 - current_] and writes [current_]
    std::uint32_t current_ = 0;
    bool historyValid_ = false;
    gfx::FrameRing::Subscription ringAlive_; // active while ring_ lives
};

} // namespace evo::post
