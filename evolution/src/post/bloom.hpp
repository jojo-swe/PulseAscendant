#pragma once

#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"

#include <volk.h>

#include <array>
#include <cstdint>

namespace evo::post {

// Physically based bloom and glare: the HDR picture convolved with a lens's point spread function, with no
// threshold (every pixel spreads, in proportion to its light) and no energy gained or lost. The tonemap mixes
// it in as (1 - w) I + w (I * PSF) with the act's small w (post::Look), so a constant picture stays as it is
// and only bright light shows its halo.
//
// The PSF, measured-lens style:
//   halo     six Gaussians, sigma about 1.2, 2.7, 5.6, 11, 23 and 45 pixels at 1080p (the chain's levels 1-6,
//            each a quarter the area of the last, down to 1/64 of the size), weighted kLevelWeights: a soft
//            core with a long, smooth skirt and no ring;
//   spokes   kSpokeShare of the bloom: faint diffraction of a six-blade aperture, three line blurs at 0, 60 and
//            120 degrees of the quarter-size picture; no starburst (no more than three lines, no colour);
//   streak   with anamorphic > 0, kStreakShare * anamorphic of the bloom in one long horizontal line (only
//            WORMHOLE's white-out uses it).
// The levels scale with the picture, so the PSF covers the same share of the frame at any resolution.
//
// Passes (GPU scope "bloom" in the stage): 6 downsamples (bloom_down), the spokes (bloom_spokes, at quarter
// size), 5 upsamples in place (bloom_up): 12 dispatches. All but the first read only pictures of half the
// size or less, so at 3840x2160 the chain moves about 1.5 times the HDR image's bytes; on an RTX 4090 that
// is an estimated 0.2-0.3 ms, the spokes' 3 x 49 bilinear taps at 960x540 about 0.1 ms more.
class Bloom {
public:
    static constexpr std::uint32_t kLevels = 6;
    static constexpr std::array<float, kLevels> kLevelWeights = {0.26f, 0.22f, 0.19f, 0.15f, 0.11f, 0.07f};
    static constexpr float kSpokeShare = 0.05f;
    static constexpr float kStreakShare = 0.3f;

    // The HDR picture's size; throws std::invalid_argument for 0.
    Bloom(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height);
    void resize(std::uint32_t width, std::uint32_t height);

    // Records the chain over `hdr` (R16G16B16A16_SFLOAT, storage, the size given): image() then holds the
    // picture convolved with the PSF, at half size. anamorphic in [0, 1] (clamped); spokeShare (clamped to
    // [0, 0.08]) is for tests that take the halo on its own. Throws std::invalid_argument for another format
    // or size, recording nothing. Between ring.begin() and ring.submit() only.
    void record(VkCommandBuffer cmd, gfx::Image& hdr, float anamorphic = 0.f, float spokeShare = kSpokeShare);

    // The bloom picture: R16G16B16A16_SFLOAT, ((width + 1) / 2) x ((height + 1) / 2), in GENERAL after
    // record(). Its pixel q stands for the full-size pixels 2q and 2q + 1 on each axis (the tonemap reads it
    // bilinearly at p / 2 - 0.25).
    gfx::Image& image() { return levels_[0]; }
    // The device memory of the chain's images.
    VkDeviceSize imageBytes() const;

private:
    void makeImages();

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t width_, height_;
    gfx::ComputePipeline down_, spokes_, up_;
    std::array<gfx::Image, kLevels> levels_; // level k + 1: the picture at 1 / 2^(k + 1) of the size
    gfx::Image spokeImage_;                  // at level 2's size
};

} // namespace evo::post
