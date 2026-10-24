# EVOLUTION Step 4: Ray Tracing Foundation and the Studio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The 2026 side becomes a real path tracer: compacted BLASes and a per-frame TLAS, a ray tracing pipeline with one closest-hit per material class, physically based materials (GGX metal and plastic, rough and smooth dielectric glass, textured emitters, a procedural checker), next-event estimation with multiple importance sampling against area lights, the live 1993 screen and a window onto a night sky, temporal accumulation plus an à-trous (SVGF-lite) denoiser in the window, many samples per pixel at export, an ACES tonemap, and the Studio — a dark room with a 1993 PC whose CRT shows the live retro image and lights the room. Passes that a frame does not need no longer run, and temporal history resets wherever the picture is discontinuous.

**Architecture:** `evo::gfx` gains a shared descriptor layer (layouts, per-ring-slot allocators, writes that include acceleration structures), mipmapped and depth images, aligned device-local buffers, a `RayTracingPipeline` with a computed shader binding table and sync validation as a context option. `evo::rt` packs every mesh of a scene into one geometry store, builds one compacted BLAS per mesh at setup and a TLAS every frame from per-slot instance buffers. `evo::modern` holds the scene interface, procedural mesh builders, the path tracer (one raygen that loops bounces and does NEE/MIS; closest-hit shaders only evaluate their material class into the payload; recursion depth 1) and the Studio. `evo::post` holds the denoiser (demodulate → reproject/accumulate → à-trous ×3 → remodulate + primary emission) and the tonemap (HDR RGBA16F → display-encoded RGBA8). `evo::render` decides per frame which stages run (`planStages`) and when history resets (`HistoryTracker`); the compositor never reads a skipped stage's image. Export (`--capture-dir`, `--render-stream`) is stateless: N spp per frame in batched dispatches, seed from the show time. The window is interactive: 1 spp, history since the last reset.

**Tech Stack:** C++20, Vulkan 1.3 with `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`, `VK_KHR_ray_query` (already required by `Context`), GLSL 460 (`GL_EXT_ray_tracing`, `GL_EXT_ray_query`, `GL_EXT_scalar_block_layout`), VMA, the step 2 foundation (`Image`, `Buffer`, `FrameRing`, `ComputePipeline`, `Readback`), Python 3 for the export tests and the storyboard.

**Spec:** `evolution/docs/plans/2026-10-21-evolution-design.md` §2.1 (the Studio), §2.4 (Act II/III Studio chapters), §3.1–3.2 (stack, `rt/`, `modern/`, `post/`), §3.3 (frame graph; "no pass runs that the chapter does not need"), §3.4 (path tracing), §3.6 last paragraph (the CRT samples the retro image as an emissive texture), §3.7 (determinism), §3.8–3.9 (export, `--spp`), §6 (verification), §7 step 4 (done when "the Studio path traced in lavapipe, determinism test green"), §8 (denoise ghosting, lavapipe speed, Windows). `HANDOFF.md` "Rekommendationer till steg 4" and the deferred step 2 Important 3 (stage skipping with history reset).

## Global Constraints

- Everything under `evolution/`; namespaces `evo::gfx`, `evo::rt`, `evo::modern`, `evo::post`, `evo::render`. New libraries: `evo_rt` (`src/rt/`, links `evo_gfx`), `evo_post` (`src/post/`, links `evo_gfx`), `evo_modern` (`src/modern/`, links `evo_rt evo_post evo_show`); `evo_render` links them. Style, commit rules and trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` as in the SDD global-constraints file.
- No new device extensions. New device features only where a task names them (none planned). Ray tracing: `maxPipelineRayRecursionDepth = 1` (only the raygen traces); all geometry `VK_GEOMETRY_OPAQUE_BIT_KHR`, no any-hit shaders; every instance `VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR`; `static_assert(sizeof(VkAccelerationStructureInstanceKHR) == 64)`.
- Every struct shared with GLSL uses `layout(scalar)` on the GPU and a C++ mirror of plain `float`/`std::uint32_t` fields with `static_assert` on `sizeof` and every `offsetof`; each is checked on the GPU by the layout test of Task 6 (extended in Task 7). Never `vec3` in std140/std430.
- Shaders compile with the existing `evo_add_shaders` rule (`--target-env vulkan1.3`, SPIR-V 1.6, depfiles); new sources in `shaders/rt/` and `shaders/post/` load as `rt_<name>.<stage>.spv` and `post_<name>.comp.spv`. Test-only shaders go to `EVO_TEST_SPIRV_DIR` like `test_raytrace_quantise.comp.spv`.
- Image formats: radiance accumulation `R32G32B32A32_SFLOAT`; guides, history and the modern HDR image `R16G16B16A16_SFLOAT` (no RG16F: it needs `shaderStorageImageExtendedFormats`); depth guide `R32_SFLOAT` (linear view depth, `-1` for a miss); the modern LDR image and the renderer output stay `R8G8B8A8_UNORM` holding display-encoded (sRGB OETF in the shader) values, like the retro side. Storage bindings use single-level views (`Image::view()`), samplers use `Image::sampledView()`.
- The retro colour image is `R8G8B8A8_SRGB` (mutable format, since Task 3): `Image::view()` is a UNORM storage view whose `imageLoad` returns encoded bytes (decode with `srgbToLinear` if treated as light); `Image::sampledView()` is an SRGB view whose samples are already linear light — never decode those again. Ray tracing shaders sample it with an explicit LOD (`textureLod`).
- Determinism. **Export** (capture, render-stream) is a pure function of (options, show time): no temporal history, `frameSeed = pcg(sampleIndex)` with `sampleIndex = std::llround(showSeconds * 48000)` (the audio sample clock). **Interactive** (the window) is a pure function of the frames rendered since the last history reset: `frameSeed = pcg(sampleIndex + 0x9E3779B9u * framesSinceReset)`, so the first frame after a reset uses the export seed. All randomness comes from integer hashes of (frameSeed, pixel, sample index, dimension); no atomics, no float reductions in an undefined order, no `rand()`.
- History resets (`HistoryReset`) on: the first frame, a resize, a change of mode or spp, a chapter change, a camera shot change, a time jump (Δt < 0 or Δt > 0.5 s; Δt = 0 is not a jump), and a modern stage that did not run in the previous frame.
- Integrator defaults: interactive 1 spp per frame and at most 4 bounces; export `--spp` (default 64) and at most 6 bounces (Decision 5); Russian roulette from bounce 3 with `p = clamp(maxComponent(throughput), 0.05, 0.95)`; contributions after the first bounce clamped to luminance 64; a non-finite sample contributes 0. Export traces in dispatches of `kSppPerDispatch = 4` samples.
- Every Studio asset is procedural C++ (meshes, materials, patterns, sky). No external models, textures or HDRIs.
- Tests run on lavapipe with validation on and assert zero validation messages; the AS, path tracer and denoiser tests also enable sync validation (Task 2). C++ RT tests render at most 320×180 and 8 spp (except the named convergence tests at 64×64) and each test executable stays under 60 s on lavapipe; Python export tests pass `--spp 2`. Setup work through `Context::immediate()` carries a `// setup` marker (`no_wait_idle_test`).
- Windows/MSVC: `std::numbers::pi_v<float>` (no `M_PI`), no VLAs or GCC attributes, `std::llround` on `double` for the sample index, `VkTransformMatrixKHR` filled row-major 3×4 from our own `Mat34`; RT and denoiser work stays split in dispatches (Windows TDR).

## Review Focus

