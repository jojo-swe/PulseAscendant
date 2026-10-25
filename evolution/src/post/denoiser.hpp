#pragma once

#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"

#include <volk.h>

#include <array>
#include <cstdint>

namespace evo::post {

// The path tracer's outputs at the trace size (= modern::GBuffer, by reference):
//   lighting  RGBA32F  mean path radiance without the primary-hit emission
//   emission  RGBA16F  mean primary-hit emission (and the sky on a primary miss)
//   albedo    RGBA16F  primary-hit demodulation albedo
//   normal    RGBA16F  xyz world shading normal, w roughness
//   depth     R32F     linear view depth, -1 on a miss
//   motion    RGBA16F  xy = previous pixel centre - current pixel centre, z = 1 when valid
//   volume    RGBA16F  rgb radiance in-scattered on the primary segment, a its transmittance T ((0, 0, 0, 1)
//                      without a medium)
struct GBufferView {
    gfx::Image& lighting;
    gfx::Image& emission;
    gfx::Image& albedo;
    gfx::Image& normal;
    gfx::Image& depth;
    gfx::Image& motion;
    gfx::Image& volume;
};

struct DenoiseSettings {
    bool temporal = true;               // false: every pixel starts its history this frame (export)
    std::uint32_t atrousIterations = 3; // steps 1, 2, 4, ...; 0: no surface filter (nor reset passes)
    float alphaColor = 0.2f, alphaMoments = 0.2f;
    std::uint32_t maxHistory = 64;
    float sigmaDepth = 1.f, sigmaNormal = 128.f, sigmaLuminance = 4.f;
    std::uint32_t resetAtrousIterations = 5; // at history length <= 2 (when atrousIterations > 0)
    bool fireflyFilter = false;              // exports at spp >= 16 (Decision 16)
    float fireflyK = 4.f;
    std::uint32_t volumeAtrousIterations = 4;
};

// SVGF-lite. Each denoise() records four kinds of compute pass:
//   firefly     (fireflyFilter only: exports from 16 spp) the demodulated illumination of each pixel scaled
//               so its luminance is at most fireflyK x the second largest of its 3x3 neighbourhood (= k x
//               its largest neighbour when it is the largest itself); a lone firefly is clamped, anything two
//               pixels wide is kept. Everything after reads this instead of the lighting.
//   accumulate  illumination = lighting / max(albedo, 1e-3) (a non-finite sample counts as 0, the result is
//               clamped to [0, the half maximum]), reprojected through the motion guide onto the previous
//               frame's 2x2 bilinear footprint (a tap counts when inside, |z_prev - z| <= 0.05 z and
//               dot(n_prev, n) >= 0.9, tested so that a NaN guide never matches; a non-finite depth is a
//               miss), len = valid ? min(len_prev + 1, maxHistory) : 1, an exponential blend with
//               alpha = max(alphaColor, 1 / len) that copies the current value exactly at len 1, the
//               exponentially weighted mean and variance of the luminance likewise (the same as max(m2 -
//               m1^2, 0) of blended moments, without squares that overflow the half range), and the variance
//               the filter starts from: the temporal one once len >= 4, before that an edge-robust spatial
//               one (the smaller of the least 3x3-quadrant variance and the least directional
//               neighbour-difference variance in the 5x5 neighbourhood, depth and normal weighted; see
//               accumulate.comp) times 4 / len. Sparse first frames: below len 4, where at least 40 % of the
//               5x5 neighbourhood's weight has zero luminance, it is the plain 5x5 variance instead, stored
//               negative so the first a-trous pass filters that pixel by depth and normal only (a luminance
//               stop on raw sparse samples weights the rare bright ones down and darkens the frame; the
//               minimum estimator keeps holes).
//               The volume layer beside it: made safe (non-finite radiance 0, non-finite T 1, T in [0, 1]),
//               reprojected through the same motion guide with no depth or normal test, the history clamped
//               to the current 3x3 min/max (the layer is smooth, not sparse), blended with alpha =
//               max(alphaColor, 1 / its own length). The history is not clamped to the current neighbourhood:
//               at 1 spp that would bias sparse lighting dark. A firefly (a sample clamped to 60000) entering
//               a long history at alpha 0.2 adds 12000 and fades by 0.8 per frame: under 1 above its
//               surroundings after 43 frames.
//   atrous      atrousIterations 5x5 B3 passes, step 1, 2, 4, ..., weighted by depth, normal and luminance
//               (against the 3x3 Gaussian of the variance), the variance carried with squared weights; pixels
//               with len <= 2 get max(atrousIterations, resetAtrousIterations) (the others are copied
//               through the extra passes). Then volumeAtrousIterations passes over the volume layer's
//               radiance, weighted by luminance only (against the 3x3 standard deviation at each pass); its
//               transmittance is not blurred, since it carries the silhouettes of the surfaces behind.
//   combine     hdrOut = T * (filtered illumination * albedo + emission) + filtered volume radiance, T the
//               volume's transmittance; exactly the surface without a medium (volume = (0, 0, 0, 1)).
// The history is the accumulated, unfiltered illumination; the emission never enters history or filtering,
// so an emissive screen stays sharp and current. A frame with len == 1 everywhere (the first after a reset,
// or temporal == false) gives the same bytes either way, the volume layer included.
//
// Lifetime: a Denoiser belongs to the ring it was made with; its destructor waits for that ring's submits
// while the ring lives. Its images are shared by all frames in flight; the barriers between frames come from
// the images' tracked state, so frames must be recorded in submission order, and every frame a denoise() was
// recorded into must be submitted (recording advances the history and the images' tracked state).
class Denoiser {
public:
    // Throws std::invalid_argument for a zero size.
    Denoiser(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height);
    ~Denoiser();
    Denoiser(const Denoiser&) = delete;
    Denoiser& operator=(const Denoiser&) = delete;

