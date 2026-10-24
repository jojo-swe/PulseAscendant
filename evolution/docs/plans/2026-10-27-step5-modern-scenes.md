# EVOLUTION Step 5: Modern Scenes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every 2026 side of the show becomes a physically based, path-traced scene, which retires the placeholder gradient in all 23 chapters that still show it. The scenes are a ship with volumetric engine trails, millions of traced particles, an emissive nebula, an endless PBR tunnel, dispersive glass with caustics, a lens over an endless chessboard, storms of chrome and glass spheres, a night city with thousands of sampled lights, a 16.7-million-point galaxy and a lensing singularity. Each one is a pure function of show time, and each one aims to run in real time at a ~1440p internal resolution on an RTX 4090 and to export at 4K.

**Architecture:** Step 5 comes in two halves, with a cut line and an owner checkpoint between them.

- **Step 5a, the foundation:** one shared path tracer and one shared denoiser serve every scene. Scenes sit in a table as small per-scene GPU objects (geometry, BLASes, materials). On top of that, step 5a adds:
  - per-scene environments with importance-sampled sun and moon discs;
  - per-point light selection, plus a light tree for thousands of lights;
  - a trace resolution separate from the output, with a temporal upscaler and GPU timers;
  - a hue-preserving tonemap;
  - an integrator that cuts noise at its sources (path regularisation, clamping only after a rough vertex, camera-only emitters, hero-wavelength dispersion);
  - denoiser fixes, plus a separately filtered volume layer;
  - analytic sphere geometry and per-frame particle BLASes;
  - per-object motion vectors and an export shutter for motion blur;
  - deterministic emissive splats;
  - participating media (height fog and baked density grids);
  - caustic photon maps;
  - the Studio polish.
- **Step 5b, the scenes:** ten scene tasks build on that foundation. Each owns its own files and its own rows in the scene table.

**Tech Stack:** C++20, Vulkan 1.3 with the extensions already required (`VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`, `VK_KHR_ray_query`), GLSL 460 (`GL_EXT_ray_tracing`, `GL_EXT_ray_query`, `GL_EXT_scalar_block_layout`), VMA, the step 2–4 foundation (`Image`, `Buffer`, `FrameRing`, `ComputePipeline`, `DescriptorLayout`, `RayTracingPipeline`, `GeometryStore`, `BlasSet`, `Tlas`, `PathTracer`, `Denoiser`, `Tonemap`), and Python 3 for the export tests, the bench and the storyboard.

**Spec:** `evolution/docs/plans/2026-10-21-evolution-design.md`. The sections this plan draws on:

- §2.3: the chapter plan, specifically its 2026 column.
- §2.4: Acts II and III. Act III's worlds are variants of Act II's scenes.
- §3.3: the frame graph (particles, volume, accumulate, à-trous).
- §3.4: path tracing.
- §3.5: particles. They are stateless, so a particle's position is f(id, t).
- §3.7: determinism.
- §3.8: export.
- §6: verification.
- §7, step 5: "Moderna scener: partiklar (compute), nebulosa (volym), tunnel, glas-glenz, lins/schack, kulor, stad, skepp". It is done when "Alla 2026-rutor".
- §8: risks.

The plan also draws on `HANDOFF.md`: "Steg 4 — KLART", the RTX checklist, the deferred items, and "Nästa steg: steg 5" (binding). Two step 4 reviews feed into it as well: Task 10's Studio review (art direction and noise sources) and the whole-branch review (`.superpowers/sdd/2026-10-26-step4-rt-foundation/review-task10.md`, `whole-review.md`).

## Global Constraints

- **Code location, libraries and naming:**
  - Everything lives under `evolution/`, in the namespaces `evo::gfx`, `evo::rt`, `evo::modern`, `evo::post` and `evo::render`.
  - Scene code goes in `src/modern/scenes/<scene>.hpp/.cpp`. Scene compute shaders go in `shaders/modern/<scene>_<what>.comp` and load as `modern_<scene>_<what>.comp.spv`; Task 1 adds the directory to `evo_add_shaders`.
  - Library links are unchanged except that `evo_modern` gains `evo_post`, because `SplatPass` and the volume bake are recorded by `ModernStage` through `evo_render`. If nothing in `src/modern` needs `post/`, leave the link out.
  - Style, commit rules and the trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` follow the SDD global-constraints file.
- **No new device extensions or features.** Intersection shaders (procedural AABB geometry), 3D storage images and 32-bit integer storage-image atomics are core in Vulkan 1.3 with the RT pipeline.
  - `maxPipelineRayRecursionDepth = 1`. Only `pathtrace.rgen` traces, and compute passes trace through ray queries.
  - All geometry is opaque: no any-hit shaders, and every instance has `TRIANGLE_FACING_CULL_DISABLE`.
- **Every struct shared with GLSL uses `layout(scalar)`.** Its C++ mirror has plain 4-byte fields and a `static_assert` on `sizeof` and on every `offsetof`, and is checked field by field on the GPU by `material_test`'s layout probe. Task 1 adds every step 5 struct there. A later task that changes a struct updates the probe in the same commit.
- **Determinism.** Every picture is a pure function of (options, show time) in Export, and of (options, the frames since the last history reset) in the window, exactly as in step 4:
  - A scene's `frame()` and `camera()` are pure functions of `show::TimelineFrame`. Cameras read only the camera-safe signals (`cameraDrive`, `flowSeconds`, the beat grid, the chapter's fixed facts). Instances, materials and lights may also read `kick`, `bass`, `energy` and `beatPhase`.
  - Every compute pass is a pure function of (id, seconds, params): particles, splats, the volume bake, photons.
  - The only atomics allowed are **32-bit integer atomics into fixed-point accumulators**. Integer addition is associative, so the order of arrival does not matter. Float atomics, and reductions in an undefined order, are forbidden.
  - The export shutter's slice times are a pure function of (t, fps, spp).
- **Integrator defaults:**
  - Interactive: 1 spp, but 4 on a reset frame and 2 on the next (Task 7). At most 4 bounces.
  - Export: `--spp` (default 64), at most 6 bounces.
  - Russian roulette starts at bounce 3.
  - **`clampIndirect` defaults to 16 and applies only to contributions after a rough vertex** (Task 6). Path regularisation raises roughness to at least 0.25 after the first non-delta vertex.
  - A non-finite sample contributes 0.
  - Exports trace in dispatches of `kSppPerDispatch = 4` samples, with at most `kMaxShutterSlices = 8` time slices.
- **Assets:**
  - Every 2026 asset is procedural C++ or GLSL: meshes, materials, patterns, skies, densities and particle targets.
  - The one external piece is the credited Future Crew glenz mesh (`retro::glenzMesh()`).
  - The 1993 counterparts are re-used for timing and paths: `retro::dotsSchedule`, `retro::tunnelPath`, `retro::glenzBounce`, `retro::cityLot` and `retro::flybyCamera`. A wipe or swap therefore shows the same motion in both eras.
- **World scale:**
  - |coordinate| < 10⁴ units everywhere. "Planetary" chapters fake scale with units, fog and camera rebasing per shot, not with 10⁶-unit coordinates.
  - The 2026 cards' numbers are the scenes' full-detail configuration. Each scene's test pins its card's number against its config. Cards computed at run time belong to step 6.
- **Per-scene performance budget** (RTX 4090 estimate; the owner measures it with `--bench`, Task 4):
  - Window at 2560×1440 output, traced at scale 1: 1 spp, 4 bounces, whole GPU frame ≤ 14 ms. Within that, trace ≤ 9 ms; dynamic BLAS build, volume bake, splats and photons ≤ 3 ms together; denoise, upscale and tonemap ≤ 2 ms.
  - Export at 3840×2160 and 64 spp: ≤ 2.5 s per frame.
  - VRAM with every scene prepared at 4K: ≤ 5 GB (Task 1's `gpuBytes()`).
- **Tests on lavapipe:**
  - Validation is on, and tests assert zero validation messages. The AS, path tracer, denoiser, spheres, motion and caustics tests also enable sync validation.
  - C++ tests render at most 320×180 and 8 spp. The named convergence tests run at 64×64.
  - Scenes are built with `SceneDetail{1.f / 256}` or smaller, which shrinks particle, splat and voxel counts.
  - Each test executable stays **under 60 s alone**.
  - Python export tests pass `--spp 1` or `2` and `--detail 0.004`.
  - Heavy GPU tests are registered through `evo_gpu_test()` (Task 1), with `TIMEOUT 300` and `PROCESSORS 2`.
  - Setup through `Context::immediate()` carries `// setup`.
- **Windows/MSVC:**
  - `std::numbers`; no VLAs or GCC attributes.
  - `std::llround(double)` for sample clocks.
  - `VkTransformMatrixKHR` is row-major 3×4 from `Mat34`.
  - Every trace stays split into dispatches of 4 spp or fewer. Splitting a frame across submits is still an RTX-checklist measurement (Decision 18).

## Review Focus

1. **Seeking into a dynamic scene.** Particles, baked volumes, splats, photon maps, previous-frame transforms and the upscaler's history must never carry state from another time. The first frame after a seek must equal a fresh renderer's frame, in the window and in exports.
   - Pinned in Task 9 (g), Task 11 (h) and Task 4 (b).
   - Task 26 extends it to one seek per scene.
2. **MIS consistency with per-point and tree light selection.** The selection pdf at a BSDF hit must be evaluated from the *previous* vertex with the same function that picked the light. Otherwise lights come out too bright or too dark, with no crash.
   - Pinned in Task 3 (c) (NEE-only equals BSDF-only) and Task 12 (d).
3. **The window and the export must agree** under the new features (upscaler, reset-frame spp, firefly filter, shutter):
   - at trace scale 1, the first window frame equals an export frame at spp 4;
   - the export is pure;
   - the shutter never samples another chapter.
   - Pinned in Task 4 (a), Task 7 (b) and Task 9 (f, g).
4. **Capacity and precision anywhere in the show.** A TLAS, light, material, particle or splat capacity exceeded mid-show throws. Planetary coordinates give acne.
   - Every scene task sweeps its chapters on the CPU every 1/60 s against its capacities and checks every transform (`checkSceneSweep`).
   - Task 26 streams the whole show at 32×18, 1 fps, with no throw.
5. **Memory and start-up with every scene prepared.** `prepareAll()` in the window must not multiply render targets per scene, and must stay within 5 GB at 4K.
   - Pinned in Task 1 (b), with image bytes independent of scene count.
   - Task 15 adds the 4K estimate.

---

# Step 5a — Foundation and Studio polish (Tasks 1–15)

## Parallel execution (three implementers, worktrees `/home/user/wt-a`, `wt-b`, `wt-c`)

Merge order inside a wave is left to right. Merge conflicts are expected only in append-only `CMakeLists.txt` blocks and in the one-line-per-feature lists Task 1 creates (`TraceExtras` writes in `path_tracer.cpp`, `kSceneOfEffect`).

| Wave | Tasks | Depends on | Main files |
|---|---|---|---|
| 1 | **1** ∥ **5** ∥ **7** | — | 1: `src/modern/{scene,modern_scene,scenes,path_tracer,scene_gpu}.*`, `src/rt/gpu_types.hpp`, `shaders/rt/{types,common,pathtrace,lights,hit}.*`, new hook stubs `shaders/rt/{environment,medium,lensing,caustics}.glsl`, `src/render/modern_stage.*`, `src/app/{options,app,capture}.*` (`--detail`), `tests/scene_fixture.hpp`, CMake helpers. 5: `src/post/tonemap*`, `shaders/post/{tonemap.comp,neutral.glsl}`, `shaders/fill_hdr.comp`. 7: `src/post/denoiser.*`, `shaders/post/{accumulate,atrous,combine,firefly}.comp`, `src/render/{renderer,stage_plan}.*` |
| 2 | **2** ∥ **3** ∥ **4** | all ←1 | 2: `shaders/rt/{environment,sky}.glsl`, `src/modern/environment.*`. 3: `shaders/rt/lights.glsl`, `src/modern/light_select.*`. 4: `src/post/upscale.*`, `shaders/post/upscale.comp`, `src/gfx/gpu_timers.*`, `src/render/{modern_stage,renderer}.*`, `src/app/{options,app,bench}.*` |
| 3 | **6** ∥ **8** ∥ **10** | 6←1; 8←1; 10←1 | 6: `shaders/rt/{bsdf,pathtrace}.*`, `shaders/rt/spectral.glsl`. 8: `src/rt/{spheres,particles}.*`, `shaders/rt/sphere.rint`, `shaders/rt/hit.glsl`, `src/modern/path_tracer.cpp` (hit groups). 10: `src/modern/splats.*`, `shaders/modern/splat_*.{glsl,comp}` |
| 4 | **9** ∥ **11** ∥ **14** | 9←8; 11←1,7; 14←2,3,5,6 | 9: `src/rt/accel.*` (builds per frame), `src/modern/{path_tracer,shutter}.*`, `shaders/rt/{hit.glsl,pathtrace.rgen}` (guides). 11: `src/modern/medium.*`, `shaders/rt/medium.glsl`, `shaders/modern/medium_common.glsl`. 14: `src/modern/studio.*`, `shaders/rt/screen.glsl`, `shaders/rt/emissive.rchit` |
| 5 | **12** ∥ **13**, then **15** | 12←3,8; 13←6,8,10 | 12: `src/rt/light_tree.*`, `shaders/rt/light_tree.glsl`. 13: `src/modern/caustics.*`, `shaders/modern/photons.comp`, `shaders/rt/caustics.glsl`. 15: docs, storyboard |