1. **Seeking with temporal accumulation on** (the user presses → in the window): the first frame after any reset must not show the old chapter or old camera, and frames after a seek must equal a fresh renderer's frames from the same point. Pinned in Task 12 (`seek_determinism_test`), resets in Task 1.
2. **Export purity with real path tracing**: a streamed frame equals a fresh capture of the same time in the Studio chapters, with the TLAS rebuilt every frame. Pinned in Task 11 (`stream_capture` extended, 160×90, `--spp 2`).
3. **AS lifetimes and synchronisation with two frames in flight**: the TLAS is rebuilt while the previous frame may still trace it; per-slot instance buffers; compaction frees the uncompacted BLAS only after the copy completed. Pinned in Task 4 (64 frames, moving instance, ray-query probe per frame, sync validation clean).
4. **SBT alignment on RTX** (handle 32, alignment 32, base 64) versus lavapipe (32/16/32): a layout that works on lavapipe can be misaligned on RTX. Pinned in Task 5 (`computeSbtLayout` table with both property sets).
5. **The CRT screen must stay sharp and current**: its picture changes at 35 Hz and must neither smear through the temporal history nor blur through à-trous. Primary-hit emission bypasses accumulation and filtering. Pinned in Task 8 (emission passthrough) and Task 10 (screen pixels follow the retro image on the next frame).
6. **Window and export must look the same** (one tonemap, display encoding exactly once): with equal spp and bounces, the first interactive frame after a reset equals an export frame byte for byte. Pinned in Task 11; the GPU tonemap matches its CPU reference in Task 9.
7. **A skipped stage's stale image must never reach the output**: the compositor substitutes black for an image it was told is not visible. Pinned in Task 1 (render a colourful 2026 frame, then a pure 1993 frame → equals a fresh renderer's 1993 frame).
8. **NaN/Inf and fireflies poisoning history** (grazing angles, roughness 0, IOR ≈ 1, a light inside geometry): one bad sample must not stay in the history forever. Pinned in Task 6 (BSDF outputs finite), Task 7 (stress scene finite) and Task 8 (non-finite input → finite history).
9. **Windows TDR at 4K export**: one trace dispatch with 64 spp at 4K can exceed the 2 s watchdog. Pinned in Task 7 (`spp 8` records exactly 2 trace dispatches).
10. **C++/GLSL struct layout drift** (`Material`, `RectLight`, `InstanceRecord`, `Vertex`, `MeshRecord`, `FrameUniforms`): pinned by the GPU layout test in Task 6, extended in Task 7.

## Parallel execution (two implementers, separate worktrees)

| Wave | Tasks | Depends on | Main files (conflicts are append-only lines in `CMakeLists.txt`) |
|---|---|---|---|
| 1 | **1** ∥ **2**, then **3** | — | 1: `src/render/{stage_plan,renderer,compositor}.*`, `shaders/composite.comp`, `src/app/{app,capture}.cpp`; 2: `src/gfx/{descriptors,compute_pipeline,context,buffer}.*`; 3: `src/gfx/{image,sync}.*`, `src/render/retro_stage.*` |
| 2 | **4** ∥ **6**, **9** | 4←2; 6 none; 9←1 | 4: `src/rt/{geometry,accel}.*`; 6: `src/rt/gpu_types.hpp`, `shaders/rt/{types,bsdf,random}.glsl`, `src/modern/{math,mesh_builder}.*`; 9: `src/render/modern_stage.*`, `src/post/tonemap.*`, `src/modern/modern_scene.hpp` |
| 3 | **5** ∥ **8** | 5←2,4; 8←2 | 5: `src/gfx/{rt_pipeline,sbt}.*`; 8: `src/post/denoiser.*`, `shaders/post/{accumulate,atrous,combine}.comp` |
| 4 | **7** | 3,4,5,6 | `src/modern/path_tracer.*`, `shaders/rt/*` |
| 5 | **10** ∥ **11** (merge 10 first) | 10←3,7,8,9; 11←1,7 | 10: `src/modern/studio.*`, `src/modern/scenes.cpp`, `src/render/modern_stage.cpp`, `src/retro/{dos_idle,effects}.*`; 11: `src/app/{options,app,capture}.*`, `tests/*.py`, `tools/storyboard.py` |
| 6 | **12** | all | tests, README, HANDOFF |

---

### Task 1: Stage plan, stage skipping and history-reset semantics

**Suggested model:** opus.

**Files:**
- Create: `src/render/stage_plan.hpp/.cpp` (pure, no Vulkan), `tests/stage_plan_test.cpp`
- Modify: `src/render/renderer.hpp/.cpp` (plan → run only needed stages; tracker; stats), `src/render/compositor.hpp/.cpp` + `shaders/composite.comp` + `shaders/crt.comp` (push constant `visible` replaces the unused `localSeconds` slot, 32 bytes kept), `src/render/modern_stage.hpp/.cpp` (`render` takes the history state, ignored until Task 10; `shotFor(frame)` returns 0), `src/app/app.cpp` (window: `RenderMode::Interactive`), `src/app/capture.cpp` and the render-stream loop (`RenderMode::Export`), `tests/renderer_test.cpp`

**Interfaces:**
```cpp
namespace evo::render {
enum class RenderMode { Interactive, Export };
struct StagePlan {
    bool retroVisible, modernVisible; // may the compositor show it (over-inclusion allowed, never under)
    bool retro, modern;               // does the stage run this frame
};
// Visible: Cut/BeatSwap: modern = era >= .5, retro = !modern. Portal: modern = era > 0, retro = era < 1.
// Wipe: modern = wipe > 0, retro = wipe < 1. Runs: retro = retroVisible || modernSamplesRetro;
// modern = modernVisible || (mode == Interactive && transition != Cut) (keeps history warm, Decision 7).
StagePlan planStages(const show::TimelineFrame&, RenderMode, bool modernSamplesRetro);

enum class HistoryReset : std::uint8_t { None, FirstFrame, Resize, Settings, Chapter, Shot, TimeJump, Skipped };
inline constexpr double kTimeJumpSeconds = 0.5;
struct HistoryKey { int chapter; std::uint32_t shot; double showSeconds; std::uint32_t width, height;
                    RenderMode mode; std::uint32_t spp; };
class HistoryTracker {
public:
    // Called once per frame in which the modern stage runs; `skippedSinceLast` = it did not run last frame.
    HistoryReset advance(const HistoryKey&, bool skippedSinceLast);
    std::uint32_t framesSinceReset() const;   // 0 on the frame that reset
    void invalidate();                        // next advance() returns FirstFrame
};
}
// RenderSettings gains: RenderMode mode = RenderMode::Export; std::uint32_t spp = 1;
//   std::uint32_t maxBounces = 0;   // 0 = the mode's default (interactive 4, export 6)
// Renderer::Stats gains: bool modernRendered; HistoryReset historyReset; std::uint32_t framesSinceReset;
// Compositor::render gains `std::uint32_t visible` (bit 0 retro, bit 1 modern); composite.comp uses
// vec3(0) for an image whose bit is clear and never imageLoads it.
```

- [ ] **Step 1: Write the failing tests.** `stage_plan_test` (CPU): Boot (Cut, era 0) → retro only in both modes; StudioNight (Cut, era 1) → modern only, retro iff `modernSamplesRetro`; BeatSwap at era 0 → Export: retro only, Interactive: retro + modern with `modernVisible` false; Wipe at 0.5 → both visible; Portal era 0.3 → both; a Boot frame forced to 2026 (`era = wipe = 1`) → modern visible. `HistoryTracker`: one case per reset reason; Δt = 0.5 → None, 0.5001 → TimeJump, 0 → None, −1/60 → TimeJump; `framesSinceReset` counts 0,1,2,… and returns to 0 on a reset. `renderer_test` additions: (a) a Boot frame records no modern pass (`Stats.modernRendered == false`); (b) Review Focus 7: render a StudioNight frame (colourful placeholder), then Boot at t = 1 → output equals a fresh renderer's Boot t = 1 byte for byte; the reverse (Boot then StudioNight with retro skipped) likewise; (c) every existing renderer test still passes.
- [ ] **Step 2: Run** `ctest --preset linux -R "stage_plan|renderer"` — FAIL (no `stage_plan.hpp`).
- [ ] **Step 3: Implement** the plan and tracker; the renderer runs `retro_.render` / `modern_.render` only when planned, advances the tracker only when the modern stage runs, and passes the visibility bits to the compositor. Window = Interactive, capture/stream = Export.
- [ ] **Step 4: Run** the full `ctest --preset linux` (stream_capture still byte-identical: export is pure).
- [ ] **Step 5: Commit** `Skip the stages a frame does not show and track when history resets`.

