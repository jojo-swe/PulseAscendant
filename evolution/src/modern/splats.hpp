#pragma once

// Emissive splats (step 5, Task 10): millions of points added into the path tracer's emission guide after
// the trace and before the denoiser, so they bypass its history and filter (like the CRT's picture). A scene
// names its splat shader (SceneStatic::splatShader) and sets SceneFrame::splats in the frames that show them.
//
// The scene's shader defines the points, a pure function of (id, seconds, params):
//
//     #version 460
//     #extension GL_GOOGLE_include_directive : require
//     #include "splat_common.glsl"     // the bindings, the projection, the footprint and main()
//     SplatPoint splatPoint(uint id, float seconds, vec4 params[4]) { ... }
//
// SplatPoint {vec3 position; vec3 flux; float radius;}: a world position, the flux it adds (radiance summed
// over its pixels, at a 1920x1080 reference: see below) and a world radius (0: a point).
//
// The footprint: the point projects through the frame's pinhole camera (no depth of field) to a pixel
// position; its depth (the linear view depth, as the depth guide) is tested once, against the depth guide at
// the pixel it lands in: behind it (or behind the camera) the point adds nothing; a guide of -1 (a miss)
// counts as infinitely far. In front, a Gaussian with sigma = max(projected radius, 0.5 px), truncated to the
// 5x5 pixels around it and renormalised to sum 1, spreads flux * w * (W * H) / (1920 * 1080) over them (W x H
// the trace size): a pixel's radiance scales with its solid angle, so the point's energy (the sum over the
// picture of radiance times pixel area) is the same at every resolution. Pixels off the picture are part of
// the 25 and drop out.
//
// Determinism: every add goes into three R32_UINT accumulators (one per channel) in fixed point, as
// round(flux * w * fixedPointScale), with imageAtomicAdd. The sums are in reference units (radiance at the
// 1920x1080 reference) and the resolve applies (W * H) / (1920 * 1080), so the step an add rounds to is the
// same at every resolution (a 160x90 test quantises like a 4K export). Integer addition is associative and
// commutative, so the sums do not depend on the order the points arrive in (integer
// atomics into fixed-point accumulators; float atomics are forbidden). An add that wraps a channel (the new
// sum smaller than the old) sets that channel's bit in a fourth R32_UINT image (imageAtomicOr); so does a
// single add of 2^32 units or more, which is never added. A channel wraps somewhere iff its exact sum
// reaches 2^32, whatever the order, so the flag is deterministic too. The resolve adds
// sum / fixedPointScale * (W * H) / (1920 * 1080) into the emission guide (each channel at most 65504, the
// half-float maximum) and a flagged channel resolves to 65504.
//
// The fixed-point range: a channel holds up to (2^32 - 1) / fixedPointScale reference units per pixel, in
// steps of 1 / fixedPointScale; each add rounds to the nearest step (an add below half a step vanishes). In
// radiance at a W x H trace that is up to (2^32 - 1) / fixedPointScale * (W * H) / (1920 * 1080). The default
// scale 4096 holds 1,048,575.99 reference units in steps of 2.4e-4: radiance up to 1,048,576 at 1920x1080
// (16x the half-float maximum), 4,194,304 at 3840x2160, 29,127 at 320x180 and 7,282 at 160x90 (a pixel that
// bright is white after the tonemap anyway; past it the channel is flagged and shows 65504). A scene of many
// faint points raises the scale so that its smallest adds (flux * w) stay well above half a step; its
// brightest pixel sums must stay below 2^32 / fixedPointScale reference units.
//
// Nothing carries over between frames: the accumulators are cleared every frame, so a frame's splats are a
// pure function of the frame (a seek needs nothing reset).

#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>