---

### Task 1: One shared path tracer and denoiser, a scene table, and the step 5 GPU ABI

**Suggested model:** opus. This is the hub task: it creates every interface and stub that later tasks fill in, so that parallel tasks never edit the same lines.

**Files:**
- Create:
  - `src/modern/scene_gpu.hpp/.cpp`;
  - `shaders/rt/{environment,medium,lensing,caustics}.glsl` as stubs with final signatures (below);
  - `tests/scene_fixture.hpp/.cpp`;
  - `tests/scene_table_test.cpp`, `tests/shared_tracer_test.cpp`.
- Modify:
  - `src/modern/{scene.hpp,modern_scene.hpp,scenes.cpp,path_tracer.hpp/.cpp,studio.hpp/.cpp}`;
  - `src/rt/gpu_types.hpp`, `shaders/rt/{types,common}.glsl`;
  - `shaders/rt/pathtrace.rgen`: restructured around the hooks, byte-exact output;
  - `shaders/rt/lights.glsl`: the per-point signatures, with fixed weights still behind them;
  - `src/render/modern_stage.hpp/.cpp`, `src/render/renderer.hpp/.cpp`;
  - `src/app/{options,app,capture}.*`: `--detail F`, and `prepare` per scene in the range;
  - `src/main.cpp`: `sceneName` for `--show-info`;
  - `tests/{material_test,studio_test,renderer_test}.cpp`, `tests/stream_capture_test.py`: the traced part comes from the `scene` field, and the five Studio chapters must still be in it;
  - `CMakeLists.txt`: the `shaders/modern/` directory, `evo_gpu_test(name exe [TIMEOUT s] [PROCESSORS n])`, and `evo_scene_test(scene)` = `evo_gpu_test(scene_<scene> scene_<scene>_test TIMEOUT 300 PROCESSORS 2)` linked with the fixture.

**Interfaces:**
```cpp
namespace evo::modern {
enum class SceneId : std::uint32_t { Placeholder, Studio, Ship, Particles, Nebula, Tunnel, Glass, Lens, Spheres,
                                     City, Cosmos, Singularity, Count };
struct SceneDetail { float scale = 1.f; };      // (0, 1]: particle, splat, voxel and photon counts scale with it
struct SceneCapacity { std::uint32_t instances = 1024, lights = 64, materials = 256, spheres = 0,
                       particles = 0, splats = 0; };
// SceneStatic gains: SceneCapacity capacity; std::vector<rt::SphereRecord> spheres;  (static analytic spheres)
//   std::filesystem::path particleShader, splatShader, mediumShader;   (empty: none)
// SceneInstance gains: rt::GeometryKind kind = Triangles; std::uint32_t firstSphere = 0, sphereCount = 0;
//   bool dynamicSpheres = false (the particle field instead of static spheres)
// SceneFrame gains: std::vector<rt::Material> materials (empty: the static table; else the whole table, this
//   frame — the per-chapter light rig, blinking windows); EnvironmentDesc environment (Task 2; default: the
//   step 4 night sky); std::optional<ParticleFrame> particles (Task 8); std::optional<SplatFrame> splats
//   (Task 10); std::optional<MediumFrame> medium (Task 11); std::vector<TreeLightFrame> treeLights (Task 12);
//   std::optional<CausticFrame> caustics (Task 13); std::optional<LensingFrame> lensing (Task 25).
//   Each is declared here as an empty struct in its own header (environment.hpp, particles.hpp, ...) that the
//   owning task fills; Task 1 only wires "absent -> dummy binding".
class ModernScene {   // + camera(); frame().camera must equal camera() (tested per scene)
public:
    virtual Camera camera(const show::TimelineFrame&) const = 0;   // cheap: shotFor uses it (step 4 M3)
};
struct SceneEntry { SceneId id; const char* name; std::unique_ptr<ModernScene> (*make)(SceneDetail); };
std::span<const SceneEntry> sceneTable();      // indexed by SceneId; Placeholder's make is nullptr
SceneId sceneFor(show::Effect);                // kSceneOfEffect, one row per effect in scenes.cpp
const char* sceneName(SceneId);
using SceneFactory = std::function<std::unique_ptr<ModernScene>(SceneId, SceneDetail)>;  // tests inject scenes

class SceneGpu {   // one per prepared scene: GeometryStore, BlasSet (+ static SphereBlas, Task 8), material buffer
public:
    SceneGpu(gfx::Context&, const SceneStatic&);
    const SceneStatic& source() const; const rt::GeometryStore& geometry() const; const rt::BlasSet& blases() const;
    const gfx::Buffer& staticMaterials() const; bool samplesRetro() const; VkDeviceSize bytes() const;
};
struct TracerCapacity { std::uint32_t instances, lights, materials; };   // max over prepared scenes
struct TraceExtras { /* one const pointer per feature resource, null -> a 1-element dummy:
    environment buffer (15), tree nodes (16), tree lights (17), medium buffer (18), density image (19),
    majorant image (20), previous transforms (21), static spheres (22), particle spheres (23), caustic maps (24),
    lensing buffer (25) */ };
class PathTracer {   // shared by every scene: pipeline + SBT, TLAS + scratch, per-slot buffers, G-buffer
public:
    PathTracer(gfx::Context&, gfx::FrameRing&, std::uint32_t width, std::uint32_t height, const TracerCapacity&);
    void reserve(const TracerCapacity&);   // setup: grows the TLAS and per-slot buffers (waits for the ring)
    void resize(std::uint32_t width, std::uint32_t height);
    void trace(VkCommandBuffer, const SceneGpu&, const SceneFrame&, const std::optional<Camera>& previousCamera,
               const TraceSettings&, gfx::Image* retroScreen, const TraceExtras& = {});
    GBuffer& gbuffer(); Stats lastStats() const; VkDeviceSize imageBytes() const;
    static std::uint32_t pipelinesCreated();   // test hook: process-wide count
};
// GBuffer gains `volume` (RGBA16F: rgb radiance in-scattered on the primary segment, a its transmittance;
// (0,0,0,1) without a medium — the tracer writes that until Task 11).
}
namespace evo::render {
// ModernStage(ctx, ring, w, h, modern::SceneDetail = {}, modern::SceneFactory = {}): one PathTracer and one
// Denoiser (created on the first prepare), a SceneGpu per prepared scene.
//   void prepare(modern::SceneId); void prepareAll(); VkDeviceSize gpuBytes() const;
//   modern::PathTracer* tracer();   (replaces studioTracer())
// Renderer::prepare(std::span<const double> showSeconds) prepares the scenes of those times; the window calls
// prepareAll(). RenderSettings gains modern::SceneDetail detail; options `--detail F` (0 < F <= 1, default 1).
}
```

**The ABI.** Task 1 adds these to `gpu_types.hpp` and `types.glsl`, together with the layout probe.

- **`Material` becomes 96 bytes.** It appends `std::uint32_t flags; float abbe; std::uint32_t medium; float pad;` at offsets 80/84/88/92, all defaulting to 0, with `rt::kNoMedium = ~0u`. The flags are:
  - `kMaterialCameraOnlyEmission = 1`
  - `kMaterialCausticReceiver = 2`
  - `kMaterialThinWalled = 4`
- **`RectLight.pad` is renamed `gate`.** It holds the index of a Portal the light is seen through, or `kNoLight`. `LightKind` gains `Tree = 3`.
- **`InstanceRecord.pad` is renamed `flags`.** Bit 0 marks spheres and bit 1 a dynamic sphere set. For sphere instances, `mesh` holds `firstSphere`.
- **`FrameUniforms` grows to 208 bytes.** `pad0` becomes `float regularizeRoughness` and `pad1` becomes `std::uint32_t treeLightCount`. It then appends `std::uint32_t mediumFlags, causticReceivers, lensing, pad2; float shutterOffset, environmentSelect; std::uint32_t sliceIndex, sliceCount`.
- **New structs**, each in its own header with its binding reserved:
  - `EnvironmentUniforms`, 112 B (Task 2 defines its fields; Task 1 puts a stub in `environment.hpp`).
  - `MediumUniforms`, 128 B (Task 11).
  - `SphereRecord`, 48 B: `center[3], radius, color[3], material, prevCenter[3], emission`.
  - `TreeLight` and `LightTreeNode` (Task 12).
  - `LensingUniforms`, 32 B (Task 25).

**The GLSL hooks** keep the step 4 behaviour exactly until their owner fills them in:

- `environment.glsl`: `environmentView(d, footprint)` and `environmentLighting(d)` both return `skyRadiance(d)`. `sampleEnvironment` returns false and `environmentPdf` returns 0.
- `medium.glsl`: `mediumActive()` returns false.
- `lensing.glsl`: `lensingActive()` returns false.
- `caustics.glsl`: `causticRadiance(HitPoint)` returns 0.
- `lights.glsl`: `pickLight(u, p, n, transmissive, …)` and `lightSelectPdf(i, p, n, transmissive)` take the shading point but still use `selectWeight / lightWeightSum`.
- The raygen keeps the previous vertex (`prevP`, `prevN`, `prevTransmissive`) for the BSDF-hit MIS.
- An escaped ray's NEE pdf goes through `escapePdf(o, d) = (1 − environmentSelect) · portalPdf(o, d) + environmentSelect · environmentPdf(d)`. That equals `portalPdf` while `environmentSelect` is 0.

- [ ] **Step 0: Record the step 4 picture.** Capture `--capture-dir` STUDIO NIGHT at 40 % and REBOOT at 60 % (160×90, `--spp 2`) into `build-out/step5-baseline/`.
- [ ] **Step 1: Write the failing tests.**
  - `scene_table_test` (CPU):
    - every effect has a row;
    - the five Studio effects map to Studio and every other effect to Placeholder;
    - `sceneName` round-trips, and `sceneTable()[id].id == id`;
    - `--show-info` scene names come from the table;
    - `Studio::camera(f) == Studio::frame(f).camera` at 500 times across its chapters;
    - `makeScene(Studio, {0.25})` builds.
  - `shared_tracer_test` (sync validation; a factory makes `SceneId::Ship` a Cornell box from `rt_test_scenes.hpp`):
    - (a) Render STUDIO NIGHT, then a FLYBY frame (the Cornell box, forced to 2026), then STUDIO NIGHT again. The last frame equals a fresh renderer's STUDIO NIGHT frame byte for byte.
    - (b) `pipelinesCreated()` rises by exactly 1 for both scenes. `gpuBytes()` minus the two `SceneGpu::bytes()` equals the images of one scene.
    - (c) Per-frame materials: the Cornell box's left wall overridden from red to blue turns the left-wall pixels blue. With empty `materials` the picture equals the static one.
    - (d) A capacity of 4096 instances traces 3000. Asking for 4097 throws `std::length_error`.
    - (e) `shotFor` does not call `frame()`: a counting test scene sees 0 calls.
  - `material_test`: every new and changed struct passes the layout probe.
  - `stream_capture_test.py`: the traced part is the set of chapters whose `scene` is not "Placeholder", and the five Studio chapters must be in it.