---

### Task 2: Descriptors, context properties, aligned device buffers, sync validation

**Suggested model:** opus.

**Files:**
- Create: `src/gfx/descriptors.hpp/.cpp`, `shaders/test/descriptor_probe.comp`, `tests/descriptors_test.cpp`
- Modify: `src/gfx/compute_pipeline.hpp/.cpp` (built on `DescriptorLayout` + an owned `DescriptorAllocator`; the existing `bind(images, buffers)` stays as a convenience; new overload taking writes), `src/gfx/context.hpp/.cpp` (properties, trilinear sampler, `syncValidation`, `deviceInfoJson` adds the RT properties), `src/gfx/buffer.hpp/.cpp` (alignment, upload helper)

**Interfaces:**
```cpp
namespace evo::gfx {
struct Binding { std::uint32_t binding; VkDescriptorType type; std::uint32_t count = 1; };
// Supported: STORAGE_IMAGE, SAMPLED_IMAGE, SAMPLER, COMBINED_IMAGE_SAMPLER, STORAGE_BUFFER, UNIFORM_BUFFER,
// ACCELERATION_STRUCTURE_KHR (written through VkWriteDescriptorSetAccelerationStructureKHR in pNext).
struct DescriptorWrite {
    std::uint32_t binding;
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkAccelerationStructureKHR> accelerationStructures;
    static DescriptorWrite image(std::uint32_t binding, VkDescriptorImageInfo);
    static DescriptorWrite buffer(std::uint32_t binding, VkDescriptorBufferInfo);
    static DescriptorWrite accelerationStructure(std::uint32_t binding, VkAccelerationStructureKHR);
};
class DescriptorLayout {     // RAII VkDescriptorSetLayout
public:
    DescriptorLayout(Context&, std::span<const Binding>, VkShaderStageFlags stages);
    VkDescriptorSetLayout handle() const; std::span<const Binding> bindings() const;
};
class DescriptorAllocator {  // pools per FrameRing slot, reset when the ring reuses the slot, grown on demand
public:
    explicit DescriptorAllocator(Context&);
    VkDescriptorSet allocate(FrameRing&, const DescriptorLayout&);  // between begin() and submit() only
    std::size_t poolCount() const;
};
// Throws std::invalid_argument for a binding missing, given twice, of the wrong kind or count.
void writeDescriptors(const Context&, VkDescriptorSet, const DescriptorLayout&, std::span<const DescriptorWrite>);
// ComputePipeline: void bind(VkCommandBuffer, FrameRing&, std::span<const DescriptorWrite>);

struct RayTracingProperties { std::uint32_t shaderGroupHandleSize, shaderGroupHandleAlignment,
    shaderGroupBaseAlignment, maxRayRecursionDepth, minScratchOffsetAlignment; };
// Context: const RayTracingProperties& rayTracingProperties() const;
//          VkSampler trilinearSampler() const;   // linear, mipmap linear, clamp, maxLod VK_LOD_CLAMP_NONE
// ContextOptions: bool syncValidation = false;   // chains VkValidationFeaturesEXT with
//          VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT when validation is on
// BufferDesc: VkDeviceSize alignment = 0;        // vmaCreateBufferWithAlignment when non-zero
// Device-local buffer filled through a staging copy in an immediate submit (// setup).
Buffer createDeviceBuffer(Context&, const BufferDesc&, std::span<const std::byte> data);
}
```
Pool sizes per pool: 64 sets; per type 256 storage images, 128 sampled/combined/samplers, 128 storage buffers, 64 uniform buffers, 32 acceleration structures.

- [ ] **Step 1: Write the failing tests** (`descriptors_test`): a layout with every supported type including an AS binding and a `count = 3` combined-sampler array is created; `writeDescriptors` throws for a missing binding, a duplicate, an image given to a buffer binding and a 2-element write to a 3-element array; `descriptor_probe.comp` reads three 1×1 textures through the array plus a storage buffer and writes their values out → expected values; 200 `allocate()` calls in one frame then 10 more frames → `poolCount()` unchanged after frame 3; `ComputePipeline` with the old `bind` and with writes gives the same output; `rayTracingProperties()` on lavapipe = {32, 16, 32, ≥ 1, 8}; an aligned buffer (alignment 256, device address usage) has `address() % 256 == 0`; `createDeviceBuffer` round trip (copy back into a host buffer) equals the input; with `syncValidation` on, a deliberate missing barrier (two dispatches write then read one storage image) raises `validationMessageCount()` (the test then expects exactly that increase).
- [ ] **Step 2: Fail.** **Step 3: Implement**; port `ComputePipeline`. **Step 4: Full suite.** **Step 5: Commit** `Share descriptor layouts and pools, and expose the ray tracing properties`.

---

### Task 3: Images with mips, depth and sampling; the retro colour image as a mipmapped texture

**Suggested model:** opus.

**Files:**
- Modify: `src/gfx/image.hpp/.cpp`, `src/gfx/sync.hpp` (aspect-aware ranges), `src/render/retro_stage.hpp/.cpp` (colour image: `kRetroMipLevels = 9`, usage `STORAGE | SAMPLED | TRANSFER_SRC | TRANSFER_DST`; after `palette_map` it records `generateMips`), `tests/retro_stage_fixture.hpp` if the readback needs level 0 explicitly
- Create: `tests/image_test.cpp`

**Interfaces:**
```cpp
namespace evo::gfx {
std::uint32_t mipLevelsFor(std::uint32_t width, std::uint32_t height);   // floor(log2(max)) + 1
// Image:
VkImageView view() const;         // mip 0 only (storage, attachments); unchanged meaning for 1-level images
VkImageView sampledView() const;  // every mip level (== view() for one level)
VkImageAspectFlags aspect() const;   // DEPTH for D16/D32/D24S8/D32S8 formats (+STENCIL), else COLOR
std::uint32_t mipLevels() const;
// Level 0 holds the picture. Blits level i-1 -> i (linear filter) with per-level barriers, all in GENERAL,
// and leaves every level in GENERAL ordered before (dstStage, dstAccess); updates the tracked state.
// Throws std::logic_error if the format lacks BLIT_SRC, BLIT_DST or SAMPLED_IMAGE_FILTER_LINEAR.
void generateMips(VkCommandBuffer, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);
}
```
- [ ] **Step 1: Write the failing tests** (`image_test`): `mipLevelsFor(320,200) == 9`, `(1,1) == 1`, `(3840,2160) == 12`; a 320×200 RGBA8 image with a 1-pixel black/white checker in level 0 → after `generateMips` level 1 is grey 127–128 everywhere, the 1×1 level is 127–128; a left-white/right-black image → level 8 within ±2 of the mean of level 0; a `D32_SFLOAT` image with `DEPTH_STENCIL_ATTACHMENT | SAMPLED` transitions to `DEPTH_ATTACHMENT_OPTIMAL`, clears with `vkCmdClearDepthStencilImage` and reads back 1.0; validation clean (single-level storage views). Retro stage: after a PLASMA frame the colour image's 1×1 level equals the mean of level 0 within ±3 per channel; a cached retro frame does not regenerate mips (`Stats.retroRendered == false` and no blit recorded).
- [ ] **Step 2: Fail.** **Step 3: Implement.** **Step 4: Full suite** (the CRT pass still reads `view()`). **Step 5: Commit** `Mipmap the 1993 picture so the Studio can sample it`.

---

### Task 4: Geometry store and acceleration structures