    // New images at the new size (waits for the ring first: a frame in flight may still use the old ones);
    // also resets the history.
    void resize(std::uint32_t width, std::uint32_t height);
    // The next denoise() uses no history: every pixel's length becomes 1.
    void resetHistory();
    // Records the passes into the frame being recorded on the ring (throws std::logic_error otherwise).
    // Throws std::invalid_argument, recording nothing, for an image of another size or format (the G-buffer
    // in the formats above, hdrOut R16G16B16A16_SFLOAT with storage usage) and for settings out of range:
    // maxHistory 0, more than 9 a-trous iterations of any kind, a sigma not positive and finite, an alpha not
    // in (0, 1], a fireflyK below 1 or not finite.
    void denoise(VkCommandBuffer cmd, const GBufferView& gbuffer, const DenoiseSettings& settings,
                 gfx::Image& hdrOut);

    // After a denoise(): the history it wrote. historyLength(): R = frames accumulated, G the mean and B the
    // temporal variance of the luminance, A the volume layer's frames accumulated. history(): RGB =
    // accumulated illumination, A = the variance the filter started from (negative for a sparse pixel).
    // volumeHistory(): RGB = accumulated in-scattered radiance, A = transmittance. RGBA16F, with
    // transfer-source usage for tests.
    gfx::Image& historyLength() { return moments_[current_]; }
    gfx::Image& history() { return color_[current_]; }
    gfx::Image& volumeHistory() { return volume_[current_]; }

    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    // The device memory of the denoiser's images at the current size, as allocated (VMA).
    VkDeviceSize imageBytes() const;

private:
    void createImages();

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t width_ = 0, height_ = 0;
    gfx::ComputePipeline firefly_, accumulate_, atrous_, combine_;
    // Ping-pong pairs: denoise() reads [1 - current_] (the previous frame) and writes [current_].
    std::array<gfx::Image, 2> color_;    // RGB illumination, A variance
    std::array<gfx::Image, 2> moments_;  // R length, G m1, B m2
    std::array<gfx::Image, 2> guides_;   // XYZ normal, W depth: what the next frame reprojects against
    std::array<gfx::Image, 2> filtered_; // a-trous ping-pong: RGB illumination, A variance
    std::array<gfx::Image, 2> volume_;   // RGB in-scattered radiance, A transmittance; the previous one is
                                         // the volume a-trous' scratch once accumulated
    std::uint32_t current_ = 0;
    bool historyValid_ = false;
    gfx::FrameRing::Subscription ringAlive_; // active while ring_ lives
};

} // namespace evo::post
