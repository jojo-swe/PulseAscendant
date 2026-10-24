# EVOLUTION Step 2: Render Foundation and the 1993 Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the step 0 test fixture with a real render foundation (frame ring, tracked images, compute passes, export-only readback) and build the 1993 side on it: an indexed 320×200 retro stage with palettes and cycling, frame-rate lock, 4:3 pillarbox and CRT look, an original 8×8 font with a text pass, the DOS boot chapter, the plasma, the sine scroller, and a compositor that already performs the era transitions. A storyboard tool captures every chapter in both eras so the result can be inspected.

**Architecture:** `evo::gfx` gains `Image` (tracks layout and emits sync2 barriers), `Buffer`, `FrameRing` (two frames in flight on one timeline semaphore; headless submit-and-wait), `ComputePipeline` (SPIR-V + push constants + a fixed binding list) and `Readback` (export only). `evo::render::Renderer` owns three stages: `RetroStage` renders the current chapter's 1993 effect into an R8 index image at 320×200 only when the 35 Hz (or per-effect) retro frame index changes, then maps it through a CPU-built 256-entry palette into RGBA8; `ModernStage` is a placeholder pattern until steps 4–5; `Compositor` scales the retro image into a 4:3 pillarbox with the CRT look, blends it with the modern image per `era`/`wipe`/`transition`, and draws text (epoch cards, chapter title, boot prompt, scroller) with the 8×8 font. Everything is a compute pass over storage images; the swapchain blit and the readback consume `Renderer::output()`.

**Tech Stack:** C++20, Vulkan 1.3 compute (sync2, timeline semaphores), GLSL 460, VMA, Python 3 (storyboard tool, PNG checks).

**Spec:** `evolution/docs/plans/2026-10-21-evolution-design.md` sections 2.2, 2.3 (chapters 0, 3, 10), 3.3, 3.6, 3.8; step 2 of section 7. Also the step 0 final-review recommendations recorded in `HANDOFF.md` ("Till steg 2, första task").

## Global Constraints

- Everything lives under `evolution/`; nothing outside it is modified. Namespaces `evo::gfx`, `evo::render`, `evo::retro`, `evo::text`. Style and commit rules as before (`.superpowers/sdd/*/global-constraints.md`).
- Retro resolution is exactly **320×200**, index format `VK_FORMAT_R8_UINT` (storage), palette-mapped image `VK_FORMAT_R8G8B8A8_UNORM` 320×200. Displayed at **4:3** with pixel aspect 1.2 (a 320×200 frame occupies a 4:3 rectangle, letter/pillar-boxed, centred, integer-independent: scale to fit the output height, width = height·4/3), nearest-neighbour source sampling before the CRT pass.
- Retro frame-rate lock: `retroFrame = floor(localSeconds · retroRate)`, `retroRate` = 35 by default, 10 for Raytrace (step 3); the retro passes run only when `retroFrame` or the chapter changes; a given (chapter, retroFrame) always produces the identical index image.
- Palette: 256 × RGBA8 built on the CPU per retro frame (`std::array<uint32_t,256>`), uploaded to a host-visible storage buffer; cycling is done by the CPU builder, never by the shader. Index 0 is always black.
- CRT pass (default on, `--no-crt` or key **C** off): scanline darkening 25 % on odd source lines, barrel distortion 2 % at the corners, phosphor triad mask at 6 % amplitude, 1 px bloom at 15 %, vignette 10 %. Off = plain nearest pillarbox.
- Output image of the renderer: `VK_FORMAT_R8G8B8A8_UNORM`, `width × height` from the options (render resolution); the window blits it (already scales); `--render-stream`/`--capture-dir` read it back.
- Compositor rules (from the step 1 spec): `Wipe` → columns left of `wipe·width` show 2026, right show 1993 (2026 sweeps in from the left), 2 px white seam; `BeatSwap`/`Cut` → `era` selects whole frame (0 = 1993, 1 = 2026), `eraCut` adds a white flash (`+0.6·eraCut`) ; `Portal` → cross-fade by `era` until the Studio exists (step 4 replaces it).
- Text: original 8×8 glyphs for ASCII 32–126 authored in `src/text/font8x8.cpp` (no copied ROM/VGA fonts), rendered by a compute pass from a glyph atlas image (`R8_UNORM`, 16 glyphs per row) and an instance buffer (`x, y, scale, rgba, glyph`); the retro variant writes palette indices into the R8 index image, the modern variant blends RGBA into the output.
- Time passed to shaders: `localSeconds` as float (chapter-local, < 40 s), `retroFrame` as uint, never raw show seconds as float.
- Determinism: for any show time, the output image is a pure function of (options, show time). Tests compare whole images.
- Tests run on lavapipe headless at small render sizes (e.g. 320×180, 640×360) to stay fast; the retro stage is always 320×200 regardless.

