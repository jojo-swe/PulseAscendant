#pragma once

#include "gfx/context.hpp"
#include "gfx/fill_pass.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "post/tonemap.hpp"
#include "render/stage_plan.hpp"
#include "show/timeline.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace evo::modern {
class CausticMaps;
class PathTracer;
class SceneGpu;
} // namespace evo::modern

namespace evo::post {
class Denoiser;
class Upscaler;
} // namespace evo::post

namespace evo::gfx {
class GpuTimers;
} // namespace evo::gfx

namespace evo::rt {
class ParticleField;
} // namespace evo::rt

namespace evo::render {

// The size the 2026 side traces at for an output size and a trace scale (Task 4 of step 5): per axis
// max(16, round(output * scale)), never above the output. Scale 1 traces at the output size.
VkExtent2D traceSize(std::uint32_t width, std::uint32_t height, float scale);
// Whether a trace scale is usable: finite, in (0, 1].
bool validTraceScale(float scale);

// The 2026 side. Every frame renders HDR linear light into an RGBA16F image at the render resolution, then
// the one tonemap (post::Tonemap: PBR Neutral, sRGB OETF, static dither) writes the display-encoded RGBA8
// image the compositor reads. The scene comes from the scene map (modern::sceneFor in the show):
//   Placeholder  the fill.comp pattern's 2026 look (a smooth gradient in the chapter's hue with the circle
//                that follows kick and beat) as HDR light that the tonemap maps back to that look
//                (fill_hdr.comp), at 0 EV;
//   any other    a path-traced scene (the Studio, and step 5's scenes): the one PathTracer -> the one
//                Denoiser -> the HDR image, tone mapped at the scene frame's exposure. A scene that samples
//                the retro image (the Studio's CRT) reads the retro stage's colour image.
// Either way whole-frame 2026 whatever the era state; the compositor decides where it shows.
//
// Every path-traced scene shares one PathTracer (pipeline, TLAS, G-buffer) and one Denoiser, built on the
// first prepare; each prepared scene adds only its SceneGpu (geometry, BLASes, materials), so the images do
// not multiply with the scenes (Review Focus 5 of step 5). The denoiser's history starts over on every
// chapter change, so it never carries one scene into another.
//
// Path tracing follows the HistoryState the renderer hands over: TraceSettings {spp, maxBounces, frameSeed};
// the denoiser follows denoiseSettingsFor(state) (stage_plan.hpp): Interactive denoises temporally with 3
// a-trous iterations; Export has no temporal history and filters with 3 below 16 spp and not at all from 16
// (Decision 6), where the firefly filter clamps lone fireflies instead (step 5 Decision 16); 5 iterations
// at history length <= 2 either way. The denoiser's history starts over
// whenever state.reset is not None, and the motion guide uses the previous frame's camera only while the
// history continues (never in Export). The primary hit's emission (the CRT's picture) bypasses the history
// and the filter, so the screen stays sharp and current (Review Focus 5).
//
// The trace size (Task 4 of step 5): the tracer and the denoiser run at traceSize(width, height, traceScale);
// below the output size the denoised picture goes through the temporal upscaler (post::Upscaler) into the
// HDR image, with the G-buffer's emission kept out of its history (the CRT stays sharp and current). The
// window jitters the samples within each trace pixel (post::subpixelWindow, from the frames since the
// history reset) and the upscaler accumulates them; a reset frame and every Export frame trace whole pixels
// unjittered and upsample spatially, so the window's first frame after a reset equals an Export frame at the
// same trace scale (Review Focus 3), and the upscaler's history resets with the denoiser's. At scale 1 there
// is no upscaler pass at all: the denoiser writes the HDR image, exactly as before.
//
// A frame with a particle field (Task 8) first runs its scene's particle shader into the stage's one
// rt::ParticleField (made on the first prepared scene with a particle shader and grown to the largest
// capacity.particles prepared), at the frame's chapter-local time (prevCenter at the previous frame's while
// the history continues, else at the same time), then traces with that slot's spheres and BLAS.
//
// A frame with caustics (Task 13) shoots its photons into the stage's one modern::CausticMaps (made on the
// first prepared scene with capacity.causticResolution > 0, grown to the largest) inside the trace, after
// the TLAS build: seeded by frameSeedFor(showSeconds, 0) alone, so the map is a pure function of the show
// time, with kExportPhotonFactor times the frame's photons in an export of kExportPhotonSpp spp or more.
//
// GPU timers (setTimers): "particles" (the particle field), "tlas" and "trace" (the tracer), "denoise",
// "upscale" (below scale 1), "tonemap"; "fill" and "tonemap" for the placeholder.
//
// A scene's GPU side is built by prepare(id) (or prepareAll()), which the window and the exports call before
// their first frame, so that no chapter hitches. A stage that was not prepared builds it on the first frame
// that shows the scene, inside render(): blocking setup (immediate submits that wait for the queue) recorded
// while the frame's command buffer is still open, which is legal and ordered before that frame's submit, and
// leaves the same state as prepare() (the first frame is byte for byte the same). A stage that never shows a
// path-traced scene, like the 1993-only tests', never pays for one. Scene objects (the CPU side) are built
// on first use too, at the stage's detail, by its factory.
class ModernStage {
public:
    // `factory` makes the scenes (empty: modern::makeScene) and `map` says which scene an effect shows
    // (empty: modern::sceneFor); tests inject both. Scenes are built at `detail`.
    // Throws std::invalid_argument for a trace scale validTraceScale refuses.
    ModernStage(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height,
                modern::SceneDetail detail = {}, modern::SceneFactory factory = {}, modern::SceneMap map = {},
                float traceScale = 1.f);
    ~ModernStage();
    ModernStage(const ModernStage&) = delete;
    ModernStage& operator=(const ModernStage&) = delete;