- [ ] **Step 2: Run** `ctest --preset linux -R "scene_table|shared_tracer|material"`. Expect FAIL.
- [ ] **Step 3: Implement.** Split `PathTracer` into the shared tracer and `SceneGpu`. Replace `ModernStage::Traced` with `std::array<std::optional<SceneGpu>, kSceneCount>`, one tracer and one denoiser. Restructure the raygen around the hooks. Add `--detail`, and `prepare` per scene in capture and render-stream.
- [ ] **Step 4: Run.**
  - The full suite.
  - The Step 0 captures again: `cmp` must report identical bytes, since the refactor is byte-exact.
  - Budget: `shared_tracer` under 40 s alone.
- [ ] **Step 5: Commit** `Share one path tracer and denoiser across a table of scenes`.

---

### Task 2: Environments per scene and sky sampling

**Suggested model:** opus.

**Files:**
- Create: `src/modern/environment.hpp/.cpp` (fills Task 1's stub), `shaders/test/environment_probe.comp`, `tests/environment_test.cpp`.
- Modify:
  - `shaders/rt/environment.glsl`: fills the stub.
  - `shaders/rt/sky.glsl`: the night sky becomes `EnvironmentKind::NightSky`, with filtered analytic stars and town lights.
  - `src/rt/sky_constants.hpp`: the moon direction stays only as the NightSky default.
  - `src/modern/path_tracer.cpp`: uploads the uniforms at binding 15.
  - `tests/material_test.cpp`: layout.

**Interfaces:**
```cpp
namespace evo::modern {
enum class EnvironmentKind : std::uint32_t { NightSky = 0, Space = 1, Dusk = 2, Day = 3 };
struct EnvironmentDesc {
    EnvironmentKind kind = EnvironmentKind::NightSky;
    Vec3 discDirection{rt::kMoonDirection[0], rt::kMoonDirection[1], rt::kMoonDirection[2]};
    float discAngularRadius = 0;   // radians; 0: no disc (the Studio's moon stays its own emitter)
    Vec3 discRadiance{};           // inside the disc
    float intensity = 1, starDensity = 1, starBrightness = 1, townDensity = 1, haze = 0;
    Vec3 zenith, horizon, ground;  // environmentDefaults(kind) fills them
    float select = 0;              // probability that NEE samples the environment instead of a rect light
                                   // (1 when the scene has no lights; outdoor scenes ~0.5; the Studio 0)
};
EnvironmentDesc environmentDefaults(EnvironmentKind);
rt::EnvironmentUniforms packEnvironment(const EnvironmentDesc&);  // 112 B: kind, intensity, discCosRadius,
    // discShare, discDirection, domeShare, discRadiance, starDensity, zenith, starBrightness, horizon,
    // townDensity, ground, haze, glowDirection, glowStrength
}
```
```glsl
vec3 environmentView(vec3 d, float footprint);   // primary and specular-chain misses: gradient, disc with limb
                                                 // darkening, glow, filtered stars, town lights
vec3 environmentLighting(vec3 d);                // NEE and misses after a rough vertex: gradient + glow + disc only
bool sampleEnvironment(vec2 u, out vec3 wi, out vec3 le, out float pdf); // disc (uniform cone) or dome
                                                 // (cosine about +y), chosen by discShare
float environmentPdf(vec3 d);                    // the same mixture's density
```
**Rules:**
- **Which sky a ray sees:**
  - Portal NEE and a BSDF ray escaping through a portal after a rough vertex see `environmentLighting`.
  - Primary and specular-chain misses see `environmentView`. Its disc is the same disc that NEE samples, so MIS stays consistent.
- **Stars:** one star per hashed cube cell, at a jittered position inside the cell. Its radiance is `flux · G(angle; σ)` with `σ = max(σ_star, footprint)`, normalised so that the star's integral over solid angle equals its flux, whatever the resolution. This is review I3.
- **Town lights:** fewer, clustered along hashed street lines, with haze towards the horizon.

- [ ] **Step 1: Write the failing tests** (`environment_test`; GPU probe plus the path tracer at 64×64).
  - (a) Disc sampling: the pdf of each sample equals `environmentPdf` within 1e-4. It integrates to 1 over the sphere (Monte Carlo, ±1 %).
  - (b) A diffuse plane under a `Day` sky with a sun disc of 0.5°: NEE+BSDF equals BSDF-only within 3 %. The per-pixel variance at 16 spp is at least 4× lower with NEE.
  - (c) The lighting sky sees no stars: the plane's `lighting` is identical with `starBrightness` 0 and 1000. A primary ray straight at a star cell sees it.
  - (d) Flux conservation: the stars' summed `emission` over a 9×9-cell patch at 160×90 and at 320×180 agrees within 2 %.
  - (e) The Studio's mean luminance stays within 5 % of the step 4 baseline (Task 1, Step 0).
  - (f) The layout probe passes.
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Budget < 40 s.
- [ ] **Step 5: Commit** `Give every scene its own sky, and sample its sun and dome`.

---

### Task 3: Per-point light selection

**Suggested model:** opus.

**Files:**
- Create: `src/modern/light_select.hpp/.cpp` (CPU reference), `shaders/test/light_select_probe.comp`, `tests/light_selection_test.cpp`.
- Modify: `shaders/rt/lights.glsl` (the three functions Task 1 declared), `src/modern/studio.cpp` (sets `gate` on the moon light).

**Interfaces:**
```glsl
float lightImportance(uint i, vec3 p, vec3 n, bool transmissive);
bool  pickLight(float u, vec3 p, vec3 n, bool transmissive, out uint index, out float pdf);   // two passes, N <= 64
float lightSelectPdf(uint index, vec3 p, vec3 n, bool transmissive);                         // the same weights
```
```cpp
namespace evo::modern { float lightImportanceReference(const rt::RectLight&, Vec3 p, Vec3 n, bool transmissive,
                                                       float screenLuminance, float portalLuminance); }
```
**The importance of a light** is `w = selectWeight · front · Φ · cosBound / d²`, where:

- `front`: p is on the emitting side of the light's plane.
- `Φ`, the light's power:
  - Emitter: `luminance(emission) · area`.
  - RetroScreen: `emissionScale · luminance(textureLod(retroScreen, vec2(.5), levels − 1)) · area`. This is the 1×1 mip, already on the GPU, so it needs no readback.
  - Portal: `luminance(environmentLighting(−normal)) · area`.
- `cosBound`: 1 for a transmissive surface. Otherwise the maximum of `max(dot(n, normalize(q − p)), 0)` over the light's four corners and its centre.
- `d²`: `max(|centre − p|², area / 4)`.
- **The gate:** a light with `gate != kNoLight` gets 0 unless the segment from p to its centre crosses that portal (`crossesPortal`).
- **Zero importance everywhere:** if every light has importance 0, `pickLight` returns false.

- [ ] **Step 1: Write the failing tests** (`light_selection_test`).
  - (a) The GPU probe equals the CPU reference within 1e-4 relative, for 3 lights × 50 points.
  - (b) Pick frequencies over 65 536 values of u equal the pdf within 1 %. Σ pdf = 1 ± 1e-5.
  - (c) MIS stays unbiased: a Cornell box with three lights of powers 1, 30 and 900, at 64×64 and 32 spp. NEE-only and BSDF-only means agree within 3 %. The per-pixel variance is at most 0.6× that of the fixed weights.
  - (d) Studio with the DOS screen (nearly black): the screen's selection probability at a desk point is < 5 % (step 4: 30 %). With an all-white retro image it is > 20 %.
  - (e) The moon's probability is 0 at a point that cannot see it through the window.
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Budget < 40 s.
- [ ] **Step 5: Commit** `Choose lights by their importance at each shading point`.

---

### Task 4: Trace resolution, temporal upscaler, GPU timers and `--bench`

**Suggested model:** opus.

**Files:**
- Create:
  - `src/post/upscale.hpp/.cpp`, `shaders/post/upscale.comp`;
  - `src/gfx/gpu_timers.hpp/.cpp`;
  - `src/app/bench.hpp/.cpp`;
  - `tests/upscale_test.cpp`, `tests/bench_test.py`.
- Modify: `src/render/{modern_stage,renderer}.hpp/.cpp`, `src/app/{options,app}.*`, `README.md` (CLI).

**Interfaces:**
```cpp
namespace evo::post {
struct UpscaleSettings { bool temporal = true; float feedback = 0.9f; };
class Upscaler {   // hdrIn at the trace size -> hdrOut at the output size; identity (one copy) when equal
public:
    Upscaler(gfx::Context&, gfx::FrameRing&, std::uint32_t inW, std::uint32_t inH, std::uint32_t outW, std::uint32_t outH);
    void resize(std::uint32_t inW, std::uint32_t inH, std::uint32_t outW, std::uint32_t outH);
    void resetHistory();
    void record(VkCommandBuffer, gfx::Image& hdrIn, gfx::Image& depth, gfx::Image& motion,
                const UpscaleSettings&, gfx::Image& hdrOut);
};
}
namespace evo::gfx {
class GpuTimers {   // a timestamp query pool per ring slot; results of a slot are read when the ring reuses it
public:
    GpuTimers(Context&, FrameRing&, std::uint32_t maxScopes = 16);
    void begin(VkCommandBuffer, const char* name); void end(VkCommandBuffer);
    std::vector<std::pair<std::string, double>> lastCompleted() const;   // milliseconds
};
}
// RenderSettings gains: float traceScale = 1;  trace size = max(16, round(output * scale)) per axis.
// Options: --trace-scale F (0.25..1; window default 0.67, exports 1); --bench; --bench-frames N (default 120);
// --chapters LIST (bench only). Renderer::Stats gains gpuMs (name -> ms, from the frame that last completed).
```
**Behaviour.**
- **The upscaler:**
  - The current frame is Catmull-Rom upsampled from the trace size.
  - The history is reprojected with `motion` (scaled to output pixels) and Catmull-Rom sampled.
  - The history is clipped to the current frame's 3×3 YCoCg mean ± 1σ, then blended with `feedback`. A reset sets feedback to 0.
  - Export always runs at scale 1, so the upscale is a copy.
- **`--bench`** runs Interactive headless. For each chapter it renders the chosen number of frames from the chapter's 20 % point, then prints one JSON object per line: `{chapter, title, scene, frames, traceScale, width, height, gpuMs:{trace, denoise, upscale, tonemap, compositor, total}, fps}`.

- [ ] **Step 1: Write the failing tests.** `upscale_test` (64×36 → 128×72):
  - (a) At scale 1 the output equals the input byte for byte, so Review Focus 3 still holds at scale 1.
  - (b) After a reset, the output equals the pure spatial upsample.
  - (c) A static high-frequency pattern with per-frame subpixel-jittered input: after 16 frames the RMSE against a native 128×72 render is < 0.8× the bilinear upsample's RMSE.
  - (d) A pattern moving 2 px per frame leaves no ghost wider than 2 px after 8 frames.
  - (e) A NaN input gives a finite output.

  `GpuTimers`: the scopes are non-negative and in order. `bench_test.py`: `--bench --chapters 11 --bench-frames 3 --width 96 --height 54` prints one valid JSON line with all the keys and exits 0.
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Budget < 30 s each.
- [ ] **Step 5: Commit** `Trace below the output size, upscale in time, and time every pass`.

---

### Task 5: A hue-preserving tonemap (Khronos PBR Neutral)

**Suggested model:** sonnet-class is enough; opus if available.

**Files:**
- Modify: `src/post/tonemap_reference.hpp/.cpp`, `src/post/tonemap.hpp/.cpp`, `shaders/post/tonemap.comp`, `shaders/fill_hdr.comp` (the exact inverse), `tests/tonemap_test.cpp`, `README.md`.
- Replace: `shaders/post/aces.glsl` with `shaders/post/neutral.glsl`.

**Behaviour.** `tonemapReference` keeps its signature. Its pipeline is:

1. `hdr · 2^EV`.
2. Clamp negatives to 0.
3. Khronos PBR Neutral, with `startCompression = 0.76` and `desaturation = 0.15`:
   - `offset = x < 0.08 ? x − 6.25x² : 0.04`, where x is the minimum component;
   - the peak compression `1 − d²/(peak + d − start)`;
   - the mix towards `newPeak` white by `1 − 1/(desat·(peak − newPeak) + 1)`.
4. Clamp to [0, 1].
5. The sRGB OETF.
6. The static triangular dither, as in step 4.

The placeholder's `fill_hdr.comp` inverts this curve exactly.

- [ ] **Step 1: Write the failing tests.**
  - The GPU equals the CPU within ±1 on the ramp and on the primaries at 4 intensities.
  - 0 → 0, and white ×64 → 255.
  - Each channel is monotonic along the ramp.
  - Hue: for inputs whose peak after the offset is < 0.76, the output's HSV hue equals the input's within 1°. Above that, the output stays in the plane spanned by the input and white.
  - The CRT red (0.8, 0.1, 0.1) × 6.8 keeps `r ≥ 2·g` in output bytes. Under ACES that test fails; record that in the commit message.
  - The placeholder picture is unchanged within ±1 (the existing renderer tests).
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Budget < 10 s.
- [ ] **Step 5: Commit** `Tone map with Khronos PBR Neutral so saturated light keeps its hue`.

---

### Task 6: Integrator v2 — regularisation, clamp after a rough vertex, camera-only emitters, dispersion

**Suggested model:** opus (sampling maths).

**Files:**
- Create: `shaders/rt/spectral.glsl`, `src/rt/spectral.hpp` (the CPU mirror), `tests/integrator_test.cpp`, `tests/rt_test_scenes.hpp` additions (prism, mirror caustic box, LED box).
- Modify: `shaders/rt/{bsdf.glsl,pathtrace.rgen,hit.glsl}`, `src/modern/path_tracer.{hpp,cpp}` (`TraceSettings::regularizeRoughness = 0.25`, `clampIndirect = 16`), `src/modern/scene.hpp` (default `clampIndirect = 16`), `tests/{path_tracer_test,material_test}.cpp`.

**Behaviour.**
- **Path state:** the raygen tracks `rough` (true after the first non-delta scattering) and `chainDelta` (every vertex so far was delta).
- **Regularisation:** once `rough` is set, every later lobe uses `max(roughness, regularizeRoughness)`, and the coat roughness too. This turns diffuse→glossy→small-light paths into smooth ones. Caustics come from Task 13 instead.
- **Clamping:** `addContribution` clamps only when `rough`. The primary hit's NEE, and camera→specular chain→light paths, are never clamped.
- **`kMaterialCameraOnlyEmission`:** the emission counts at bounce 0 or while `chainDelta` holds. It never counts after a rough vertex, and is never a light.
- **Dispersion (`abbe > 0`):** at a path's first dispersive dielectric event, choose λ = 380 + 400·u from a reserved dimension.
  - η(λ) follows Cauchy fitted to `ior` at 587.6 nm and the Abbe number (λF 486.1, λC 656.3).
  - The throughput is multiplied by `spectralRgb(λ)`. That is non-negative, and its mean over uniform λ is (1, 1, 1) ± 1e-3 per channel: the CIE 1931 → linear sRGB response with its negative lobes clipped and each channel renormalised.
  - Later dispersive events reuse λ.
  - `abbe = 0` keeps step 4's glass exactly.

- [ ] **Step 1: Write the failing tests** (`integrator_test`, 64×64).
  - (a) A mirror caustic box: diffuse floor, small lamp, mirror. Regularisation cuts the speckle fraction (pixels more than 10× their 3×3 median) at least 5× at 16 spp, and the floor mean moves < 25 % (the bias is documented).
  - (b) Camera → chrome sphere → a lamp of L = 500 reads ≥ 400 with `clampIndirect = 16`. A diffuse→lamp contribution above 16 is clamped.
  - (c) An LED quad with camera-only emission of 1000 shows 1000 when seen directly and through a mirror. The desk patch beside it is byte-identical with the LED at 1000 and at 0.
  - (d) A prism with `abbe 30` in front of a white light, projecting onto a screen: the red and blue centroids are ≥ 3 px apart. A parallel slab shows no colour shift (rgb ratios within 2 %). `abbe 0` is byte-identical to step 4's glass.
  - (e) `spectralRgb` integrates to (1, 1, 1) on the CPU and on the GPU.
  - (f) The step 4 furnace and MIS tests still pass.
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `-R "integrator|path_tracer|material"`. Budget < 50 s.
- [ ] **Step 5: Commit** `Cut noise at its sources and split light by wavelength in glass`.

---

### Task 7: Denoiser — clean first frames, export grain, the volume layer

**Suggested model:** opus. It is built on synthetic G-buffers, as in step 4, so it runs beside Task 1.

**Files:**
- Create: `shaders/post/firefly.comp`.
- Modify:
  - `src/post/denoiser.hpp/.cpp`, `shaders/post/{accumulate,atrous,combine}.comp`;
  - `src/render/{renderer,stage_plan}.hpp/.cpp`: the reset-frame spp;
  - `src/render/modern_stage.cpp`: settings, after Task 1 merges;
  - `tests/{denoiser_test,export_mode_test,seek_determinism_test}.cpp`, `README.md`: Review Focus 3 now names spp 4.

**Interfaces:**
```cpp
// GBufferView gains gfx::Image& volume.  DenoiseSettings gains:
//   std::uint32_t resetAtrousIterations = 5;     // at history length <= 2
//   bool fireflyFilter = false; float fireflyK = 4.f;   // export at spp >= 16 (Decision 16)
//   std::uint32_t volumeAtrousIterations = 4;
// combine: hdr = T * (illumination * albedo + emission) + volumeFiltered, T = volume.a.
// Renderer: an Interactive reset frame traces max(spp, 4) samples, the next max(spp, 2) (kResetSpp {4, 2});
//   HistoryKey keeps the configured spp. Renderer::Stats gains std::uint32_t spp (traced this frame).
```
**Behaviour.**
- **Sparse first frames:** at history length ≤ 2, the variance becomes the plain 3×3 spatial variance (not the robust minimum) whenever ≥ 40 % of the 5×5 neighbourhood has zero luminance, and à-trous runs `resetAtrousIterations` times.
- **The firefly filter:** clamps each pixel's demodulated luminance to `fireflyK` × the second-largest luminance in its 3×3 neighbourhood, before the filter. It is applied in exports only.
- **The volume layer** keeps its own history:
  - reprojected with the surface motion, with no depth or normal test;
  - clamped to the current 3×3 min/max (safe here: the layer is smooth, not sparse);
  - its own à-trous with luminance weights only.

- [ ] **Step 1: Write the failing tests.**
  - (a) Sparse Bernoulli lighting (p = 0.3, value 1/0.3) at length 1: RMSE after denoising < 0.045. Record the step 4 figure (~0.063) in the test.
  - (b) An Interactive renderer reports `Stats.spp` 4, 2, 1 on frames 0, 1, 2. Its first frame equals an Export spp 4 frame byte for byte (`export_mode_test`).
  - (c) Firefly: a flat 0.5 field with one pixel at 500 comes out ≤ 4× the second-largest neighbour. A 2×2 block at 5 is kept within 1 %. The emission is untouched. The filter is off in Interactive.
  - (d) Volume: a constant volume (0.2, 0.1, 0.05, T = 0.5) over a surface of 1 gives exactly 0.5 + volume (in fp16). Noisy volume input: the stddev drops ≥ 3×. With `volume = (0, 0, 0, 1)` the output bytes equal those before this task.
  - (e) A NaN in `volume` keeps the output finite.
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Budget < 20 s.
- [ ] **Step 5: Commit** `Denoise sparse first frames, export grain and volumetric light`.

---

### Task 8: Analytic spheres and per-frame particle BLASes

**Suggested model:** opus (AS lifetimes).

**Files:**
- Create:
  - `src/rt/spheres.hpp/.cpp`, `src/rt/particles.hpp/.cpp`;
  - `shaders/rt/sphere.rint`;
  - `shaders/modern/particles_common.glsl`;
  - `shaders/test/particles_grid.comp`;
  - `tests/spheres_test.cpp`.
- Modify:
  - `shaders/rt/hit.glsl`: `hitPointSphere()` under `GEOMETRY_SPHERES`;
  - the four `*.rchit` files, compiled twice: `-DGEOMETRY_SPHERES` → `rt_<class>_sphere.rchit.spv`;
  - `src/modern/{path_tracer.cpp,scene_gpu.*}`: 8 hit groups, static sphere BLAS, particle field.

**Interfaces:**
```cpp
namespace evo::rt {
enum class GeometryKind : std::uint32_t { Triangles = 0, Spheres = 1 };
inline constexpr std::uint32_t kHitGroupsPerKind = 4;
constexpr std::uint32_t hitGroupFor(MaterialClass c, GeometryKind k) { return std::uint32_t(c) + 4u * std::uint32_t(k); }
class SphereBlas {   // static spheres: one AABB BLAS, compacted, built once (setup)
public: SphereBlas(gfx::Context&, std::span<const SphereRecord>); VkDeviceAddress address() const;
        const gfx::Buffer& records() const; };
class ParticleField {   // dynamic spheres: per ring slot a SphereRecord buffer, an AABB buffer and a BLAS
public:
    ParticleField(gfx::Context&, gfx::FrameRing&, std::uint32_t capacity, const std::filesystem::path& shader);
    // Dispatches the shader (count particles at `seconds`, prevCenter at `previousSeconds`), barrier COMPUTE
    // write -> AS build read, then builds this slot's BLAS (PREFER_FAST_BUILD). May run more than once per
    // frame (shutter slices, Task 9): each later build waits on the previous traces (RT AS read -> build write).
    // Throws std::length_error for count > capacity, std::logic_error outside begin()/submit().
    void record(VkCommandBuffer, std::uint32_t count, double seconds, double previousSeconds,
                std::span<const float, 16> params);
    VkDeviceAddress blasAddress() const; const gfx::Buffer& records() const;   // this slot's
};
}
namespace evo::modern { struct ParticleFrame { std::uint32_t count; std::array<float, 16> params; std::uint32_t material; }; }
```
```glsl
// particles_common.glsl — a scene's particles_<scene>.comp defines this pure function and includes the common main():
SphereRecord particle(uint id, float seconds, vec4 params[4]);   // local_size_x 256; main writes record + AABB
```

**The sphere intersection shader** is a robust quadratic, with one Newton refinement of t. It reports `gl_HitKindFrontFacingTriangleEXT` from outside and the back-facing kind from inside, so that a camera or ray inside a glass sphere refracts out.

**Sphere closest-hits:**
- normal = (p − centre) / radius;
- uv = spherical;
- `baseColor` multiplied by `color`;
- `emission += color · emission`.

- [ ] **Step 1: Write the failing tests** (`spheres_test`, sync validation).
  - (a) 4 static spheres: the depth guide equals CPU ray–sphere within 1e-4, and the normals are radial within 1e-3.
  - (b) A smooth glass sphere matches the `uvSphere(r, 64, 128)` glass sphere's mean within 2 % at 32 spp.
  - (c) `particles_grid.comp` drives 4096 particles moving with time over 32 frames, two in flight. At 5 probe pixels, each frame's depth equals the analytic value for its own time, and `prevCenter` equals the analytic previous position.
  - (d) A count above capacity throws.
  - (e) The Emissive and Checker classes on spheres show emission and the pattern.
  - (f) A camera inside a glass sphere sees a finite, non-black picture.
  - (g) `rt_pipeline_test`'s SBT table with 8 hit groups, on the RTX properties.
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Budget < 45 s.
- [ ] **Step 5: Commit** `Trace analytic spheres and millions of stateless particles`.

---

### Task 9: Object motion vectors and the export shutter

**Suggested model:** opus.

**Files:**
- Create: `src/modern/shutter.hpp/.cpp`, `tests/motion_test.cpp`.
- Modify:
  - `src/rt/accel.hpp/.cpp`: `Tlas(…, maxBuildsPerFrame)`, with an instance-buffer region per build;
  - `src/modern/path_tracer.*`: slices, previous transforms at binding 21, a uniform region per slice;
  - `shaders/rt/{hit.glsl,pathtrace.rgen}`: `prevPosition` in the payload and the guides;
  - `src/render/{modern_stage,renderer}.*`, `src/app/{app,capture}.cpp`: `shutterSeconds = 0.5 / fps` in exports, 0 in the window.

**Interfaces:**
```cpp
namespace evo::modern {
inline constexpr std::uint32_t kMaxShutterSlices = 8;
std::uint32_t shutterSlices(std::uint32_t spp, double shutterSeconds);   // 1 if shutter 0, else min(ceil(spp/4), 8)
double sliceOffset(std::uint32_t k, std::uint32_t slices, double shutterSeconds);  // ((k + .5)/slices - .5) * shutter
// Slice times are clamped into [chapterStart, chapterEnd - 1e-6] so a slice never samples another chapter.
// PathTracer::trace takes std::span<const SceneFrame> slices (one per slice; dispatches split evenly) and a
// std::function<void(VkCommandBuffer, std::uint32_t slice)> beforeSlice (the particle rebuild); guides come
// from slice slices/2. Previous transforms: instance i of the previous frame when both frames have the same
// count and the same (mesh, kind) sequence, else the current one (camera motion only).
}
// RenderSettings gains double shutterSeconds = 0.
```
- [ ] **Step 1: Write the failing tests** (`motion_test`).
  - (a) Interactive, static camera, a quad moving 2 px per frame: motion is (−2, 0) ± 0.01 on the quad and (0, 0) behind it.
  - (b) Changing the instance count falls back to camera-only motion.
  - (c) Particles: motion equals the `prevCenter` shift.
  - (d) The denoiser keeps history on a moving object: after 8 frames its history length is ≥ 6 (step 4: 1).
  - (e) Export spp 8, shutter 1/120, a quad moving 8 px per 1/60 s: its edge is a ramp 4 ± 1 px wide. Shutter 0 gives a ramp ≤ 1 px.
  - (f) `shutterSlices(8, s) = 2`, `(64, s) = 8`, `(4, s) = 1`. Slice clamping: the last frame of FLYBY never evaluates DOTS (CPU).
  - (g) Purity: frame T equals itself after first rendering T − 1/60. Two runs are byte-identical.
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `-R "motion|accel|path_tracer|export_mode"`. Budget < 50 s.
- [ ] **Step 5: Commit** `Give moving objects motion vectors and exports a shutter`.

---

### Task 10: Deterministic emissive splats

**Suggested model:** opus.

**Files:**
- Create:
  - `src/modern/splats.hpp/.cpp`;
  - `shaders/modern/splat_common.glsl`, `shaders/modern/splat_resolve.comp`;
  - `shaders/test/splat_grid.comp`;
  - `tests/splat_test.cpp`.
- Modify: `src/render/modern_stage.cpp`. Splats run after the trace and before the denoiser, adding into the `emission` guide, which bypasses history and filtering.

**Interfaces:**
```cpp
namespace evo::modern {
struct SplatFrame { std::uint32_t count; std::array<float, 16> params; float fixedPointScale = 4096.f; };
class SplatPass {
public:
    SplatPass(gfx::Context&, gfx::FrameRing&, std::uint32_t width, std::uint32_t height);
    void resize(std::uint32_t width, std::uint32_t height);
    // Clears three R32_UINT accumulators, dispatches the scene's splat shader (pipelines cached per path),
    // resolves into `emission` (adds). Throws std::length_error over the scene's splat capacity.
    void record(VkCommandBuffer, const std::filesystem::path& shader, const SplatFrame&, const Camera&,
                gfx::Image& depth, gfx::Image& emission, double seconds);
};
}
```
```glsl
struct SplatPoint { vec3 position; vec3 flux; float radius; };   // flux at a 1920x1080 reference
SplatPoint splatPoint(uint id, float seconds, vec4 params[4]);  // pure; defined by splat_<scene>.comp
```
**Rules:**
- **Visibility:** a point behind the depth guide is skipped. A depth of −1 counts as infinitely far.
- **Footprint:** a Gaussian with `σ = max(projected radius, 0.5 px)`, truncated to 5×5 and **renormalised to sum 1**. The value added per pixel is `flux · w · (W·H)/(1920·1080)`, so a point's energy is the same at every resolution.
- **Accumulation:** fixed point at `fixedPointScale`, with `atomicAdd`.
- **Overflow:** a wrap (the new value smaller than the old) sets an overflow bit in a fourth R32_UINT image through `atomicOr`, and that pixel resolves to 65504. Whether a pixel wraps does not depend on the order of the adds, so this stays deterministic.

- [ ] **Step 1: Write the failing tests** (`splat_test`).
  - (a) One point's summed energy at 160×90 and at 320×180 agrees within 1 %.
  - (b) A point behind the depth guide adds nothing; one in front adds its energy.
  - (c) 2^18 points rendered twice give identical bytes. A shader variant that permutes the ids gives identical bytes.
  - (d) A pixel whose sum exceeds 2^32 resolves to 65504. A sum just below that is not flagged.
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Budget < 30 s.
- [ ] **Step 5: Commit** `Splat millions of points into the picture in any order`.

---

### Task 11: Participating media — height fog and baked density grids

**Suggested model:** opus (volume integration).

**Files:**
- Create:
  - `src/modern/medium.hpp/.cpp`;
  - `shaders/rt/medium.glsl` (fills the stub);
  - `shaders/modern/medium_common.glsl`, `shaders/modern/medium_majorant.comp`;
  - `shaders/test/medium_constant.comp`;
  - `tests/medium_test.cpp`.
- Modify:
  - `shaders/rt/pathtrace.rgen`: the medium in the bounce loop, NEE transmittance and the volume output;
  - `src/modern/path_tracer.cpp`: bindings 18–20;
  - `src/render/modern_stage.cpp`: bakes before the trace.

**Interfaces:**
```cpp
namespace evo::modern {
struct FogDesc { float density = 0, baseHeight = 0, falloff = 0;   // sigma_t(y) = density * exp(-falloff * (y - baseHeight))
                 Vec3 albedo{.9f, .9f, .9f}; float phaseG = .3f; };
struct GridMediumDesc { std::array<std::uint32_t, 3> size{128, 128, 128}; rt::Mat34 worldToMedium; // to [0,1]^3
                        float densityScale = 1; Vec3 albedo{.8f, .8f, .8f}; float phaseG = .5f;
                        float emissionScale = 1; std::array<float, 16> params; };
struct MediumFrame { std::optional<FogDesc> fog; std::optional<GridMediumDesc> grid; };
class MediumVolume {   // RGBA16F density + emission (3D, storage + sampled), R16F majorant per 8^3 cell (+1 voxel apron)
public:
    MediumVolume(gfx::Context&, gfx::FrameRing&, std::array<std::uint32_t, 3> maxSize, const std::filesystem::path& shader);
    void record(VkCommandBuffer, const GridMediumDesc&, double seconds);  // bake, majorant, barrier -> RT sampled read
    gfx::Image& density(); gfx::Image& majorant();
};
rt::MediumUniforms packMedium(const MediumFrame&);   // 128 B: worldToMedium, bounds, flags, densityScale,
                                                     // albedo, phaseG, emissionScale, fog terms, grid sizes
}
```
```glsl
vec4 mediumVoxel(vec3 uvw, float seconds, vec4 params[4]);   // (density, emission rgb): defined by medium_<scene>.comp
```
**The integrator.** σ_t is the fog plus the grid, and the majorant is the fog's maximum on the segment plus the grid's majorant cell, walked by DDA.

- **Secondary segments:** delta tracking. On each tentative collision:
  - emission is added by the collision estimator, `throughput · σ_a · Le / σ̄`;
  - a scatter takes the probability σ_s/σ̄ and samples Henyey–Greenstein;
  - NEE from the scatter point uses the phase function with MIS.
- **Shadow rays:** the surface occlusion ray times a ratio-tracked transmittance.
- **The primary segment** is estimated twice:
  - ratio tracking gives a continuous transmittance T to the surface hit;
  - one delta-tracking walk estimates the in-scattered radiance, and its scatter path continues normally.

  The tracer writes `volume = (L_v, T)`. The surface path is traced unattenuated, and the combine pass multiplies it by T (Task 7).
- **Disc light** (sun or moon) through fog gives light shafts.

- [ ] **Step 1: Write the failing tests** (`medium_test`, 64×64; grids 32³).
  - (a) A non-scattering homogeneous slab of σ = 1 and thickness 1 in front of an emitter: `volume.a = e⁻¹` ± 2 %.
  - (b) A constant grid equals the homogeneous result within 3 %.
  - (c) An emissive absorbing slab gives `L = Le(1 − e^{−σ_a d})` ± 3 %.
  - (d) Furnace: albedo 1 inside emitting walls of 1 gives a radiance of 1 ± 3 %.
  - (e) NEE through a slab onto a diffuse plane equals the analytic value × e^{−σd} ± 3 %.
  - (f) Exponential fog transmittance matches the closed form ± 2 %.
  - (g) HG: the CPU sampling pdf equals eval and integrates to 1.
  - (h) Two bakes give identical bytes, and the majorant is ≥ every density in its cell (readback).
  - (i) Density 1e4 stays finite with T ≈ 0.
  - (j) A scene without a medium keeps the Task 1 baseline bytes.
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Budget < 60 s.
- [ ] **Step 5: Commit** `Trace fog and baked nebulae with delta tracking`.

---

### Task 12: A light tree for thousands of lights

**Suggested model:** opus.

**Files:**
- Create: `src/rt/light_tree.hpp/.cpp`, `shaders/rt/light_tree.glsl`, `tests/light_tree_test.cpp`.
- Modify:
  - `shaders/rt/lights.glsl`: the `Tree` kind in the importance, pick and pdf functions;
  - `src/modern/path_tracer.cpp`: bindings 16–17, per-slot tree lights;
  - `src/modern/scene.hpp`: `TreeLightFrame`.

**Interfaces:**
```cpp
namespace evo::rt {
struct TreeLight { float center[3]; float radius; float emission[3]; std::uint32_t sphere;   // 48 B
                   std::uint32_t trail, depth, pad0, pad1; };   // trail: the child bits root -> leaf
struct LightTreeNode { float boundsMin[3]; float power; float boundsMax[3]; std::uint32_t child; };  // 32 B;
                                     // leaf: child = light | 0x80000000; inner: children at child, child + 1
struct LightTree { std::vector<LightTreeNode> nodes; std::vector<TreeLight> lights; };
LightTree buildLightTree(std::span<const TreeLight>);      // binary, longest axis, power-weighted median; deterministic
void refitLightTree(LightTree&, std::span<const float> scale);  // per-frame powers (blinking), O(n)
}
// A top-level RectLight of kind Tree stands for the whole tree (its importance from the root's bounds and power);
// tree lights are emissive spheres: light k is sphere firstSphere + k of the instance whose `light` is that Tree
// entry (static spheres, or the first N particles of the dynamic field), so a BSDF hit's tree light is
// gl_PrimitiveID - firstSphere. Moving tree lights: the scene gives their positions per frame from the C++
// mirror of its particle function, and the tree is rebuilt per frame on the CPU (<= 16k lights, < 1 ms).
```
**Traversal.**
- **Node importance:** `power / max(dist²(p, centre), (diag/2)²) · cosBound`, where `cosBound` is the maximum over the 8 AABB corners of `max(dot(n, normalize(c − p)), 0)` (1 when transmissive).
- **Choosing a child:** the left child with probability I_L/(I_L + I_R).
- **Sampling a sphere light:** a uniform cone towards the sphere.
- **`treeLightPdf(k, p, n)`** walks down again along `trail`.

- [ ] **Step 1: Write the failing tests** (`light_tree_test`).
  - (a) CPU: Σ over leaves of the pdf = 1 ± 1e-5 at 20 points, for 1000 lights.
  - (b) The pick histogram matches the pdf (χ² at p > 0.01).
  - (c) The build is deterministic, and refit equals a rebuild's powers.
  - (d) GPU: 256 sphere lights over a plane. NEE-only equals BSDF-only within 3 %. The variance is ≤ 0.5× that of uniform selection.
  - (e) 4096 lights trace at 64×64 and 8 spp in < 10 s.
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Budget < 45 s.
- [ ] **Step 5: Commit** `Sample thousands of lights through a light tree`.

---

### Task 13: Caustic photon maps

**Suggested model:** opus.

**Files:**
- Create:
  - `src/modern/caustics.hpp/.cpp`;
  - `shaders/modern/photons.comp` (ray queries against the frame's TLAS), `shaders/modern/caustic_resolve.comp`;
  - `shaders/rt/caustics.glsl` (fills the stub);
  - `tests/caustics_test.cpp`.
- Modify:
  - `src/modern/path_tracer.*`: a callback after the TLAS build and before the trace dispatches; binding 24;
  - `shaders/rt/{hit.glsl,pathtrace.rgen}`: receivers add the caustic radiance, and paths are excluded so nothing is counted twice.

**Interfaces:**
```cpp
namespace evo::modern {
struct CausticReceiver { rt::Mat34 worldToUv; std::uint32_t resolution = 1024; };   // rows 0-1: uv in [0,1]^2
struct CausticFrame { std::vector<CausticReceiver> receivers;   // <= 2
                      std::uint32_t photons = 1u << 20;         // export: 4x
                      std::int32_t light = -1;                  // a RectLight index, or -1 for the environment's disc
                      std::uint32_t casterInstanceFirst, casterInstanceCount; };   // aim photons at their bounds
class CausticMaps {   // fixed-point R32_UINT x3 per receiver -> RGBA16F irradiance array (binding 24)
public:
    CausticMaps(gfx::Context&, gfx::FrameRing&, std::uint32_t maxResolution);
    void record(VkCommandBuffer, const CausticFrame&, VkAccelerationStructureKHR tlas, const PhotonScene&,
                std::uint32_t seed);
};
}
```

**Photon tracing.**
- **Emission:**
  - From a disc: parallel rays over the casters' bounding sphere, projected.
  - From a rect: points on the rect, with directions aimed at the casters' bounding sphere.
  - Each photon's flux is the light's power on that target divided by N.
- **Path:** a photon must hit a smooth dielectric or metal first, and makes up to 6 delta events. It uses hero wavelengths for `abbe > 0` and carries `spectralRgb`.
- **Landing:** on a `kMaterialCausticReceiver` surface after at least one delta event, it splats bilinearly into 2×2 texels, in fixed point.
- **Seed:** the frame seed, so the map is a pure function of the show time.

**Rendering.**
- A receiver adds `albedo/π · E` to the payload's lighting term.
- The raygen drops any light reached through a delta chain that started at a receiver's diffuse bounce. Without this, those paths would count the caustic twice.

- [ ] **Step 1: Write the failing tests** (`caustics_test`).
  - (a) A glass sphere of radius 1 over a receiver, lit by an overhead disc: the peak irradiance in the focus region is ≥ 10× the direct irradiance. The total caustic flux is ≤ the flux incident on the sphere's cross-section × (1 − R) + 5 %.
  - (b) With `abbe 30`, the red focus ring's radius exceeds the blue ring's.
  - (c) The same seed gives an identical map.
  - (d) Exclusion: with the map disabled, the receiver under the sphere shows no BSDF-found caustic (the mean in the focus ≤ the mean in the shadow ring + 10 %). With the map enabled, the focus is ≥ 5× the shadow ring.
  - (e) A scene without receivers keeps the Task 1 baseline bytes.
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run.** Budget < 50 s.
- [ ] **Step 5: Commit** `Shoot photons through glass so caustics and rainbows land sharp`.

---

### Task 14: Studio polish (art direction)

**Suggested model:** opus (art direction plus wiring).

**Files:**
- Modify:
  - `src/modern/studio.hpp/.cpp`;
  - `src/rt/gpu_types.hpp` + `types.glsl`: `Pattern::CrtGlass = 3`, `Pattern::Concrete = 4`;
  - `shaders/rt/{hit.glsl,screen.glsl,emissive.rchit}`;
  - `tests/studio_test.cpp`.

**Behaviour.** The order follows the review.

1. **A light rig per chapter**, as per-frame `materials` from Task 1, through `StudioRig studioRig(Effect) {lamp, screen}`:

   | Chapters | Lamp | Screen emissionScale |
   |---|---|---|
   | STUDIO NIGHT | off | 3.5 |
   | FINALE, PORTAL | 0.2 | 3.0 (the megamix lights the room) |
   | RETURN, REBOOT | 1.0 | 1.5 |

   The light emissions follow automatically.
2. **Noise at the sources:**
   - a diffuse warm-white inner shade, with the diffuser recessed 3.5 cm;
   - the LEDs flagged `kMaterialCameraOnlyEmission`;
   - `clampIndirect` 16 (Task 6 default);
   - the window portal uses the lighting sky (Task 2).
3. **Composition:**
   - the chrome sphere at (0.22, desk, −0.12), reflecting the screen;
   - the glass sphere between the lamp and the screen;
   - wide shots at eye y ≈ 1.1 and fov ≈ 32°, framed on thirds, with the chair as a foreground silhouette;
   - the moonbeam landing on visible floor;
   - a dim cool fill from a brighter horizon in the window's sky.
4. **The CRT in close-up:**
   - `CrtGlass` bulges the coat's shading normal towards the screen centre (radius 0.5 m) while the quad stays flat for exact NEE (step 4 Decision 10);
   - a grey-green glass base;
   - a 200-line beam profile (a dark gap between lines) instead of darkened alternate rows;
   - an aperture-grille phosphor mask at LOD < 0;
   - the scanlines fade over LOD −0.5…0.5 (review M6).
5. **A modern room** (Decision 13):
   - polished concrete floor (`Concrete`);
   - plaster walls;
   - an oak desk;
   - a slim black window frame;
   - the town as lights with building silhouettes in the NightSky.

   The 1993 PC is then the only 1993 object in the room.

- [ ] **Step 1: Write the failing tests** (`studio_test` additions; 160×90 unless noted).
  - (a) STUDIO NIGHT: the desk patch in front of the monitor is ≥ 40 % brighter with an all-white retro image than with black (step 4: ≥ 10 %).
  - (b) The rig: the lamp material's emission is 0 in STUDIO NIGHT and > 0 in RETURN and REBOOT. The screen light's emission equals the rig's in every chapter.
  - (c) Speckle: at 32 spp in the STUDIO NIGHT wide shot, < 1 % of pixels lie more than 10 levels above their 3×3 median (step 4: 3–5 % at 192 spp).
  - (d) An all-red retro image makes the chrome sphere's projected disc contain ≥ 20 red-dominant pixels.
  - (e) The LED: the desk patch beside it is byte-identical with the LED on and off.
  - (f) Fix round E's CRT sharpness and resize tests still pass.
  - (g) The moonbeam: the floor patch where the analytic moon ray through the window centre lands is ≥ 2× brighter than the floor next to it, in the 80 % wide shot.
- [ ] **Step 2: Fail.**
- [ ] **Step 3: Implement**, then inspect the `build-out/step5/studio/*` stills at 640×360 and 64 spp and tune.
- [ ] **Step 4: Run** `-R "studio|renderer|export_mode|seek"`. Budget < 60 s.
- [ ] **Step 5: Commit** `Light the Studio with its CRT and frame it like a film`.

---

### Task 15: Close step 5a — storyboard, owner checkpoint, docs

**Suggested model:** opus.

**Files:** `README.md` (the new features, `--trace-scale`, `--bench`, `--detail`, the tonemap, what lavapipe cannot show), `docs/plans/HANDOFF.md` (5a status, decisions, RTX checklist additions), and `tests/shared_tracer_test.cpp` (the 4K memory estimate).

- [ ] **Step 1: The memory estimate test.** Sum the image descriptors at 3840×2160 with the fixed set of shared images, plus `SceneGpu::bytes()` of every row in the table (the prepared CPU sides). The total must be ≤ 5 GB. This is computed, not allocated.
- [ ] **Step 2: The full suite** under `xvfb-run -a ctest --preset linux`, plus `windowed` under `EVO_RUNTIME_TESTS=ON` once.
- [ ] **Step 3: The storyboard** at `--spp 8`. Inspect the Studio cells and fix anything broken.
- [ ] **Step 4: The owner checkpoint (the cut line).** HANDOFF lists what the owner runs on the RTX machine:
  - `Evolution --bench --width 2560 --height 1440 --trace-scale 1` and `--width 3840 --height 2160 --trace-scale 0.67` over chapters 9, 10, 11, 19 and 20;
  - the step 4 RTX checklist;
  - one `--capture-dir` at 3840×2160 and `--spp 256` in STUDIO NIGHT, timed, for the TDR question.

  Step 5b starts after the owner's numbers come back, or on the controller's ruling to proceed without them.
- [ ] **Step 5: Commit** `Close step 5a: foundation, Studio polish and the RTX checkpoint`.

---

# ───── Cut line: step 5b starts here ─────

# Step 5b — The 2026 scenes (Tasks 16–26)

**Chapter → scene map** (`kSceneOfEffect`). Each scene task flips its own rows from Placeholder.

| Scene (task) | Chapters | 2026 promise (spec §2.3, §2.4, cards) |
|---|---|---|
| Ship (16) | 1 FLYBY | Path-traced metal and glass ship, volumetric engine trails, stars with flares |
| Particles (17) | 2 DOTS, 16 PARTICLE OCEAN | 2,097,152 particles: cube → sphere → torus knot → logo; a particle sea |
| Nebula (18) | 3 PLASMA, 14 NEBULA, 22 PLASMA PLANET | A ray-marched emissive nebula with scattering; a planet's plasma atmosphere |
| Tunnel (19) | 4 TUNNEL, 23 WORMHOLE | An endless PBR tunnel with light shafts in fog; the cosmic tunnel |
| Glass (20) | 5 GLENZ, 15 GLASS CATHEDRAL, 24 CRYSTAL CITY | Refraction, dispersion, caustics |
| Lens (21) | 6 LENS, 25 LENS OF GOD | An endless reflective chessboard and a rolling glass sphere; at planetary scale |
| Spheres (22) | 7 RAYTRACE, 17 SPHERE STORM, 26 RAYTRACE INFINITY | Dozens to thousands of chrome and glass spheres, soft area lights |
| City (23) | 8 VECTOR CITY, 12 ASCENT, 13 CITY NIGHT, 27 METROPOLIS | A neon city with millions of windows, volumetric fog, wet streets |
| Cosmos (24) | 21 GALAXY, 28 ASCENDANT | 16,777,216 points; the Act III climax |
| Singularity (25) | 18 SINGULARITY | A black hole: accretion disc and gravitational lensing |

That is 23 chapters, the whole placeholder set. The Studio keeps FINALE, PORTAL, STUDIO NIGHT, RETURN and REBOOT.

**What every scene task delivers** (the "standard scene contract"; each task adds only what is specific to it):

- **Code:**
  - `src/modern/scenes/<scene>.hpp/.cpp` with a `ModernScene` built from `SceneDetail`.
  - One authored camera function per chapter: Catmull-Rom with smoothstep, shots authored relative to `chapterDuration` and `accentSeconds`, cameras reading only the camera-safe signals.
  - `camera()` equals `frame().camera`.
  - Its rows in `kSceneOfEffect` and in `sceneTable()`.
  - Its shaders in `shaders/modern/<scene>_*.comp`.
  - Exposure chosen per chapter.
- **Tests:** `tests/scene_<scene>_test.cpp`, registered with `evo_scene_test(<scene>)`, containing:
  - **`checkChapterPictures`** for every chapter of the scene: at 20 %, the accent and 80 %, rendered at 160×90, spp 2 and detail 1/256. Each picture must have:
    - mean display luminance in [0.03, 0.85];
    - < 2 % of pixels at 255 in any channel;
    - < 60 % pure black;
    - more than 50 % of its pixels different from the placeholder's;
    - the accent frame byte-identical when rendered twice.
  - **`checkSceneSweep`** over every 1/60 s of each chapter, on the CPU:
    - cameras finite, and within a shot moving less than the scene's stated maximum per frame;
    - shots increasing in order;
    - instances, lights, materials, particles and splats within their capacities;
    - every transform finite and not mirroring;
    - every light valid against its instance.
  - **A card pin:** where the card names a number, the full-detail configuration produces that number.
  - **One scene-specific physical check** (listed per task).
  - Under 60 s alone.
- **A budget line:** the scene's expected RTX cost against the Global Constraints budget, which the owner verifies with `--bench` in Task 26.
- **A commit.**

## Parallel execution

| Wave | Tasks | Notes |
|---|---|---|
| 6 | **16** ∥ **17** ∥ **18** | Act I's first half; the features are particles, the shutter, splats and media |
| 7 | **19** ∥ **20** ∥ **21** | Dispersion, caustics and fog |
| 8 | **22** ∥ **23** ∥ **24** | Spheres, the light tree, splats at 16.7M |
| 9 | **25**, then **26** | 25 adds the lensing hook's body (`shaders/rt/lensing.glsl`) and is the only 5b task that touches ray generation |

Conflicts are limited to one line per scene in `src/modern/scenes.cpp` and an appended `evo_scene_test` line in `CMakeLists.txt`.

---

### Task 16: Ship — FLYBY

**Look.**
- **The ship:** a sleek procedural starship of about 40k triangles, built from lathe fuselages, beveled wing slabs and intake cylinders.
  - Brushed titanium (`BrushedMetal`), chrome intakes, and dark ceramic panels with emissive panel lines that pulse on `kick` (per-frame materials).
  - A smooth glass canopy over an emissive-lit cockpit (dials as small emitters).
- **Engines:** two emissive nozzles, and a grid medium of 96³ voxels per plume in the ship's frame. The density is a turbulent cone; the emission follows a blackbody ramp along the axis, from white-blue to orange to dark; the frame follows the ship.
- **Environment:** `Space`, with a warm sun disc of 0.6° as the key light and a cool planet limb below (a large sphere with a `Dusk`-lit ocean colour and a fog shell).
- **Stars:** splats, 262,144 at full detail. Each has a four-spike diffraction kernel (spikes as extra Gaussian taps along two axes) that brightens near the sun.
- **Trajectory:** the ship follows the 1993 trajectory (`retro::flybyCamera` / `flybyShipEye`, scaled to metres), so the portal shows the same flight.

**Shots.**
- 0: a chase camera behind and above.
- 1, from the accent: the ship crosses the camera at speed. This is where the export shutter shows motion blur.
- 2: a slow orbit past the canopy, which reflects the sun and the planet.

**Specific check.** In export at spp 8, shutter 1/120, at the crossing: the silhouette's edge spread along the ship's screen velocity is ≥ 2 px wider than with shutter 0. The `volume` image behind the projected nozzles has T < 0.95 and luminance > 0.

**Budget:** trace 6 ms; plume bake 0.3 ms; splats 0.3 ms.

- [ ] **Steps:** write `scene_ship_test` (fails) → implement → run → commit `Fly a path-traced ship on volumetric fire`.

### Task 17: Particles — DOTS, PARTICLE OCEAN

**Look, DOTS.**
- **The particles:** 2,097,152 traced micro-spheres (`kDotsParticles`, radius 2.2 mm in a 1.2 m formation), morphing cube → sphere → torus knot → the "EVOLUTION" logo.
  - The logo targets are the 8×8 font's lit pixels from `text/font8x8.cpp`, extruded 6 voxels deep and filled by jittered sampling.
  - Each morph lands on `retro::dotsSchedule(frame)`'s step times, so 1993 and 2026 change shape on the same beat.
- **Motion:** curl-noise turbulence, its amplitude driven by `bass`.
- **Material:** 92 % polished chrome-white particles and 8 % emissive in the act's palette, flashing on `kick`.
- **Set:** a dark glossy floor that reflects the formation, two large soft rect lights and a rim light. Depth of field is on (aperture 0.02, focus on the formation).

**Look, PARTICLE OCEAN.**
- **The sea:** 2,097,152 spheres on a 1448×1448 grid displaced by a sum of 8 Gerstner waves (amplitude from `energy`). The result is a liquid-metal sea of beads under a `NightSky`, with the moon as a disc of 0.5°.
- **Foam:** crest particles turn emissive cyan on kicks.
- **Camera:** skims the surface, then rises at the accent to reveal the whole sea.

**Specific checks.**
- The particle shader's centres equal a C++ mirror (`dotsParticle(id, t)` and `oceanParticle(id, t)` in the scene's `.cpp`) within 1e-4 for 64 ids at 10 times.
- At each step time of the schedule, every sampled particle lies on its pure target shape: the sphere's radius within 1e-3.
- Under a static camera at a morph, the motion guide is valid on ≥ 90 % of the particle pixels.
- The card says `2,097,152`, which equals the full-detail count, for both chapters.

**Budget:** BLAS build 2.5 ms, trace 8 ms.

- [ ] **Steps:** test (fails) → implement → run → commit `Morph two million traced particles on the 1993 beat`.

### Task 18: Nebula — PLASMA, NEBULA, PLASMA PLANET

**Look.**
- **PLASMA**, the wipe's 2026 half: an emission nebula baked at 192³. The density is domain-warped fBm; the emission hue cycles through the **1993 plasma palette** at the same time (the palette mapped to emission), so the wipe reads as a single image.
  - Embedded hot stars are emissive spheres that light the dust through NEE (albedo 0.65, HG g 0.6).
  - The camera dollies in slowly.
- **NEBULA:** a flight through a larger cloud of pillars.
  - The emission is concentrated on ionisation fronts, where the density gradient faces a star.
  - Background star splats sit behind the cloud.
  - Three shots.
- **PLASMA PLANET:** a planet of radius 60 units with an emissive, animated plasma-ocean pattern.
  - A cloud shell in a spherical-shell grid of 256×128×32, mapped in the medium shader.
  - The sun disc rises over the limb, giving a fog-lit atmosphere rim.
  - The camera is rebased per shot.

**Specific checks.**
- At the nebula's projected centre, T < 0.8 and the `volume` luminance is > 0.
- PLASMA's mean emission hue at 3 times is within 15° of the 1993 palette's mean hue at the same retro frame.
- Two bakes are byte-identical.

**Budget:** bake 1.2 ms, trace 9 ms.

- [ ] **Steps:** test → implement → run → commit `Raymarch a living nebula in the plasma's colours`.

### Task 19: Tunnel — TUNNEL, WORMHOLE

**Look.**
- **TUNNEL:** an endless tunnel of 64 recycled ring segments placed along `retro::tunnelPath` with `retro::tunnelTwist`, so the camera rides the 1993 curve.
  - The panels alternate brushed steel and glossy black ceramic.
  - Emissive strip lights run along the segments, each a rect light within the 64-light budget, pulsing on `kick`.
  - Slit windows let a strong exterior disc through uniform fog (σ 0.03), giving light shafts.
  - The tunnel reflects into infinity.
- **WORMHOLE:** the cosmic version.
  - Glass and energy rings 1 km apart (rebased per shot) curve into a nebula medium of its own (128³).
  - The camera accelerates (shutter blur) towards a white-out at the end of the chapter.

**Specific checks.**
- The CPU sweep keeps the camera within 0.7 × the tunnel radius of `tunnelPath`.
- No more than 64 lights in any frame.
- The `volume` luminance along a projected shaft is ≥ 1.5× that beside it.

**Budget:** trace 8 ms, fog included.

- [ ] **Steps:** test → implement → run → commit `Ride an endless path-traced tunnel through shafts of light`.

### Task 20: Glass — GLENZ, GLASS CATHEDRAL, CRYSTAL CITY

**Look.**
- **GLENZ:** the credited Future Crew glenz mesh (`retro::glenzMesh`, flat-faceted) as dispersive flint glass (IOR 1.62, Abbe 30), with an inner core of tinted glass scaled 0.82 (the 1993 two-colour glenz).
  - It uses the same rotation and `glenzBounce` as 1993 at the same time.
  - It bounces over a white cyclorama, a caustic receiver, under a small strong area light. The caustic map throws **rainbow caustics** on the floor.
  - The 1993 copper bars become emissive horizontal strips behind it.
- **GLASS CATHEDRAL:** a nave of instanced glenz-faceted pillars and gothic arches (lathe and box builders).
  - Stained-glass windows are thin-walled coloured glass (`kMaterialThinWalled`).
  - A sun disc and height fog give coloured shafts across the nave.
  - The camera dollies down the aisle.
- **CRYSTAL CITY:** about 400 instanced hexagonal prisms of dispersive glass on a `retro::cityLot` grid, at sunrise (`Dusk`, low sun).
  - The ground is a caustic receiver.
  - The camera flies between the towers.

**Specific checks.**
- GLENZ at the accent: the floor's caustic region contains pixels with r > 1.3b and pixels with b > 1.3r within 6 px of each other. With `abbe = 0` neither appears.
- The glenz vertices equal `retro::glenzWorldVertices` at the same time, after the fixed scale (CPU).
- The cathedral's shafts: `volume` luminance on the shaft is ≥ 1.5× that off it.

**Budget:** photons 1.5 ms, trace 9 ms.

- [ ] **Steps:** test → implement → run → commit `Turn the glenz into real glass that throws rainbows`.

### Task 21: Lens — LENS, LENS OF GOD

**Look.**
- **LENS:** an endless chessboard of black granite and white marble (`Checker` class, roughness 0.04) that reflects to the horizon under a `Day` sky with a sun disc of 0.5°.
  - A 1 m dispersive glass sphere rolls across it, magnifying the board (the 1993 lens).
  - Its caustic focus burns a bright spot on the board, which is a receiver.
  - The camera tracks low beside the sphere. The wipe matches the 1993 rotozoom's rotation rate.
- **LENS OF GOD:** a colossal lens 200 units across hovering over an endless chess planet.
  - The curved horizon comes from a large sphere floor with spherical checker coordinates.
  - The sun is focused into a white-hot point on the board, with clouds in height fog.
  - The camera descends from orbit to the focus (rebased per shot).

**Specific check.** LENS with the sun high: the board under the sphere has a spot ≥ 3× brighter than the shadow ring around it (export, spp 4, 160×90).

**Budget:** photons 1 ms, trace 7 ms.

- [ ] **Steps:** test → implement → run → commit `Roll a glass sphere over an endless chessboard`.

### Task 22: Spheres — RAYTRACE, SPHERE STORM, RAYTRACE INFINITY

**Look.**
- **RAYTRACE**, the climax and the drop: the 1993 composition (chrome balls on a chessboard) path traced in real time.
  - 48 analytic spheres, a mix of chrome, coloured glass and gold, under two large soft rect lights and a `Dusk` dome. They bounce on the beat.
  - Multi-bounce reflections, and caustics through the glass balls (board receiver).
- **SPHERE STORM:** 32,768 spheres in a particle field orbiting a vortex driven by `energy` and `kick`.
  - A mix of chrome and emissive. The emissive spheres are the field's first 4096 particles and are tree lights (Task 12), with the tree rebuilt each frame from the C++ mirror of the particle function.
  - Shutter motion blur.
- **RAYTRACE INFINITY:** a lattice of chrome spheres repeating to infinity, as 4096 sphere instances recycled around the camera with fog fading them to the horizon.
  - The camera falls through the lattice.

**Specific checks.**
- RAYTRACE: a chrome sphere's projected disc reflects the board, showing both checker colours inside the disc.
- SPHERE STORM: tree-light selection is active (`treeLightCount > 0`), and the frame is finite with 4096 emissive spheres.

**Budget:** trace 9 ms; build 1 ms.

- [ ] **Steps:** test → implement → run → commit `Path trace the 1993 raytrace dream in real time`.

### Task 23: City — VECTOR CITY, ASCENT, CITY NIGHT, METROPOLIS

**Look.**
- **VECTOR CITY:** the 1993 city's layout (`retro::cityLot`, so the wipe shows the same streets) rebuilt as a neon city.
  - Towers are instanced bevel boxes with a procedural `Windows` pattern: emissive window cells with hashed on/off states and warm/cold tints that blink by a pure function of time. That is millions of windows, at no geometry cost.
  - Wet asphalt is a coat over dark diffuse, with puddle roughness noise.
  - Neon signs are rect lights; 4096 street lamps are tree lights.
  - Height fog lights up around the lamps.
- **ASCENT:** the camera leaves through the Studio's window (continuity for step 6) and rises over the city through a cloud layer (a grid medium) lit by the moon disc, giving shafts. It ends above the clouds under the stars.
- **CITY NIGHT:** a street-level journey with rain.
  - Rain streaks are splats, motion-stretched along the camera velocity.
  - Reflections stretch in the wet street.
- **METROPOLIS:** a megacity to the horizon, tiled lots instanced up to 200k instances (TLAS capacity per scene), with 16k tree lights.
  - The camera flies along an elevated avenue at speed, with shutter blur.

**Specific checks.**
- The wet street: a pixel below a neon sign's reflection point is ≥ 2× brighter than the dry asphalt beside it.
- Tree lights are active in CITY NIGHT and METROPOLIS.
- The windows' blink state is identical at the same time reached by two seek paths.
- The instance capacity holds over the sweep.

**Budget:** trace 9.5 ms; tree refit on the CPU < 1 ms.

- [ ] **Steps:** test → implement → run → commit `Light a wet neon city with thousands of lamps`.

### Task 24: Cosmos — GALAXY, ASCENDANT

**Look.**
- **GALAXY:** 16,777,216 splat stars (`kGalaxyPoints`, matching the card) in a logarithmic two-arm spiral with a bulge.
  - Stellar colours come from a temperature distribution.
  - A grid dust medium of 256×64×256 in the disc plane forms dark lanes and pink HII emission knots.
  - The camera swings from edge-on to face-on.
- **ASCENDANT**, the climax of Act III: 2,097,152 traced particles rise out of the galaxy's core and assemble the "EVOLUTION" logo (Task 17's targets reused as a shared header, `modern/scenes/logo_targets.hpp`, created in Task 17), lit by a disc.
  - On the final accent the logo shatters into light (emissive ramp), handing over to the credits.