**Suggested model:** opus (lifetimes and sync are the review's focus).

**Files:**
- Create: `src/rt/geometry.hpp/.cpp`, `src/rt/accel.hpp/.cpp`, `shaders/test/ray_query_probe.comp` (`GL_EXT_ray_query`), `tests/accel_test.cpp`; CMake `evo_rt`

**Interfaces:**
```cpp
namespace evo::rt {
struct Vertex { float position[3]; float normal[3]; float uv[2]; };                  // 32 bytes, scalar
struct MeshRecord { std::uint32_t firstVertex, vertexCount, firstIndex, indexCount; }; // 16 bytes
struct Mesh { std::vector<Vertex> vertices; std::vector<std::uint32_t> indices; };    // indices local to the mesh
struct Mat34 { float m[3][4]; };                                                       // row-major, = VkTransformMatrixKHR
// Every mesh in one device-local vertex buffer and one index buffer (usage: AS build input, storage,
// device address), plus a MeshRecord storage buffer. Throws std::invalid_argument for an empty mesh, an index
// out of range or an index count not a multiple of 3.
class GeometryStore {
public:
    GeometryStore(gfx::Context&, std::span<const Mesh>);
    std::span<const MeshRecord> records() const;
    const gfx::Buffer& vertices() const; const gfx::Buffer& indices() const; const gfx::Buffer& meshRecords() const;
};
// One BLAS per mesh, built in one immediate submit (// setup), PREFER_FAST_TRACE | ALLOW_COMPACTION; with
// `compact` the sizes are queried, compacted copies made in a second immediate submit, and only then are the
// originals and the scratch buffer destroyed.
class BlasSet {
public:
    BlasSet(gfx::Context&, const GeometryStore&, bool compact = true);
    VkDeviceAddress address(std::uint32_t mesh) const;
    VkDeviceSize builtBytes() const; VkDeviceSize compactedBytes() const;   // compactedBytes == builtBytes without compaction
};
struct Instance { std::uint32_t mesh; Mat34 transform; std::uint32_t customIndex; std::uint32_t hitGroup;
                  std::uint8_t mask = 0xFF; };
// Rebuilt every frame. Owns one TLAS and scratch sized for maxInstances, and one host-visible instance buffer
// per ring slot. build(): writes this slot's instance buffer (the ring's begin() already waited for it);
// barrier (RAY_TRACING_SHADER | COMPUTE_SHADER AS read, AS_BUILD AS write) -> AS_BUILD (AS read|write) so the
// previous frame's traces and builds finish first; builds; barrier AS_BUILD AS write ->
// RAY_TRACING_SHADER | COMPUTE_SHADER AS read. Throws std::length_error over maxInstances.
class Tlas {
public:
    Tlas(gfx::Context&, gfx::FrameRing&, std::uint32_t maxInstances);
    void build(VkCommandBuffer, const BlasSet&, std::span<const Instance>);
    VkAccelerationStructureKHR handle() const;
};
}
```
Scratch addresses are aligned to `minScratchOffsetAlignment` (lavapipe 8, RTX 128) through `BufferDesc::alignment`.

- [ ] **Step 1: Write the failing tests** (`accel_test`, Context with sync validation): meshes = a unit quad and a unit cube; 4 instances at known transforms with `customIndex` 10–13; `ray_query_probe.comp` casts a 16×16 grid of rays along −z and writes (t, customIndex, primitiveId, frontFace) → each equals the CPU analytic intersection (t ± 1e-4, ids exact, misses reported as −1); the same with `compact = false` gives identical probe results and `compactedBytes() <= builtBytes()`; Review Focus 3: 64 frames with two in flight, instance 0 moved to x = 0.1·frame, TLAS built and probed each frame, each frame's result (read after its own `wait`) equals its own transform; 5 instances into `Tlas(…, 4)` throws; `GeometryStore` throws for index 9 in a 4-vertex mesh; zero validation messages.
- [ ] **Step 2: Fail.** **Step 3: Implement.** **Step 4: Run** `ctest -R accel` (< 20 s). **Step 5: Commit** `Build compacted BLASes once and a TLAS every frame`.

---

### Task 5: Ray tracing pipeline and shader binding table

**Suggested model:** opus.

**Files:**
- Create: `src/gfx/sbt.hpp/.cpp` (pure), `src/gfx/rt_pipeline.hpp/.cpp`, `shaders/test/sbt_probe.rgen`, `sbt_probe.rmiss` (compiled with `-DMISS_INDEX=0/1`), `sbt_probe.rchit` (compiled with `-DHIT_GROUP=0..3`), `tests/rt_pipeline_test.cpp`

**Interfaces:**
```cpp
namespace evo::gfx {
struct SbtRegion { VkDeviceSize offset, stride, size; };
struct SbtLayout { SbtRegion raygen, miss, hit; VkDeviceSize totalBytes; };
// a = alignUp(handleSize, handleAlignment); raygen: offset 0, stride = size = alignUp(a, baseAlignment);
// miss: offset raygen.size, stride a, size alignUp(missCount*a, baseAlignment); hit: after miss, same rule.
SbtLayout computeSbtLayout(const RayTracingProperties&, std::uint32_t missCount, std::uint32_t hitGroupCount);
struct RtShaders {   // one triangle hit group per closest-hit, in SBT order (= instance hitGroup)
    std::filesystem::path raygen; std::vector<std::filesystem::path> misses, closestHits; };
class RayTracingPipeline {
public:
    // Layout stages RAYGEN | MISS | CLOSEST_HIT; recursion depth 1; SBT in a device-local buffer aligned to
    // shaderGroupBaseAlignment (createDeviceBuffer), usage SHADER_BINDING_TABLE | SHADER_DEVICE_ADDRESS.
    RayTracingPipeline(Context&, const RtShaders&, std::span<const Binding>, std::uint32_t pushConstantBytes);
    void bind(VkCommandBuffer, FrameRing&, std::span<const DescriptorWrite>);
    void push(VkCommandBuffer, const void*, std::uint32_t bytes);
    void traceRays(VkCommandBuffer, std::uint32_t width, std::uint32_t height);
    const SbtLayout& sbtLayout() const;
};
}
```
- [ ] **Step 1: Write the failing tests** (`rt_pipeline_test`): `computeSbtLayout({32,16,32,…}, 2, 4)` → raygen {0,32,32}, miss {32,32,64}, hit {96,32,128}, total 224; RTX `({32,32,64,…}, 2, 4)` → raygen {0,64,64}, miss {64,32,64}, hit {128,32,128}, total 256; `({32,32,128,…}, 1, 1)` → raygen {0,128,128}, miss {128,32,128}, hit {256,32,128}; every offset a multiple of the base alignment. Probe: a 64×64 launch over a TLAS (Task 4) of four quads with `hitGroup` 0–3 in the four quadrants and a gap column; the raygen writes the payload: hit group k → `k + 1`, miss 0 → 100; one row traced with `missIndex 1` → 101. Zero validation messages. < 10 s.
- [ ] **Step 2: Fail.** **Step 3: Implement.** **Step 4: Run.** **Step 5: Commit** `Trace rays through a computed shader binding table`.

---

### Task 6: Materials, shared GPU structs, BSDFs and procedural meshes

**Suggested model:** opus (sampling maths). Independent of Tasks 2–5: CPU code and compute test shaders only.

**Files:**
- Create: `src/rt/gpu_types.hpp`, `shaders/rt/types.glsl` (mirrors of these and of Task 4's `Vertex`/`MeshRecord`), `shaders/rt/random.glsl` (PCG hash and samplers), `shaders/rt/bsdf.glsl`, `src/modern/math.hpp` (float `Vec3`, `Mat34` helpers: identity, translate, rotate axis-angle, scale, compose, transformPoint/Normal), `src/modern/mesh_builder.hpp/.cpp`, `shaders/test/struct_layout.comp`, `shaders/test/bsdf_probe.comp`, `tests/material_test.cpp`, `tests/mesh_builder_test.cpp`

**Interfaces:**
```cpp
namespace evo::rt {
enum class MaterialClass : std::uint32_t { Pbr = 0, Glass = 1, Emissive = 2, Checker = 3 };  // = hit group
enum class Pattern : std::uint32_t { None = 0, WoodPlanks = 1, BrushedMetal = 2 };
inline constexpr std::uint32_t kNoTexture = 0, kRetroScreenTexture = 1, kNoLight = ~0u;
struct Material {   // 80 bytes, scalar
    float baseColor[3]; float roughness; float metallic; float ior; float transmission;
    float emission[3]; float emissionScale; float coat; float coatRoughness;
    std::uint32_t pattern; std::uint32_t texture; float patternScale; float baseColor2[3];
    std::uint32_t materialClass;
};
enum class LightKind : std::uint32_t { Emitter = 0, RetroScreen = 1, Portal = 2 };   // Portal: radiance = sky(dir)
struct RectLight {  // 64 bytes: point = corner + u*edgeU + v*edgeV, emits along normalize(cross(edgeU, edgeV))
    float corner[3]; float edgeU[3]; float edgeV[3]; float emission[3];
    std::uint32_t kind; float selectWeight; float area; std::uint32_t pad; };
struct InstanceRecord { std::uint32_t mesh, material, light, pad; };   // index = gl_InstanceCustomIndexEXT
}
```
```glsl
// bsdf.glsl
struct Surface { vec3 baseColor; float roughness; float metallic; float ior; float transmission;
                 float coat; float coatRoughness; vec3 emission; };
// f·cos and pdf for a given pair (0 for delta lobes); n is the shading normal, wo and wi point away.
vec3 bsdfEval(Surface s, vec3 n, vec3 wo, vec3 wi, out float pdf);
// Samples wi; weight = f·cos/pdf (the throughput factor); delta = smooth lobe (no NEE, no MIS).
bool bsdfSample(Surface s, vec3 n, vec3 wo, vec3 u /*3 uniforms*/, out vec3 wi, out vec3 weight,
                out float pdf, out bool delta);
```
Model: alpha = max(roughness², 1e-3); GGX with visible-normal sampling (Heitz 2018) and Smith height-correlated G; Schlick Fresnel; opaque = coat (F0 0.04, `coatRoughness`) over a base of GGX specular `F0 = mix(0.04, baseColor, metallic)` plus `(1 − metallic)` Lambert; lobe choice by approximate albedo weights at `wo`; `transmission = 1` → dielectric with exact Fresnel, smooth (delta) when roughness < 0.02, else rough GGX transmission (Walter 2007). Mesh builders (counter-clockwise outward, flat normals per face for boxes, smooth for lathe/sphere, uv in [0,1]):
```cpp
namespace evo::modern {
rt::Mesh box(Vec3 halfExtents);
rt::Mesh bevelBox(Vec3 halfExtents, float bevel, int segments);
rt::Mesh cylinder(float radius, float height, int segments, bool caps);
rt::Mesh lathe(std::span<const Vec2> profile /* (radius, y), bottom to top */, int segments);
rt::Mesh uvSphere(float radius, int rings, int segments);
rt::Mesh quad(float width, float height);      // facing +z, centred
}
```
- [ ] **Step 1: Write the failing tests.** `material_test` (GPU compute, no RT): layout — the CPU fills each struct with field sentinels (field i = i + 0.5 or i + 1), `struct_layout.comp` copies every field in declaration order into a float/uint output → equal; BSDF (`bsdf_probe.comp`, 8 materials × 8 outgoing angles 0°–89.9° × 65 536 samples): Lambert white mean weight 1.0 ± 0.01; metal F0 = 1 roughness 0 (delta) weight exactly 1; metal F0 = 1 roughness 0.5 mean in [0.85, 1.01]; smooth glass IOR 1.5 reflect + transmit = 1 ± 0.01; no material above 1.01; for non-delta samples `|pdf(sample) − pdf(eval)| / pdf < 1e-3`; reflection reciprocity `eval(wo,wi) == eval(wi,wo)` ± 1e-4 relative; every output finite (Review Focus 8). `mesh_builder_test`: box, bevelBox, capped cylinder, closed lathe and sphere are watertight (after welding positions within 1e-6 every edge has exactly two triangles); winding normal · vertex normal > 0 for every triangle; vertex normals unit ± 1e-5; uv in [0,1]; sphere area of `uvSphere(1, 32, 64)` within 1 % of 4π.
- [ ] **Step 2: Fail.** **Step 3: Implement.** **Step 4: Run** (< 20 s). **Step 5: Commit** `Model GGX metal, plastic and glass, and build meshes in code`.

---

### Task 7: The path tracer

**Suggested model:** opus.

**Files:**
- Create: `src/modern/scene.hpp` (`Camera`, `SceneStatic`, `SceneInstance`, `SceneFrame`), `src/modern/path_tracer.hpp/.cpp`, `shaders/rt/common.glsl` (bindings, payloads, `FrameUniforms`), `shaders/rt/sky.glsl` (procedural night sky: horizon-to-zenith blue gradient, hash stars, moon disc with halo, faint city glow at the horizon), `shaders/rt/lights.glsl` (rect sampling, MIS), `shaders/rt/pathtrace.rgen`, `shaders/rt/sky.rmiss`, `shaders/rt/shadow.rmiss`, `shaders/rt/pbr.rchit`, `shaders/rt/glass.rchit`, `shaders/rt/emissive.rchit`, `shaders/rt/checker.rchit`, `tests/rt_test_scenes.hpp` (Cornell box, emitter quad, furnace sphere, stress scene), `tests/path_tracer_test.cpp`
- Modify: `src/rt/gpu_types.hpp` + `shaders/rt/types.glsl` (`FrameUniforms`), `tests/material_test.cpp` (layout cases for `Vertex`, `MeshRecord`, `FrameUniforms`)

**Interfaces:**
```cpp
namespace evo::modern {
struct Camera { Vec3 eye, target, up; float fovYDegrees; float aperture = 0; float focusDistance = 1;
                std::uint32_t shot = 0; };
struct SceneStatic { std::vector<rt::Mesh> meshes; std::vector<rt::Material> materials; };
struct SceneInstance { std::uint32_t mesh, material; Mat34 transform; std::uint32_t light = rt::kNoLight; };
struct SceneFrame { Camera camera; std::vector<SceneInstance> instances; std::vector<rt::RectLight> lights;
                    float exposureEV = 0; };
struct TraceSettings { std::uint32_t spp = 1, maxBounces = 4; std::uint32_t frameSeed = 0; bool nee = true;
                       bool russianRoulette = true; float clampIndirect = 64.f; };
inline constexpr std::uint32_t kSppPerDispatch = 4;
std::uint32_t frameSeedFor(double showSeconds, std::uint32_t framesSinceReset);  // Global Constraints formula
// Guides and outputs at the trace size (formats per Global Constraints):
//   lighting  RGBA32F  mean over the frame's samples of path radiance without primary-hit emission
//   emission  RGBA16F  mean primary-hit emission (and the sky on a primary miss)
//   albedo    RGBA16F  primary-hit demodulation albedo (diffuse: baseColor; metal: F0; glass and miss: 1)
//   normal    RGBA16F  xyz world shading normal of the primary hit, w roughness
//   depth     R32F     linear view depth of the primary hit (centre ray, unjittered), -1 on a miss
//   motion    RGBA16F  xy = previous pixel centre - current pixel centre from the previous camera, z = 1 valid
struct GBuffer { gfx::Image lighting, emission, albedo, normal, depth, motion; };
class PathTracer {
public:
    // Builds the geometry store, BLASes, the RT pipeline (4 hit groups = MaterialClass, misses: sky, shadow)
    // and the material buffer once. A scene that does not sample the retro screen binds a 1x1 black texture.
    PathTracer(gfx::Context&, gfx::FrameRing&, const SceneStatic&, std::uint32_t width, std::uint32_t height);
    void resize(std::uint32_t width, std::uint32_t height);
    // TLAS build, per-slot FrameUniforms and instance records, then ceil(spp / kSppPerDispatch) trace
    // dispatches (lighting/emission accumulate across them, divided by spp in the last). `previousCamera`
    // feeds the motion guide (nullopt -> motion invalid). retroScreen is sampled through sampledView().
    void trace(VkCommandBuffer, const SceneFrame&, const std::optional<Camera>& previousCamera,
               const TraceSettings&, gfx::Image& retroScreen);
    GBuffer& gbuffer();
    struct Stats { std::uint32_t traceDispatches = 0; };
    Stats lastStats() const;
};
}
```
Integrator (raygen loop; closest-hit shaders only fill the payload with the evaluated `Surface`, normals, uv, `InstanceRecord.light` and hit kind; hit-group order = `MaterialClass`): per sample — box-filter pixel jitter and thin-lens origin from `random.glsl`; on a hit with emission at bounce 0 write it to `emission`, at bounce ≥ 1 add `throughput · Le · w_MIS`; for non-delta surfaces pick one light by `selectWeight`, sample a uniform point, shadow ray (`TerminateOnFirstHit | SkipClosestHit | Opaque`, miss 1, tmax = distance · (1 − 1e-4)), add `throughput · f·cos · Le / (pdf_select · pdf_area→solid angle) · w_MIS`; `Portal` lights give `Le = sky(direction)`; a BSDF ray that leaves through a portal rect gets the sky with the MIS weight against that portal; power heuristic β = 2; `RetroScreen` radiance = `srgbToLinear(textureLod(retro, uv, lod)) · emissionScale` with `lod` from a ray cone (spread `atan(2·tan(fovY/2)/height)`; NEE uses `lod = 3`). Ray origins offset by `geometricNormal · 1e-4 · (1 + maxAbs(position))`.

- [ ] **Step 1: Write the failing tests** (`path_tracer_test`, 64×64 unless noted, sync validation on): (a) camera facing an emitter quad of emission 2.0 → `emission == 2.0` ± 1e-6 inside, `lighting == 0` there; (b) MIS is unbiased: Cornell box (0.8 white walls, red/green sides, ceiling rect light), 64 spp, `nee = true` vs `nee = false` → image means within 3 %, and the per-pixel spread between two seeds is smaller with NEE; (c) furnace: camera inside a closed sphere, every face albedo 0.5 diffuse with emission 1.0, 32 bounces, roulette off → `emission ≈ 1.0` and `lighting` mean 1.0 ± 2 % (the geometric series); (d) the same `frameSeed` gives identical bytes in all six images; another seed changes `lighting`; (e) the stress scene (grazing camera, roughness-0 metals, IOR 1.0001 glass, a light inside a box) → every value of every image finite (Review Focus 8); (f) Review Focus 9: `spp 8` → `traceDispatches == 2`, `spp 1` → 1, `spp 5` → 2; (g) guides: a plane at distance 3 facing the camera → centre depth 3 ± 1e-3, normal = plane normal; static camera → motion (0,0,1); camera translated sideways by a known amount → motion equals the analytic pixel shift ± 0.01; (h) textured light: a retro texture all red vs all green → screen emission red vs green (255 → 1.0·scale, 128 → 0.2158·scale ± 1e-3) and the floor in front turns red vs green (NEE through the screen); layout test cases for `Vertex`, `MeshRecord`, `FrameUniforms` pass.
- [ ] **Step 2: Fail.** **Step 3: Implement.** **Step 4: Run** `ctest -R "path_tracer|material"` (< 60 s). **Step 5: Commit** `Path trace with next-event estimation and MIS against area lights and the sky`.

---

### Task 8: Temporal accumulation and à-trous denoiser

**Suggested model:** opus. Built and tested on synthetic G-buffers uploaded from the CPU, so it runs beside Tasks 5 and 7.

**Files:**
- Create: `src/post/denoiser.hpp/.cpp`, `shaders/post/accumulate.comp` (demodulate, reproject, accumulate, moments), `shaders/post/atrous.comp`, `shaders/post/combine.comp` (remodulate + emission → HDR), `tests/denoiser_test.cpp`

**Interfaces:**
```cpp
namespace evo::post {
struct GBufferView { gfx::Image& lighting; gfx::Image& emission; gfx::Image& albedo; gfx::Image& normal;
                     gfx::Image& depth; gfx::Image& motion; };      // = modern::GBuffer, by reference
struct DenoiseSettings {
    bool temporal = true; std::uint32_t atrousIterations = 3;          // steps 1, 2, 4, ...
    float alphaColor = 0.2f, alphaMoments = 0.2f; std::uint32_t maxHistory = 64;
    float sigmaDepth = 1.f, sigmaNormal = 128.f, sigmaLuminance = 4.f; };
class Denoiser {
public:
    Denoiser(gfx::Context&, gfx::FrameRing&, std::uint32_t width, std::uint32_t height);
    void resize(std::uint32_t width, std::uint32_t height);   // also resets
    void resetHistory();                                      // history images cleared to 0, length 0
    // hdrOut: RGBA16F. History = the accumulated, unfiltered illumination (à-trous output is display only).
    void denoise(VkCommandBuffer, const GBufferView&, const DenoiseSettings&, gfx::Image& hdrOut);
    gfx::Image& historyLength();                              // R channel = frames accumulated (tests)
};
}
```
Rules: illumination = lighting / max(albedo, 1e-3), non-finite → 0 first; reprojection taps the previous 2×2 bilinear footprint and keeps a tap when it is inside, `|z_prev − z| ≤ 0.05·z` and `dot(n_prev, n) ≥ 0.9`; `len = valid ? min(len_prev + 1, maxHistory) : 1`, `α = max(alphaColor, 1/len)` and the blend uses `select` so `len == 1` copies the current value exactly; variance = max(m2 − m1², 0), multiplied by `4 / len` while `len < 4`. **`temporal = true` with `len == 1` produces bytes identical to `temporal = false`** (the window's first frame equals export, Review Focus 6). À-trous: 5×5 B3 kernel (1/16, 1/4, 3/8), weights `exp(−|z_p − z_q| / (sigmaDepth · step · 0.01 · max(z_p, 1e-3)))`, `max(0, n_p·n_q)^sigmaNormal`, `exp(−|l_p − l_q| / (sigmaLuminance · sqrt(gauss3x3(variance)) + 1e-4))`, variance propagated with squared weights. Output = filtered illumination × albedo + emission (emission never enters history or filtering, Review Focus 5).

- [ ] **Step 1: Write the failing tests** (`denoiser_test`, 64×64, sync validation on): (a) lighting 0.5 + fresh uniform noise ±0.5 per frame, albedo 1, motion 0, 0 iterations → RMSE against 0.5 after 32 frames < 0.08 and below the RMSE after 4 frames; history length = frame count; (b) `resetHistory()` → next frame length 1 everywhere and output equal to a fresh denoiser's first frame byte for byte, and equal to `temporal = false`; (c) depth of the right half changed by 50 % → right half length 1, left half 2; (d) a static pattern shifted 3 px per frame with motion −3 → after 8 frames within 2 % of the pattern away from a 3 px border; (e) edge: left 0.1 / right 0.9 with a depth step → after 3 iterations the two edge columns stay within 5 % of 0.1 and 0.9; flat-region noise stddev drops ≥ 3×; (f) emission checker that changes between frames appears unchanged in the next output (output − filtered lighting = emission exactly); (g) one NaN and one +Inf in `lighting` → output and history finite everywhere; (h) the same input sequence twice → identical bytes. < 15 s.
- [ ] **Step 2: Fail.** **Step 3: Implement.** **Step 4: Run.** **Step 5: Commit** `Accumulate and filter the path tracer's noise, keeping the screen sharp`.

---

### Task 9: HDR modern stage, scene registry and the ACES tonemap

**Suggested model:** opus.

**Files:**
- Create: `src/modern/modern_scene.hpp`, `src/post/tonemap.hpp/.cpp`, `src/post/tonemap_reference.hpp/.cpp` (CPU mirror), `shaders/post/tonemap.comp`, `tests/tonemap_test.cpp`
- Modify: `src/render/modern_stage.hpp/.cpp` (keeps the placeholder fill for `SceneId::Placeholder`; owns the HDR `RGBA16F` image and the tonemap for RT scenes; RT wiring lands in Task 10)

**Interfaces:**
```cpp
namespace evo::modern {
// A 2026 scene the path tracer renders. frame() and its camera are pure functions of the chapter's fixed
// facts and localSeconds (like the retro scene providers, but continuous time is allowed).
class ModernScene {
public:
    virtual ~ModernScene() = default;
    virtual const SceneStatic& staticScene() const = 0;
    virtual SceneFrame frame(const show::TimelineFrame&) const = 0;
    virtual bool samplesRetro() const = 0;
};
enum class SceneId { Placeholder, Studio };
SceneId sceneFor(show::Effect);                           // Task 9: always Placeholder; Task 10 maps the Studio
}
namespace evo::post {
// hdr * 2^exposureEV -> ACES fit (Hill: sRGB->AP1 input matrix, RRT+ODT rational fit, output matrix) ->
// clamp [0,1] -> sRGB OETF -> round(v * 255 + d) with d a triangular dither in (-0.5, 0.5) from pcg(x, y),
// clamped to [0, 255] (black stays 0, white stays 255; static per pixel, no temporal shimmer).
std::array<std::uint8_t, 3> tonemapReference(const float rgb[3], float exposureEV, std::uint32_t x, std::uint32_t y);
class Tonemap {
public:
    Tonemap(gfx::Context&, gfx::FrameRing&);
    void record(VkCommandBuffer, gfx::Image& hdr /*RGBA16F*/, gfx::Image& ldr /*RGBA8*/, float exposureEV);
};
}
```
- [ ] **Step 1: Write the failing tests** (`tonemap_test`): a 256×4 HDR ramp from 0 to 64 (log spaced) and the 8 primaries/secondaries at 4 intensities uploaded as RGBA16F → GPU bytes equal `tonemapReference` within ±1; 0 → 0 and white → 255 at every pixel; monotonic along the ramp per channel after removing the dither; exposure +1 EV equals the input doubled; `sceneFor` returns `Placeholder` for every effect; the placeholder picture is unchanged (existing renderer tests).
- [ ] **Step 2: Fail.** **Step 3: Implement.** **Step 4: Run.** **Step 5: Commit** `Tone map HDR light into the 2026 picture`.

---

### Task 10: The Studio

**Suggested model:** opus (art direction plus wiring).

**Files:**
- Create: `src/modern/studio.hpp/.cpp`, `src/modern/scenes.cpp` (`sceneFor`, scene factory), `src/retro/dos_idle.hpp/.cpp` (Decision 3), `tests/studio_test.cpp`
- Modify: `src/render/modern_stage.hpp/.cpp` (RT scenes: `PathTracer` → `Denoiser` → `Tonemap`; interactive: temporal on, `DenoiseSettings::atrousIterations = 3`, reset on `HistoryReset != None`; export: `temporal = false`, iterations 3 when spp < 16 else 0 (Decision 6); keeps the previous camera for motion; `shotFor` returns the scene camera's shot), `src/render/renderer.cpp` (`modernSamplesRetro` from the scene; passes the retro colour image), `src/retro/effects.cpp` (StudioNight and Return → `dos_idle`), `shaders/rt/emissive.rchit` (screen: barrel UV warp of 2 % like `crt.comp`, scanline darkening 25 % on odd source lines faded out above LOD 1, black outside the warped picture)

**The room** (metres, +y up, desk front edge at z = 0; positions are a starting point, the look decides): room 4.0 × 2.7 × 4.0, matte warm-grey walls (0.55, 0.53, 0.50, roughness 0.9), dark ceiling; floor = `Checker` class, 0.3 m black / off-white linoleum tiles, roughness 0.35 (a nod to RAYTRACE); a 1.2 × 1.0 window opening in the back-left wall (`Portal` light onto the sky); desk 1.4 × 0.75 × 0.7, `WoodPlanks` pattern, coat 0.6 at roughness 0.15, brushed-metal legs; CRT monitor: beige plastic (0.78, 0.74, 0.64, roughness 0.45) `bevelBox` 0.40 × 0.36 × 0.42 with a bezel and a flat 0.28 × 0.21 screen recessed 1 cm (`Emissive`, texture `kRetroScreenTexture`, emissionScale 5, coat 1 at roughness 0.03; the screen rect is also the `RetroScreen` light), a green power LED; desktop PC case under the monitor with dark drive slots; keyboard base plus 101 key caps instanced from one key mesh; mouse; three floppy disks; a glazed mug (`lathe`, coat); a 6 cm chrome sphere (metallic 1, roughness 0.02) and a 7 cm glass sphere (IOR 1.5) on small stands; a desk lamp (`lathe` + cylinders, brushed metal) whose 7 cm square emitter (1.0, 0.75, 0.5) × 30 is the `Emitter` light. Light weights: screen 0.45, lamp 0.35, window 0.20. Exposure per chapter authored in `studio.cpp`.

**Cameras** (pure functions of chapter, `localSeconds`, `chapterDuration`, `accentSeconds`; Catmull-Rom through authored keys with smoothstep easing; `shot` increments at authored cut times): FINALE and PORTAL (9, 10): a slow pull from the screen to a wide view of the desk (step 6 replaces it with the portal path); STUDIO NIGHT (11): shot 0 a wide dolly across the dark room towards the desk, shot 1 from the accent a close orbit around the monitor; RETURN (19): a slow push back towards the screen; REBOOT (20): a static close-up of the screen with a slight drift.

**Interfaces:**
```cpp
namespace evo::modern {
class Studio final : public ModernScene { /* staticScene(), frame(), samplesRetro() == true */ };
Camera studioCamera(const show::TimelineFrame&);   // also used by tests
std::unique_ptr<ModernScene> makeScene(SceneId);
// sceneFor: Finale, Portal, StudioNight, Return, Reboot -> Studio (Decision 1); everything else Placeholder.
}
```
- [ ] **Step 1: Write the failing tests** (`studio_test`, 320×180, export, spp 4, captures to `build-out/step4/`): (a) STUDIO NIGHT at 30 % and 80 %: mean luminance > 0.05, < 2 % of pixels at 255 in any channel, the screen rectangle (projected from the screen corners) brighter than the room's mean, zero validation messages, each frame < 30 s; (b) the CRT lights the room: with a test retro image all white vs all black (a `ModernStage` fixture given its own retro texture) the screen rectangle and a desk patch in front of the monitor are each ≥ 10 % brighter with white, a patch on the wall behind the camera changes < 2 %; (c) Review Focus 5 in the window path: interactive, frame N with a red retro image then frame N+1 with a green one → in the screen rectangle of frame N+1 the green channel is at least 5× the red one (the picture itself bypasses history; only the coat's reflection of the room may lag); (d) `studioCamera` is finite, moves < 0.05 m per 1/60 s within a shot, changes shot exactly at the authored times; (e) 101 key instances and all transforms finite; instance count ≤ the TLAS maximum; (f) `sceneFor` mapping; STUDIO NIGHT plans the retro stage (`Stats.retroRendered` on its first frame) because the Studio samples it; (g) the DOS idle screen: `C:\DEMO>` with a 2 Hz cursor, pure in retroFrame (`retro_registry_test` already covers purity once registered).
- [ ] **Step 2: Fail.** **Step 3: Implement**, then look at the captures and tune exposure and light levels until the room reads as a dark room lit by the CRT and the lamp. **Step 4: Run** `ctest -R "studio|renderer|retro_registry"`. **Step 5: Commit** `Path trace the Studio around a CRT that shows 1993 and lights the room`.

---

### Task 11: Export mode: `--spp`, seeds and export determinism

**Suggested model:** opus.

**Files:**
- Modify: `src/app/options.hpp/.cpp` (`--spp N`, 1–4096; unset = 1 in the window, 64 in exports), `src/app/app.cpp`, `src/app/capture.cpp` (pass mode and spp), `README.md` (CLI, interactive vs export), `tests/options_test.cpp`, `tests/render_stream_test.py`, `tests/capture_test.py`, `tests/stream_capture_test.py` (all runs `--spp 2`; the Studio chapters now path trace; TIMEOUT 600), `tools/storyboard.py` (`--spp`, default 8), `tests/storyboard_test.py`
- Create: `tests/export_mode_test.cpp`

Per-slot readback and RGB packing on the GPU are deferred to step 8 (Decision 8).

- [ ] **Step 1: Write the failing tests.** `options_test`: `--spp 16` parses; `--spp 0`, `--spp 5000`, `--spp x` throw; the default is reported as unset. `export_mode_test` (Renderer at 160×90, STUDIO NIGHT at 40 %): Review Focus 6 — an Interactive renderer's first frame equals an Export renderer's frame at spp 1, both with `maxBounces = 4`, byte for byte; Export spp 2 at T in two fresh renderers is identical, and identical after first rendering T − 1/60 (purity); T and T + 1/60 differ (the seed moves). `stream_capture_test.py` (Review Focus 2) passes over every chapter including 9, 10, 11, 19, 20 with `--spp 2`; `capture_test.py` rejects `--spp 0` with exit 1.
- [ ] **Step 2: Fail.** **Step 3: Implement.** **Step 4: Run** `ctest -R "options|export_mode|render_stream|capture|stream_capture|storyboard"`. **Step 5: Commit** `Export path-traced frames at any spp, seeded by the show time`.

---

### Task 12: Seek determinism suite, storyboard, README, HANDOFF

**Suggested model:** opus.

**Files:**
- Create: `tests/seek_determinism_test.cpp`
- Modify: `README.md` (path tracer, Studio, modes, denoiser, what lavapipe can and cannot show), `docs/plans/HANDOFF.md` (step 4 table, decisions taken, deferred minors, step 5 recommendations)

- [ ] **Step 1: Write the failing test** (`seek_determinism_test`, Interactive, 160×90, spp 1; Review Focus 1): renderer A plays STUDIO NIGHT from T0 for 12 frames at 1/60 s; renderer B first plays 6 frames of RETURN, then seeks to T0 and plays the same 12 frames → B's frames equal A's byte for byte from the seek on (`historyReset == TimeJump` or `Chapter` on the seek frame); crossing the shot cut inside STUDIO NIGHT resets (`Shot`) and the frame after the cut equals a fresh renderer started there; a frame duplicated (Δt = 0) does not reset; a BEATSWAP chapter (DOTS) in Interactive keeps `modernRendered` true on its 1993 beats while Export skips them; time running backwards (the show loop) resets.
- [ ] **Step 2: Fail, then make it pass** (fixes go to the task that owns the code).
- [ ] **Step 3: Storyboard:** `python3 tools/storyboard.py --app build/linux/bin/Evolution --out build/linux/build-out/storyboard --spp 8`; inspect the Act I and Act II grids; fix any broken Studio frame before closing.
- [ ] **Step 4: Full suite** `ctest --preset linux --output-on-failure`, plus `windowed` under `EVO_RUNTIME_TESTS=ON` once (Boot: no path tracing in the first second).
- [ ] **Step 5: Commit** `Pin seeking under temporal accumulation and document the path tracer`.

---

## Decisions the controller must confirm (defaults used above)

1. **Studio chapters in step 4:** FINALE, PORTAL, STUDIO NIGHT, RETURN, REBOOT show the Studio; FLYBY (spec: "first reveal of the Studio" by portal) stays on the placeholder until step 5's ship and step 6's portal path.
2. **Portal stays a cross-fade** in the compositor; flying into the screen is step 6.
3. **What the CRT shows in 2026-only chapters** (STUDIO NIGHT, RETURN): a new DOS idle screen (`C:\DEMO>` with a blinking cursor) instead of the grey test pattern.
4. **Export `--spp` default 64** (spec example); tests use 2, the storyboard 8.
5. **Bounces:** interactive 4 (spec), export 6 (the spec is silent; glass needs more).
6. **À-trous at export when spp < 16** (3 iterations), off from 16 spp. The spec only rules out temporal filtering at export.
7. **Warm history in the window:** in BeatSwap/Portal/Wipe chapters the modern stage keeps running while it is invisible (costs GPU time, avoids a noisy first beat after each swap); exports skip it strictly.
8. **Per-slot readback and RGB packing on the GPU** move to step 8 (they only speed up 4K export; at 64 spp the GPU dominates).
9. **Sync validation** only in the AS, path tracer and denoiser tests, not in every test (slow on lavapipe).
10. **Flat CRT screen** (exact NEE for the screen light); curvature only as a UV warp plus the bezel.

## Self-review notes

- Spec coverage: §3.4 RT pipeline with per-class closest-hits, 1 spp + NEE + accumulation + à-trous, export spp → Tasks 5–8, 11; §3.3 "no pass the chapter does not need" → Task 1; §3.6 CRT as emissive texture → Tasks 3, 7 (h), 10; §3.7 determinism and seeking → Global Constraints, Tasks 1, 11, 12; §3.8–3.9 `--spp` → Task 11; §2.1 Studio → Task 10; §6 storyboards and validation → Tasks 10, 12; §7 done criterion → Tasks 10–12; HANDOFF recommendations: stage skipping + history reset (1), DescriptorAllocator with AS (2), Image depth/mips/SAMPLED (3), HDR + tonemap (9), interactive vs export spp/seed (7, 11), history reset on cuts/chapters (1, 12), per-slot readback (Decision 8).
- Not in step 4 by design: particles and volumes (step 5, they will depth-test against the `depth` guide), bloom and grading (step 6), object-motion vectors (the Studio is static apart from the camera; step 5 adds previous instance transforms), ML denoising (spec: none in v1).
- Names used across tasks: `RenderMode`, `StagePlan`, `planStages`, `HistoryReset`, `HistoryKey`, `HistoryTracker`, `Binding`, `DescriptorWrite`, `DescriptorLayout`, `DescriptorAllocator`, `writeDescriptors`, `RayTracingProperties`, `createDeviceBuffer`, `mipLevelsFor`, `sampledView`, `generateMips`, `Vertex`, `MeshRecord`, `Mesh`, `Mat34`, `GeometryStore`, `BlasSet`, `Instance`, `Tlas`, `SbtLayout`, `computeSbtLayout`, `RtShaders`, `RayTracingPipeline`, `Material`, `MaterialClass`, `Pattern`, `RectLight`, `LightKind`, `InstanceRecord`, `Camera`, `SceneStatic`, `SceneInstance`, `SceneFrame`, `TraceSettings`, `kSppPerDispatch`, `frameSeedFor`, `GBuffer`, `PathTracer`, `GBufferView`, `DenoiseSettings`, `Denoiser`, `ModernScene`, `SceneId`, `sceneFor`, `Tonemap`, `tonemapReference`, `Studio`, `studioCamera`, `makeScene`.
- Review Focus 1 → Task 12; 2 → Task 11; 3 → Task 4; 4 → Task 5; 5 → Tasks 8, 10; 6 → Tasks 8, 9, 11; 7 → Task 1; 8 → Tasks 6, 7, 8; 9 → Task 7; 10 → Tasks 6, 7.
- Risky for lavapipe time: Task 7 (b)/(c) at 64×64 × 64 spp and the extended `stream_capture`; both have explicit budgets, and resolution may drop before spp does.

## Controller rulings (2026-10-24)

All ten open decisions above are accepted as proposed (option one each): the Studio appears in FINALE, PORTAL, STUDIO NIGHT, RETURN and REBOOT in step 4 (FLYBY's reveal waits for steps 5–6); Portal stays a cross-fade until step 6; the CRT shows the DOS idle screen in 2026-only chapters; export `--spp` defaults to 64 (tests 2, storyboard 8); bounces 4 interactive / 6 export; spatial à-trous at export only below 16 spp; the window keeps tracing hidden 2026 in BeatSwap/Portal/Wipe chapters, exports skip it; per-slot readback and GPU RGB packing move to step 8; sync validation only in the AS, path tracer and denoiser tests; the CRT screen is flat with a UV warp.