    // Builds a path-traced scene's GPU side now (and the shared tracer and denoiser on the first), instead of
    // in the first frame that shows it (setup: before a frame, not between ring.begin() and submit()). A
    // no-op for the placeholder and for a scene already prepared. Throws std::invalid_argument for a scene
    // the factory does not make; whatever it throws, the stage is left as it was (the scene unprepared).
    void prepare(modern::SceneId id);
    // prepare() for every scene the effects map to (the window).
    void prepareAll();
    // The device memory of what the stage holds for path tracing, as allocated (VMA statistics count the
    // same): every prepared scene's SceneGpu::bytes(), plus imageBytes() and the tracer's bufferBytes()
    // (TLAS, per-slot buffers, SBT, dummies) once one is prepared, and the particle field's bytes() once a
    // scene with particles is; 0 before.
    VkDeviceSize gpuBytes() const;
    // The images path tracing adds, whatever the number of scenes, as allocated: the tracer's G-buffer and
    // the denoiser's images (at the trace size), below scale 1 the denoised trace-size picture and the
    // upscaler's history, and the stage's HDR and display images (0 before the first prepare).
    VkDeviceSize imageBytes() const;
    // Recreates the images at the new size. The caller makes sure no submitted frame still uses the old ones.
    void resize(std::uint32_t width, std::uint32_t height);
    // A new trace scale: the trace-size images are recreated (as resize(); the caller resets the history).
    // A no-op for the current scale; throws std::invalid_argument for one validTraceScale refuses.
    void setTraceScale(float scale);
    float traceScale() const { return traceScale_; }
    VkExtent2D traceExtent() const { return {traceWidth_, traceHeight_}; }
    // The subpixel jitter of the last frame render() traced ((0, 0) after a placeholder frame).
    std::array<float, 2> lastTraceJitter() const { return lastJitter_; }
    // GPU timer scopes for the passes the stage records (null: none). The timers must outlive the stage's
    // use.
    void setTimers(gfx::GpuTimers* timers);
    // Records the frame. `retroImage` is the retro stage's colour image (R8G8B8A8_SRGB, mipmapped), which a
    // scene that samples it (the Studio's CRT) reads; a scene that does not ignores it (it may then be null).
    // Throws std::invalid_argument for a scene that samples the retro image without one.
    void render(VkCommandBuffer cmd, const show::TimelineFrame& frame, const HistoryState& state,
                gfx::Image* retroImage = nullptr);
    // The scene an effect shows (the stage's map).
    modern::SceneId sceneOf(show::Effect effect) const;
    // The camera shot of a frame (a change resets the history): the scene camera's (from camera(), not
    // frame()), 0 for the placeholder.
    std::uint32_t shotFor(const show::TimelineFrame& frame) const;
    // Whether the frame's 2026 picture shows the retro image (the Studio's CRT).
    bool samplesRetro(const show::TimelineFrame& frame) const;