**Specific checks.**
- The full-detail splat count equals `16,777,216`.
- The dust lanes: `volume` T < 0.9 on the lane's projection.
- ASCENDANT's particles lie on the logo targets at the assembly time (CPU mirror).

**Budget:** splats 2 ms, bake 0.8 ms, trace 7 ms.

- [ ] **Steps:** test → implement → run → commit `Splat a sixteen-million-star galaxy and rise out of it`.

### Task 25: Singularity — SINGULARITY

**Files** beyond the contract: `shaders/rt/lensing.glsl` (fills the hook), `src/modern/scenes/singularity.*` (`LensingFrame`, packed as `LensingUniforms`, 32 B: centre, mass radius r_s, influence radius, steps).

**Look.**
- **Lensing:** a Schwarzschild black hole of r_s = 1 unit. Inside the influence sphere (radius 30 r_s), rays are bent piecewise: up to 48 RK4 steps of the null geodesic in the hole's plane. Each step is a straight `traceRayEXT` segment; a ray reaching r < 1.01 r_s returns black.
- **The accretion disc:** a grid medium in disc coordinates (Doppler-boosted blackbody emission, with turbulence on `bass`), so the far side of the disc appears lensed over the hole.
- **Background:** galaxy splats.
- **The camera** orbits inward at the accent.