## Review Focus

1. **Window resize to a non-16:9 size** (user drags the window tall): the retro 4:3 box must stay centred with black bars on all sides as needed, never stretched. Pinned in Task 2 by a 400×600 render probing the bar/content boundary rows and columns.
2. **A chapter change exactly on a retro frame boundary** (chapter start times are beat times, not multiples of 1/35 s): the retro cache must invalidate on chapter change even when `retroFrame` happens to be equal. Pinned in Task 2 (two chapters, same `retroFrame`, different images).
3. **Text that runs off the 320×200 frame or the output** (long boot lines, scroller glyphs entering/leaving): no out-of-bounds stores, glyphs clipped per pixel. Pinned in Task 3 (string at x = 300 and x = −5, y = 195).
4. **`--no-crt` and the CRT pass must agree on geometry** (the user toggles C mid-show): the content rectangle is identical, only the look changes. Pinned in Task 2 (same bar boundaries with and without CRT).
5. **Headless export of many frames** must not leak or wait on a window: the frame ring's headless mode reuses one command buffer per frame and the readback happens after the fence, with no `vkQueueWaitIdle` in the windowed loop. Pinned in Task 1 (render 50 frames headless, VMA stats unchanged after the second frame; the windowed loop has zero `vkQueueWaitIdle` calls — asserted by a grep test over `src/app/app.cpp` and `src/render/`).

---

### Task 1: Render foundation (Image, Buffer, FrameRing, ComputePipeline, Readback)

**Suggested model:** opus (Vulkan plumbing with clear contracts).

**Files:**
- Create: `src/gfx/image.hpp/.cpp`, `src/gfx/buffer.hpp/.cpp`, `src/gfx/frame_ring.hpp/.cpp`, `src/gfx/compute_pipeline.hpp/.cpp`, `src/gfx/readback.hpp/.cpp`, `src/gfx/sync.hpp` (shared `imageBarrier2` helper; remove the duplicates in offscreen.cpp and swapchain.cpp)
- Modify: `src/gfx/offscreen.hpp/.cpp` → becomes a thin test fixture built on the new classes (same public interface so `offscreen_test` and `capture` keep passing), `src/app/app.cpp` (windowed loop uses `FrameRing`; no `vkQueueWaitIdle` per frame; Context created before the window and `SDL_Vulkan_LoadLibrary(nullptr)` first), `src/gfx/swapchain.cpp` (present takes the ring's command buffer and the timeline semaphore value to wait on instead of recording its own blit submit; or records the blit into the ring's command buffer — choose: the blit is recorded into the ring's command buffer and `present()` only does acquire/submit/present with the ring's semaphores)
- Test: `tests/gfx_foundation_test.cpp`, `tests/no_wait_idle_test.py`

