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
class PathTracer;
class SceneGpu;
} // namespace evo::modern

namespace evo::post {
class Denoiser;
} // namespace evo::post

namespace evo::render {

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
// Interactive denoises temporally with 3 a-trous iterations; Export has no temporal history and filters with
// 3 a-trous iterations below 16 spp and not at all from 16 (Decision 6 of step 4). The denoiser's history
// starts over whenever state.reset is not None, and the motion guide uses the previous frame's camera only
// while the history continues (never in Export). The primary hit's emission (the CRT's picture) bypasses the
// history and the filter, so the screen stays sharp and current (Review Focus 5 of step 4).
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
    ModernStage(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t width, std::uint32_t height,
                modern::SceneDetail detail = {}, modern::SceneFactory factory = {},
                modern::SceneMap map = {});
    ~ModernStage();
    ModernStage(const ModernStage&) = delete;
    ModernStage& operator=(const ModernStage&) = delete;

    // Builds a path-traced scene's GPU side now (and the shared tracer and denoiser on the first), instead of
    // in the first frame that shows it (setup: before a frame, not between ring.begin() and submit()). A
    // no-op for the placeholder and for a scene already prepared. Throws std::invalid_argument for a scene
    // the factory does not make.
    void prepare(modern::SceneId id);
    // prepare() for every scene the effects map to (the window).
    void prepareAll();
    // The device memory of what the stage holds for path tracing: every prepared scene's SceneGpu::bytes(),
    // plus imageBytes() once one is prepared.
    VkDeviceSize gpuBytes() const;
    // The images path tracing adds, whatever the number of scenes: the tracer's G-buffer, the denoiser's
    // images and the stage's HDR and display images (0 before the first prepare).
    VkDeviceSize imageBytes() const;
    // Recreates the images at the new size. The caller makes sure no submitted frame still uses the old ones.
    void resize(std::uint32_t width, std::uint32_t height);
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

private:
    // The scene object of an id, built on first use (CPU only).
    const modern::ModernScene& scene(modern::SceneId id) const;
    void renderTraced(VkCommandBuffer cmd, modern::SceneId id, const show::TimelineFrame& frame,
                      const HistoryState& state, gfx::Image* retroImage);

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    std::uint32_t width_, height_;
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
    std::unique_ptr<post::Denoiser> denoiser_;
    std::unique_ptr<modern::PathTracer> tracer_;
    std::optional<modern::Camera> previousCamera_; // the last frame's, while the history continues
};

} // namespace evo::render