**Specific checks** against the analytic result:
- A background star straight behind the hole shows as an Einstein ring at the analytic angular radius ± 1 px (64×64, deflection α ≈ 2r_s/b).
- A ray with impact parameter b < 2.6 r_s ends black.
- With lensing off, the images equal the unbent ones.

**Budget:** trace 11 ms (the most expensive scene; the owner may set `--trace-scale` 0.6 here).

- [ ] **Steps:** test → implement → run → commit `Bend light around a black hole`.

### Task 26: Close step 5 — test budgets, whole-show sweep, storyboard, docs

**Files:** `tests/stream_capture_test.py` and the CMake parts, `tests/seek_determinism_test.cpp`, `tools/storyboard.py`, `README.md`, `docs/plans/HANDOFF.md`.

- [ ] **Step 1: Per-scene budgets.**
  - `stream_capture` traced parts become `stream_capture_act1`, `_act2` and `_act3`. Each compares one mid-chapter frame per traced chapter at 96×54, `--spp 1 --detail 0.004`, with `PROCESSORS 2` and TIMEOUT 600.
  - `seek_determinism` gains one seek per scene: 3 frames at 64×36, from a fresh renderer against a seeking one.
- [ ] **Step 2: A whole-show sweep.** `--render-stream --width 32 --height 18 --fps 1 --spp 1 --detail 0.004 --duration 711.16 --validation` must exit 0 with zero messages. This is a CTest `whole_show` behind `EVO_LONG_TESTS`, and it is run once by hand here.
- [ ] **Step 3: The storyboard** at `--spp 4 --detail 0.05`. Inspect every 2026 cell, and fix what is broken in the owning scene.
- [ ] **Step 4: The full suite and the docs.**
  - README: the scenes, and the features that each 2026 card names.
  - HANDOFF: the step 5 table, the decisions, deferred items, and the owner's `--bench` run over all 31 chapters at 2560×1440, with the result compared against the per-scene budgets.