**Interfaces:**
```cpp
namespace evo::gfx {
struct ImageDesc { uint32_t width, height; VkFormat format; VkImageUsageFlags usage; uint32_t mipLevels = 1; const char* name = ""; };
class Image {   // owns VkImage + VkImageView + VmaAllocation; tracks layout/stage/access
public:
    Image() = default; Image(Context&, const ImageDesc&); ~Image(); Image(Image&&) noexcept; Image& operator=(Image&&) noexcept;
    void transition(VkCommandBuffer, VkImageLayout newLayout, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess); // emits barrier2 from tracked state; no-op when identical layout and a read→read hazard
    VkImage handle() const; VkImageView view() const; VkImageLayout layout() const; VkExtent2D extent() const; VkFormat format() const;
};
struct BufferDesc { VkDeviceSize size; VkBufferUsageFlags usage; bool hostVisible; const char* name = ""; };
class Buffer { public: Buffer() = default; Buffer(Context&, const BufferDesc&); /* RAII, movable */ void* mapped() const; VkBuffer handle() const; VkDeviceSize size() const; void flush() const; void invalidate() const; VkDeviceAddress address() const; };
class FrameRing {   // kFramesInFlight = 2; one timeline semaphore; per frame: command pool + primary buffer
public:
    explicit FrameRing(Context&);
    VkCommandBuffer begin();          // waits (timeline) until this slot's previous submit completed, resets its pool, begins recording
    uint64_t submit(VkSemaphore waitBinary = VK_NULL_HANDLE, VkPipelineStageFlags2 waitStage = 0, VkSemaphore signalBinary = VK_NULL_HANDLE); // ends recording, submits; returns the timeline value signalled
    void wait(uint64_t value);        // host wait on the timeline
    void waitAll();                   // all slots
    uint32_t slot() const; VkSemaphore timeline() const;
};
struct Binding { uint32_t binding; VkDescriptorType type; };   // STORAGE_IMAGE, STORAGE_BUFFER, SAMPLED_IMAGE, SAMPLER, UNIFORM_BUFFER
class ComputePipeline {
public:
    ComputePipeline(Context&, const std::filesystem::path& spv, std::span<const Binding>, uint32_t pushConstantBytes);
    // A descriptor set per call from a growable pool, written with the given resources (in binding order) and bound; sets are recycled per FrameRing slot.
    void bind(VkCommandBuffer, FrameRing&, std::span<const VkDescriptorImageInfo> images, std::span<const VkDescriptorBufferInfo> buffers);
    void push(VkCommandBuffer, const void* data, uint32_t bytes);
    void dispatch(VkCommandBuffer, uint32_t width, uint32_t height, uint32_t localX = 8, uint32_t localY = 8);
};
class Readback {    // export only
public:
    Readback(Context&, uint32_t width, uint32_t height);   // host-visible buffer sized width*height*4
    void record(VkCommandBuffer, Image& src);               // transitions src to TRANSFER_SRC, copies, buffer barrier to host
    std::vector<std::uint8_t> rgb() const;                  // after the submit completed: invalidate + RGBA→RGB top-down
};
}
```
Decisions: the descriptor pool per pipeline holds 64 sets per slot and is reset per slot at `FrameRing::begin()` via a callback list the ring keeps (pipelines register themselves). Samplers: `Context` gets `nearestSampler()` and `linearSampler()`.

- [ ] **Step 1: Write the failing tests** `gfx_foundation_test.cpp`: (a) `Image` transition sequence UNDEFINED→GENERAL→TRANSFER_SRC updates `layout()`; a second transition to the same layout with read access records no barrier (count via a wrapped `vkCmdPipelineBarrier2` hook? simpler: assert `layout()` and trust validation — the test runs with validation on and asserts `validationMessageCount()==0`); (b) `FrameRing`: 50 headless frames each dispatching the fill pass through `ComputePipeline` into an `Image` then `Readback`, every frame's RGB identical for identical time and different for different time; `vmaGetHeapBudgets` usage equal between frame 2 and frame 50 (Review Focus 5); (c) `Buffer` host-visible write/flush/readback round trip. `no_wait_idle_test.py`: greps `src/app/app.cpp` and `src/render/**` for `vkQueueWaitIdle|vkDeviceWaitIdle` and fails on any hit outside resize/shutdown paths (allow-list lines containing `// shutdown` or `// resize`).
- [ ] **Step 2: Run to verify they fail.** **Step 3: Implement**; port `Offscreen` and the windowed loop. **Step 4: Full `ctest --preset linux`** (all step 0/1 tests still green, including `windowed` with `EVO_RUNTIME_TESTS=ON` once). **Step 5: Commit** `Build the render foundation: tracked images, a frame ring and compute passes`.

---

### Task 2: Renderer, retro stage, CRT pillarbox and compositor

**Suggested model:** opus.