    // The display-encoded picture (R8G8B8A8_UNORM): what the compositor shows.
    gfx::Image& image() { return image_; }
    // The HDR light it was tone mapped from (R16G16B16A16_SFLOAT).
    gfx::Image& hdr() { return hdr_; }
    // The shared path tracer, once a scene is prepared (tests read its G-buffer); null before.
    modern::PathTracer* tracer();
    // A prepared scene's GPU side; null for one not prepared (and the placeholder).
    const modern::SceneGpu* sceneGpu(modern::SceneId id) const;
    // The particle field every scene with particles shares; null until one is prepared.
    const rt::ParticleField* particles() const { return particles_.get(); }
    // The caustic maps every scene with caustics shares (Task 13); null until one is prepared.
    const modern::CausticMaps* caustics() const { return caustics_.get(); }

private:
    // The scene object of an id, built on first use (CPU only).
    const modern::ModernScene& scene(modern::SceneId id) const;
    void renderTraced(VkCommandBuffer cmd, modern::SceneId id, const show::TimelineFrame& frame,
                      const HistoryState& state, gfx::Image* retroImage);
    bool upscales() const { return traceWidth_ != width_ || traceHeight_ != height_; }
    // The trace-size images (tracer, denoiser, upscaler, the denoised picture) at the current sizes.
    void resizeTraced();
    void beginScope(VkCommandBuffer cmd, const char* name);
    void endScope(VkCommandBuffer cmd);

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t width_, height_;
    float traceScale_;
    std::uint32_t traceWidth_, traceHeight_;
    modern::SceneDetail detail_;
    modern::SceneFactory factory_;
    modern::SceneMap map_;
    gfx::FillPass fill_; // the placeholder, as HDR light
    post::Tonemap tonemap_;
    gfx::Image hdr_;
    gfx::Image image_;
    // Declared in the order they must outlive each other: a SceneGpu reads its scene's static side, and the
    // tracer's TLAS (destroyed first: it waits for the ring's frames) points at the SceneGpus' BLASes.
    mutable std::array<std::unique_ptr<modern::ModernScene>, modern::kSceneCount> scenes_;
    std::array<std::unique_ptr<modern::SceneGpu>, modern::kSceneCount> gpus_;
    std::unique_ptr<rt::ParticleField> particles_;  // its BLASes outlive the tracer's TLAS
    std::unique_ptr<modern::CausticMaps> caustics_; // Task 13: sized to the largest causticResolution
    std::unique_ptr<post::Denoiser> denoiser_;
    std::unique_ptr<post::Upscaler> upscaler_;
    gfx::Image traceHdr_; // the denoised picture at the trace size, below scale 1 (else empty)
    std::unique_ptr<modern::PathTracer> tracer_;
    std::optional<modern::Camera> previousCamera_; // the last frame's, while the history continues
    double previousSeconds_ = 0;                   // the last traced frame's chapter-local time
    gfx::GpuTimers* timers_ = nullptr;
    std::array<float, 2> lastJitter_{0.f, 0.f};
};

} // namespace evo::render