- [ ] **Step 5: Commit** `Close step 5: every 2026 frame is a path-traced scene`.

---

## Decisions the controller must confirm (defaults used above)

1. **Split into 5a and 5b** with the owner's RTX `--bench` checkpoint at the cut line (Task 15). Default: yes; proceed to 5b without numbers only on a ruling.
2. **Tonemap:** Khronos PBR Neutral, which has an analytic inverse for the placeholder and keeps exact hue below 0.76. The alternative is AgX, which has more filmic highlights.
3. **Particles:** traced analytic spheres in a per-frame BLAS for DOTS, OCEAN, SPHERE STORM and ASCENDANT; compute splats for GALAXY's 16.7M and for star fields. This replaces the spec's §3.5 "additive points/quads with dynamic rendering": compute splats stay deterministic, and traced particles cast shadows and reflect.
4. **Integer atomics** into fixed-point accumulators are allowed (splats, photons). They are order-independent and therefore deterministic. Float atomics stay forbidden.
5. **Volume composition:** a separate `volume` layer (radiance, T) with its own temporal filter. `hdr = T·surface + volume`.
6. **Motion blur:** an export shutter of 180° (`0.5/fps`) in up to 8 time slices (a TLAS and particle BLAS per slice). The window has none. The alternative is a post-process blur in step 6.
7. **Trace scale:** window default 0.67 with the temporal upscaler; exports trace at native resolution, scale 1. At scales below 1 the CRT text in the window is upscaled (slightly softer).
8. **Reset-frame spp** in the window: 4, then 2, then the configured spp. Review Focus 6 (step 4) then compares the first window frame with an export at spp 4.
9. **Per-frame material tables** (`SceneFrame::materials`) rather than per-instance variants, for light rigs and blinking.
10. **`Material` grows to 96 bytes** (flags, abbe, medium, pad), and the ABI is fixed in Task 1 so later tasks do not collide.
11. **One trace per frame stays.** If step 6 needs Studio + X in one frame, it builds a combined `SceneStatic` on the CPU (a new table row), with no second G-buffer.
12. **The chapter → scene map** above, notably ASCENT → City, WORMHOLE → Tunnel, CRYSTAL CITY → Glass, ASCENDANT → Cosmos.
13. **A modern Studio room** (concrete floor, plaster, oak) instead of the step 4 linoleum checker; the checker returns in LENS and RAYTRACE.
14. **A light tree over emissive sphere lights** (≤ 65,536) instead of ReSTIR DI. It is deterministic and has no temporal reuse.
15. **Gravitational lensing** by piecewise bent rays in SINGULARITY. It is the only 5b change to the raygen, through Task 1's hook.
16. **An export firefly filter at spp ≥ 16** (k = 4, against the second-largest neighbour). It is on by default and the README states it.
17. **Scene detail:** scenes take `SceneDetail`; tests run at 1/256 or 0.004, and card numbers are pinned against full-detail configuration. Cards computed at run time remain step 6.
18. **Splitting a 4K export frame across submits (TDR)** stays an RTX-checklist measurement (Task 15 Step 4). It is implemented in step 8 only if the owner's run shows trouble.
19. **`clampIndirect` 16 after a rough vertex only** (the Studio's step 4 value of 2 is retired), plus path regularisation to roughness 0.25.
20. **Caustics** come from photon maps on flagged receivers, with the double-counting paths excluded in the raygen. This is biased but deterministic, and is the only way sharp dispersive caustics fit in a frame.

## Self-review notes

**Spec coverage:**
- §7 step 5 list:
  - particles (compute): Tasks 8, 10, 17, 24;
  - the nebula (volume): Tasks 11, 18;
  - the tunnel: Task 19;
  - the glass glenz: Tasks 6, 13, 20;
  - the lens and chessboard: Task 21;
  - the spheres: Task 22;
  - the city: Tasks 12, 23;
  - the ship: Task 16.
- "Alla 2026-rutor": the map covers all 23 placeholder chapters, and Task 26's storyboard and stream tests check them.
- §3.3: the frame graph's particles, volume and accumulate passes are Tasks 8, 10, 11 and 7.
- §3.4: Tasks 1, 6, 9.
- §3.5: stateless particles and splats, Tasks 8 and 10.
- §3.7: determinism in the Global Constraints and in Tasks 9, 10, 11 and 26.
- §3.8: exports at 4K: the shutter in Task 9, budgets.
- §6: storyboards and lavapipe budgets.
- §8: denoise ghosting is addressed by motion vectors (Task 9) and reset frames (Task 7).

**HANDOFF step 5 advice:**

| Item | Task |
|---|---|
| Shared tracer and denoiser, scene table | 1 |
| Environment, sun and moon sampling | 2 |
| Trace resolution and upscaling | 4 |
| Dynamic geometry | 8 |
| More instances | 1 (capacities) |
| Volumes and dispersion | 11 and 6 |
| Motion vectors | 9 |
| Light rig and material variants | 1 and 14 |
| Per-point selection and the light BVH | 3 and 12 |
| Blotchy first frames and export grain | 7 |
| Test budgets | 1 and 26 |
| Performance and memory | Global Constraints, 4, 15 |

**Studio review:**

| Item | Task |
|---|---|
| CRT as the key light | 14 |
| Regularisation and clamping after a rough vertex | 6 |
| Recessed diffuser | 14 |
| Smooth lighting sky | 2 |
| Camera-only LEDs | 6 and 14 |
| PBR Neutral with a CPU reference | 5 |
| Composition | 14 |
| CRT close-ups | 14 |
| Filtered analytic stars and town | 2 |
| Per-point selection with the screen's 1×1 mip | 3 |
| M3 `shotFor` | 1 |
| M6 scanline aliasing | 14 |

**Deliberately not in step 5:**
- the η² transmission term (it cancels for closed glass, and no camera sits inside glass);
- ReSTIR, ML denoising (spec: none in v1), bloom and grading, portal camera paths, cards computed at run time (all step 6);
- per-slot readback and the TDR submit split (step 8).

**Names used across tasks:**
- Task 1: `SceneId`, `SceneDetail`, `SceneCapacity`, `SceneEntry`, `sceneTable`, `sceneFor`, `sceneName`, `SceneFactory`, `SceneGpu`, `TracerCapacity`, `TraceExtras`, `PathTracer::reserve`, `ModernStage::prepare/prepareAll/gpuBytes/tracer`, `GBuffer::volume`, `evo_gpu_test`, `evo_scene_test`, `checkChapterPictures`, `checkSceneSweep`.
- Task 2: `EnvironmentKind`, `EnvironmentDesc`, `environmentDefaults`, `packEnvironment`, `environmentView`, `environmentLighting`, `sampleEnvironment`, `environmentPdf`.
- Task 3: `lightImportance`, `pickLight`, `lightSelectPdf`.
- Task 4: `Upscaler`, `UpscaleSettings`, `GpuTimers`.
- Task 6: `spectralRgb`, `regularizeRoughness`.
- Task 7: `DenoiseSettings::{resetAtrousIterations, fireflyFilter, fireflyK, volumeAtrousIterations}`, `kResetSpp`.
- Task 8: `GeometryKind`, `hitGroupFor`, `SphereRecord`, `SphereBlas`, `ParticleField`, `ParticleFrame`.
- Task 9: `shutterSlices`, `sliceOffset`, `kMaxShutterSlices`.
- Task 10: `SplatFrame`, `SplatPass`, `splatPoint`.
- Task 11: `FogDesc`, `GridMediumDesc`, `MediumFrame`, `MediumVolume`, `packMedium`, `mediumVoxel`.
- Task 12: `TreeLight`, `LightTreeNode`, `LightTree`, `buildLightTree`, `refitLightTree`.
- Task 13: `CausticReceiver`, `CausticFrame`, `CausticMaps`.
- Task 14: `StudioRig`, `studioRig`.
- Task 25: `LensingFrame`.

The ABI field names are fixed in Task 1 and only used afterwards.

**Review Focus coverage:**
- 1 → Tasks 4 (b), 9 (g), 11 (h), 26 Step 1.
- 2 → Tasks 3 (c), 12 (d).
- 3 → Tasks 4 (a), 7 (b), 9 (f, g).
- 4 → `checkSceneSweep` in every scene task, Task 26 Step 2.
- 5 → Tasks 1 (b), 15 Step 1.

**Risky for lavapipe time:**
- Tasks 11 and 13 (volumes and photons): they use 32³ grids and 2^14 photons in tests.
- The 5b scene tests: about 9–12 renders at 160×90 and spp 2. Resolution drops before spp does, as in step 4.
- Task 26's act-split stream tests.

## Controller rulings (2026-10-24)

All twenty decisions are accepted as proposed (the plan's defaults), with one change: Decision 1 keeps the 5a/5b cut line and the owner's RTX `--bench` checkpoint, but the checkpoint does **not** block step 5b. Work continues straight into 5b; the owner runs `--bench` (and the RTX checklist in HANDOFF.md) when the machine is available, and any findings are folded into a later fix wave. Tonemap: Khronos PBR Neutral.