**Files:**
- Create: `src/render/renderer.hpp/.cpp`, `src/render/retro_stage.hpp/.cpp`, `src/render/modern_stage.hpp/.cpp` (placeholder: the fill pattern tinted by chapter, from step 1's Task 6 shader), `src/render/compositor.hpp/.cpp`, `src/retro/palette.hpp/.cpp` (palette builders), `src/retro/effects.hpp` (registry: effect → compute shader name, retro rate, palette builder)
- Create shaders: `shaders/retro/common.glsl` (bayer4, hash), `shaders/retro/test_pattern.comp` (index test pattern: horizontal bands 0–255 and a moving block, used until real effects exist), `shaders/retro/palette_map.comp` (R8 index → RGBA8 via the 256-entry buffer), `shaders/crt.comp` (RGBA8 320×200 → output-size retro image with pillarbox + CRT or plain), `shaders/composite.comp` (retro image + modern image + era state → output)
- Modify: `src/app/app.cpp`, `src/app/capture.cpp` (use `Renderer`), `src/app/options.*` (`--no-crt`), key **C**
- Test: `tests/renderer_test.cpp`

**Interfaces:**
```cpp
namespace evo::render {
struct RenderSettings { uint32_t width, height; bool crt = true; };
class Renderer {
public:
    Renderer(gfx::Context&, gfx::FrameRing&, const RenderSettings&);
    void resize(uint32_t width, uint32_t height);
    void render(VkCommandBuffer, const show::TimelineFrame&);   // records all passes; output() is GENERAL afterwards
    gfx::Image& output();
    void setCrt(bool);
    struct Stats { bool retroRendered; uint32_t retroFrame; } lastStats() const;
};
}
namespace evo::retro {
using PaletteBuilder = void(*)(std::array<uint32_t,256>&, const show::TimelineFrame&, uint32_t retroFrame);
struct EffectInfo { const char* shader; uint32_t retroRate; PaletteBuilder palette; };
const EffectInfo& effectFor(show::Effect);   // step 2 maps every effect to test_pattern.comp except Plasma/Boot/Credits added in later tasks
void grayPalette(std::array<uint32_t,256>&, const show::TimelineFrame&, uint32_t);   // 0 black … 255 white
}
```
Compositor push constants: `{uint width, height; float era, wipe, eraCut; uint transition, crt; float localSeconds}`. The retro stage runs its two passes (effect, palette_map) only when `(chapter, retroFrame)` changed since the last render; the CRT pass runs every frame (output size may change). `capture` and `render-stream` build a `Renderer` at the requested size and use `Readback`.

- [ ] **Step 1: Write the failing test** `renderer_test.cpp` (headless, validation on, asserts zero validation messages at the end): (a) 640×360 render at Boot t=1: pillarbox geometry — columns 0..(640−480)/2−1 = 0..79 black, column 80 non-black at a row in the middle (test pattern band), rows all content (4:3 of 360 high = 480 wide); (b) 400×600 (Review Focus 1): content rectangle 400×300 centred vertically → rows 0..149 black and rows 150..449 content; (c) `--no-crt` vs crt: same first/last content column and row (Review Focus 4); (d) determinism: render t=1.000 and t=1.010 (same retroFrame 35) → identical retro image bytes (compare output with CRT off), t=1.03 (retroFrame 36) differs; (e) Review Focus 2: construct two `TimelineFrame`s with different `chapter` but equal `localSeconds` → outputs differ (the test pattern's moving block uses `chapter` as a seed); (f) wipe: a frame with `transition=Wipe`, `wipe=.5` → left half equals the modern-only render, right half equals the retro-only render, except the 2 px seam; (g) `eraCut=1` brightens.
- [ ] **Step 2: Fail.** **Step 3: Implement.** **Step 4: Run** `ctest -R "renderer|offscreen|capture|render_stream"`. **Step 5: Commit** `Render the 1993 stage through a palette, a CRT and the era compositor`.

---

### Task 3: 8×8 font and the text pass

**Suggested model:** opus (pixel-art authoring plus a pass).

**Files:**
- Create: `src/text/font8x8.hpp/.cpp` (`const std::array<std::uint8_t,8>& glyph(char)`; 95 glyphs, each row MSB = leftmost pixel; original design: 7 px wide max, 1 px gap, baseline at row 6, descenders on row 7), `src/text/atlas.hpp/.cpp` (builds the `R8_UNORM` 128×48 atlas image at startup), `src/text/text_pass.hpp/.cpp` (instance buffer + two pipelines), `shaders/text_index.comp`, `shaders/text_rgba.comp`
- Modify: `src/render/renderer.cpp` (epoch cards bottom-left at 2× scale, chapter title top-left at 1×, HUD toggle), `src/render/compositor.cpp` (calls the rgba text pass last), `src/app/app.cpp` (key **H** toggles HUD; `--no-hud`)
- Test: `tests/font_test.cpp`, `tests/text_pass_test.cpp`

**Interfaces:**
```cpp
namespace evo::text {
struct Item { float x, y; float scale; std::uint32_t rgba; std::string text; };   // y grows downwards; rgba used by the rgba variant; for the index variant the low byte is the palette index
class TextPass {
public:
    TextPass(gfx::Context&, gfx::FrameRing&);
    void drawIndex(VkCommandBuffer, gfx::Image& indexImage, std::span<const Item>);   // clips to the image
    void drawRgba(VkCommandBuffer, gfx::Image& rgbaImage, std::span<const Item>);     // alpha-blends
};
}
```
- [ ] **Step 1: Write the failing tests**: `font_test`: every printable glyph has at least 4 set pixels except space (0); no glyph uses column 7 (the gap) except for a listed set (e.g. `W`, `M`, `@`); `A` row 0 equals `0b00111000`?? — no: do not pin exact art; pin invariants: `I` is horizontally symmetric, `-` has exactly one non-empty row, digits `0`–`9` are pairwise different. `text_pass_test`: draw `"A"` at (8,8) scale 1 into a 64×32 index image with index 15 → the set pixels of `glyph('A')` appear at (8+col, 8+row) and nowhere else; Review Focus 3: items at x=300 (into a 320×200 image) and x=−5, y=195 render without validation messages and only in-bounds pixels change; scale 2 doubles each pixel.
- [ ] **Step 2: Fail.** **Step 3: Implement.** **Step 4: Run.** **Step 5: Commit** `Draw text with an original 8x8 font in both eras`.

---

### Task 4: BOOT chapter

**Suggested model:** opus.

**Files:**
- Create: `src/retro/boot.hpp/.cpp` (the typed-prompt script as data: lines with start times, per-character typing at 25 chars/s, cursor blink 2 Hz), registered in `effects.hpp` for `Effect::Boot` (retro rate 35, palette: index 0 black, 7 light grey `#AAAAAA`, 15 white, 8 dark grey)
- Script, verbatim: t=0.0 `C:\>` (cursor only until 0.8 s) · 0.8 type `cd demo` · 1.6 `C:\DEMO>` · 2.2 type `evolution.exe` · 3.4 blank line then `EVOLUTION  1993 -> 2026` · 4.0 `Sound Blaster 16 at 220h, IRQ 5, DMA 1 ... OK` · 4.8 `Gravis Ultrasound ... not found` · 5.4 `VGA 320x200x256 ... OK` · 6.0 `Loading` with dots appended every 0.25 s up to 6 · then at chapter accent (from the frame) a white frame for 2 retro frames, then black until the chapter ends.
- Modify: `src/render/retro_stage.cpp` (effects that are text-only run the index text pass instead of a compute shader: `EffectInfo.shader == nullptr`)
- Test: `tests/boot_test.cpp`: frames at t=0.5 (cursor only, ≤ 70 set pixels), t=3.0 (contains the `evolution.exe` glyph row pixels), t=5.0 (more non-zero pixels than at 3.0), the two renders at t=2.00 and t=2.02 identical (same retro frame), the white frame at accent has > 90 % index 15.
- [ ] Steps: failing test → fail → implement → run → commit `Boot the demo from a 1993 prompt`.

---

### Task 5: PLASMA retro effect

**Suggested model:** opus.

**Files:**
- Create: `shaders/retro/plasma.comp` (nested sine fields on integer pixel coordinates, 320×200, index = 32 + 95·(0.5 + 0.5·f) folded into a 0–255 ramp, `localSeconds` quantised by `retroFrame/35`), `src/retro/palettes.cpp` (`plasmaPalette`: three 85-entry gradients cycling by `retroFrame` — cycling means rotating indices 32–255 by `retroFrame % 224`)
- Register `Effect::Plasma` (rate 35).
- Test: `tests/plasma_test.cpp`: index image at retroFrame 10 has ≥ 64 distinct indices; palette at retroFrame 10 and 11 differ in ≥ 200 entries but the index image is identical for the same retroFrame; the displayed (palette-mapped) image differs between consecutive retro frames.
- [ ] Steps: failing test → fail → implement → run → commit `Cycle a 256-colour plasma at 320x200`.

---

### Task 6: Sine scroller (CREDITS and FLYBY retro text)

**Suggested model:** opus.

**Files:**
- Create: `src/retro/scroller.hpp/.cpp` (`std::vector<text::Item> scrollerItems(const std::string&, double localSeconds, uint32_t retroFrame, float speedPxPerSec, float amplitude, float wavelength)`: glyphs at x = 320 − speed·t + 8·i, y = 150 + amplitude·sin((x + phase)/wavelength), index 15; text for Credits verbatim: `   EVOLUTION  ...  CODE: CLAUDE  ...  MUSIC: JOYOLO (SUNO)  ...  CONCEPT: JOHN  ...  GREETINGS TO FUTURE CREW AND EVERYONE WHO WAS THERE IN 1993  ...  REALITY IS WHAT YOU RENDER  ...` (looping); Flyby's retro text `WE WERE THERE IN 1993 ...` at y 180, amplitude 6)
- Register `Effect::Credits` (text-only, rate 35, grey palette) and the Flyby retro overlay hook (`EffectInfo.overlay` scroller string; Flyby's main shader stays the test pattern until step 3).
- Test: `tests/scroller_test.cpp`: at t=0 the first glyph is off-screen right (x ≥ 320); at t=4 with speed 60 it is at x ≈ 80 (±1); y stays within [150−amplitude, 150+amplitude]; the index image at t=4 contains pixels of the first letter at its computed position.
- [ ] Steps: failing test → fail → implement → run → commit `Scroll credits and greetings on a sine wave`.

---

### Task 7: Storyboard tool, README, handoff

**Suggested model:** opus.

**Files:**
- Create: `tools/storyboard.py` (`--app`, `--out`, `--width 640 --height 360`, optional `--chapters 0,3`; for every chapter captures at 20 %, accent and 80 % of the chapter with `--capture-dir`, both with and without `EVO_FORCE_ERA` (a new env var read by the app: `0`/`1` forces `era` for inspection; documented as diagnostic), and assembles one PNG grid per act (pure-Python PNG compose using `check_png.py`'s decoder and `src/image/png` semantics re-implemented in Python, or simpler: write a `--grid` mode that calls the executable's capture per cell and composes via numpy + the existing `write_png` logic in Python) — choose numpy + a tiny PNG writer in Python (zlib available).
- Modify: `README.md` (retro pipeline, CRT toggle, HUD, storyboard usage), `docs/plans/HANDOFF.md`.
- Test: CTest `storyboard` runs the tool on chapters 0 and 3 at 320×180 and asserts the grid PNG exists and decodes.
- [ ] Steps: failing test → fail → implement → run → commit `Capture a storyboard of every chapter in both eras`.

---

## Self-review notes

- Spec 3.6 items 1–7 → Tasks 2 (1–6) and 3 (7); 3.3 frame graph foundation → Task 1; 2.3 chapters 0, 3, 10 → Tasks 4, 5, 6; 3.8 headless export → Task 1 readback + Task 2 wiring; step 0 final-review foundation advice → Task 1.
- Names used across tasks: `Image`, `Buffer`, `FrameRing`, `ComputePipeline`, `Readback`, `Renderer`, `RenderSettings`, `RetroStage`, `ModernStage`, `Compositor`, `EffectInfo`, `effectFor`, `PaletteBuilder`, `TextPass`, `Item`, `glyph`, `scrollerItems`.
- Review Focus 1, 2, 4 → Task 2; 3 → Task 3; 5 → Task 1.
- Modern stage is deliberately a placeholder; steps 4–5 replace it. Portal = cross-fade until the Studio exists.