namespace evo::modern {

struct Camera;

struct SplatFrame {
    std::uint32_t count = 0;        // ids 0 .. count - 1; at most the scene's SceneCapacity::splats
    std::array<float, 16> params{}; // the shader's vec4 params[4]
    float fixedPointScale = 4096.f; // fixed-point steps per reference unit (see above); finite, > 0
    // The footprint in the window (Interactive): 5 (5x5, as above) or 3 (3x3 taps, renormalised the same way;
    // cheaper for tens of millions of points). Exports always splat 5x5.
    std::uint32_t windowFootprint = 5;
};

// The dispatch runs rows of kSplatRowWidth ids; at most 65535 rows.
inline constexpr std::uint32_t kSplatRowWidth = 65536;
inline constexpr std::uint64_t kMaxSplats = std::uint64_t{kSplatRowWidth} * 65535;
// The reference size the flux is given at.
inline constexpr float kSplatReferencePixels = 1920.f * 1080.f;
// What a flagged or saturated channel resolves to: the half-float maximum.
inline constexpr float kSplatSaturated = 65504.f;

// The CPU validation of a frame's splats, which checkSceneFrame runs (the tracer's and the scene sweep's):
// none, or splats in a scene with a splat shader (else std::invalid_argument), a count of at most
// `capacity` (else std::length_error), a finite fixedPointScale > 0 and finite params (else
// std::invalid_argument).
void checkSplats(const std::optional<SplatFrame>& splats, std::uint32_t capacity,
                 const std::filesystem::path& shader);

// The splat pass: the accumulators (three R32_UINT images and the overflow image, at the trace size), a
// compute pipeline per scene shader and the resolve. One in ModernStage, made when the first scene with a
// splat shader is prepared, whatever the number of such scenes (Task 1b's decision: per-frame feature resources
// live once in ModernStage).
class SplatPass {
public:
    // Loads the resolve shader (shaderDir()/modern_splat_resolve.comp.spv) and makes the accumulators.
    // Throws std::invalid_argument for an empty size.
    SplatPass(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height);
    ~SplatPass();
    SplatPass(const SplatPass&) = delete;
    SplatPass& operator=(const SplatPass&) = delete;

    // Recreates the accumulators at the new size (the caller makes sure no submitted frame still uses them).
    void resize(std::uint32_t width, std::uint32_t height);
    // Loads a scene's splat shader now (setup) instead of in its first frame. A relative path is a file in
    // gfx::shaderDir() (modern_<scene>_splats.comp.spv); an absolute one is used as it is (tests). Throws
    // std::runtime_error naming the file when it cannot load.
    void prepare(const std::filesystem::path& shader);

    // Records the frame's splats: clears the accumulators, dispatches the shader (loaded on first use,
    // pipelines cached per path) over ids 0 .. count - 1, then resolves into `emission` (adds). `depth` is
    // the depth guide (R32_SFLOAT) and `emission` the emission guide (R16G16B16A16_SFLOAT), both at the
    // pass's size; both end in GENERAL. `seconds` is what the shader's splatPoint receives (ModernStage
    // passes the chapter-local show time, as FrameUniforms::seconds). A count of 0 records nothing. Throws,
    // recording nothing, std::length_error for more than kMaxSplats points, std::invalid_argument for a
    // fixedPointScale that is not finite and > 0, another image size or format, and what prepare() throws.
    // The scene's capacity is checked by checkSplats (through checkSceneFrame, which the tracer runs on the
    // same frame first). Between ring.begin() and ring.submit() only.
    // `footprint` is 5 (5x5 taps) or 3 (3x3; ModernStage passes SplatFrame::windowFootprint in the window).
    void record(VkCommandBuffer cmd, const std::filesystem::path& shader, const SplatFrame& splats,
                const Camera& camera, gfx::Image& depth, gfx::Image& emission, double seconds,
                std::uint32_t footprint = 5);

    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    // The accumulators (red, green, blue) and the overflow bits (bit c: channel c wrapped), as the last
    // record() left them; for tests.
    gfx::Image& accumulator(std::uint32_t channel) { return accumulators_.at(channel); }
    gfx::Image& overflow() { return overflow_; }
    // The device memory of the four images, as allocated (VMA).
    VkDeviceSize imageBytes() const;
    // How many splat pipelines this pass has loaded (one per shader path).
    std::size_t pipelineCount() const { return pipelines_.size(); }

private:
    void createImages();
    gfx::ComputePipeline& pipeline(const std::filesystem::path& shader);

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t width_ = 0, height_ = 0;
    std::array<gfx::Image, 3> accumulators_;
    gfx::Image overflow_;
    gfx::ComputePipeline resolve_;
    std::map<std::filesystem::path, std::unique_ptr<gfx::ComputePipeline>> pipelines_;
};

} // namespace evo::modern
