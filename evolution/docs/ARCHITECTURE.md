# EVOLUTION architecture

EVOLUTION is a standalone Vulkan 1.3 demo under `evolution/`: every chapter of a 682 s, three-act show first
appears as a 1993 effect (320x200, palette-indexed, rasterised on the GPU) and then as a 2026 path-traced scene.
This document covers the build, the module layout, the render pipeline, the determinism rules and the checks
that need RT hardware. The README has the user-facing description, flags and keys.

## Build and test

```sh
cd evolution
cmake --preset linux && cmake --build --preset linux && ctest --preset linux --output-on-failure
```

- Dependencies (SDL3, Vulkan-Headers, volk, VulkanMemoryAllocator) are fetched with git at configure time, each
  pinned to a commit. Shaders are GLSL, compiled to SPIR-V at build time with `glslangValidator`.
- Tests run headless on Mesa lavapipe (Vulkan 1.4 in software with ray query, RT pipeline and mesh shaders):
  `apt-get install mesa-vulkan-drivers vulkan-tools glslang-tools spirv-tools` plus the SDL3 build packages
  listed in the README. The `linux` test preset runs 8 tests at a time; the whole suite takes about two minutes.
  `windowed`, `windowed_unsupported` and `silent_mode` are behind `-DEVO_RUNTIME_TESTS=ON` and run under
  `xvfb-run`.
- Every GPU test runs with the validation layer on and fails on any message. Synchronisation validation is on in
  the `accel`, `path_tracer`, `denoiser`, `image` and `descriptors` tests only; the layer does not credit host
  waits on the timeline semaphore, so tests keep buffers alive or idle the queue between subtests.
- Music files are not committed. `python3 tools/prepare_show.py <track>.mp3 ...` writes `assets/music/show.wav`,
  the generated headers in `src/show/generated/` and the receipts in `music/`. Without `show.wav` the demo runs
  in SILENT MODE. Act III's chapters are placed on the form of its track (`tools/song_form.py`), and the code
  and tests take Act III's times from the generated data, so swapping its track is that one command and a test
  run (`music/README.md`, "Replacing a track") plus a baseline regeneration.
- `tests/baseline/hash_outputs.py` renders a fixed set of captures and streams and compares SHA-256 sums
  (`SUMS`) and Studio luminance (`LUMINANCE`) on lavapipe; use `--check tests/baseline/SUMS` for changes that must
  not move a pixel.
- Extra worktrees configure in seconds by pointing FetchContent at an existing build's sources
  (`-DFETCHCONTENT_SOURCE_DIR_SDL3=<build>/_deps/sdl3-src`, likewise `VOLK`, `VULKANHEADERS`,
  `VULKANMEMORYALLOCATOR`) and symlinking `assets/music/*.wav` before configuring.

## Modules (`src/`)

| Directory | Library | Role |
|---|---|---|
| `app/` | `evo_app` | CLI options, window loop, capture and `--render-stream` export, `--bench`, show clock |
| `audio/` | `evo_audio` | WAV reader, sample-exact transport, SDL3 audio player |
| `show/` | `evo_show` | Timeline: 31 chapters, cue sheet, era curve, accent, beat grid; generated data headers |
| `gfx/` | `evo_gfx` | Vulkan context, frame ring with a timeline semaphore, images with layout tracking, buffers, descriptors, compute and RT pipelines, SBT, GPU timers, swapchain |
| `image/`, `text/` | | PNG reader/writer; 8x8 font and text pass |
| `retro/` | `evo_retro` | The 1993 effects: a registry of pure functions of the retro frame (BOOT, PLASMA, FLYBY, DOTS, TUNNEL, GLENZ, ROTOZOOM/LENS, RAYTRACE, VECTOR CITY, FINALE, PORTAL, REBOOT, THE END, scrollers) |
| `rt/` | `evo_rt` | Geometry store, compacted BLAS, per-frame TLAS, analytic spheres and particle fields, GPU structs shared with GLSL, binding numbers |
| `modern/` | `evo_modern` | Path tracer, scene table and per-scene GPU state, environments, light selection, the Studio, splats |
| `post/` | `evo_post` | Denoiser (temporal accumulation and a-trous), temporal upscaler, tonemap |
| `render/` | `evo_render` | Renderer, stage plan, retro stage, modern stage, compositor, scene rasteriser |

`shaders/` mirrors this: `retro/` (compute, one invocation per pixel), `rt/` (ray generation, closest hit per
material class, miss, intersection), `post/`, `modern/` (scene compute passes), `common/`, `test/` (fixtures
compiled only for tests).

## Render pipeline

Each frame is a pure function of show time. `Renderer` asks `render::planStages` which stages the frame shows and
runs only those:

1. **Retro stage.** The active 1993 effect writes a 320x200 palette-index image at its own frame rate (a
   `retroFrame` count, e.g. 35 fps for the scroller). Triangles and points are transformed in double precision on
   the CPU (clipped at z >= 0.1 and a 512 px guard band, 28.4 fixed point) and rasterised by `retro/polys.comp`
   and `points.comp`. Palettes are built and cycled on the CPU. The result is mapped to RGBA8 (`R8G8B8A8_SRGB`
   with a UNORM storage view) and mip-mapped in linear light.
2. **Modern stage.** The shared `PathTracer` traces the chapter's scene at `traceSize(width, height, traceScale)`:
   one raygen loops over bounces with next-event estimation (rectangle lights, the CRT screen, portals, sun/moon
   discs, a light tree for many lights) and MIS, Russian roulette, indirect clamping and ray-cone LOD. Closest-hit
   shaders exist per material class (PBR, glass, emissive, checker, plus sphere variants). Recursion depth is 1;
   `RayTracingPipeline` rejects SPIR-V that would exceed it. The shared `Denoiser` demodulates, reprojects,
   accumulates (with edge-robust variance) and runs a-trous passes; a `post::Upscaler` resolves the trace size to
   the output size in time. HDR output (`RGBA16F`) goes through the PBR Neutral tonemap with a static dither.
   Particles (traced spheres in a per-frame BLAS), emissive splats and participating media are separate passes
   recorded by `ModernStage`.
3. **Compositor.** Combines the two eras according to the chapter's transition (Cut, Portal, BeatSwap, Wipe),
   draws the CRT look, the HUD and the `eraCut` flash, and writes the swapchain image or the export readback.
   A stage that is not visible is not run, and its stale image never reaches the output.

The Studio (a 1993 PC in a dark room, 480 instances) is lit by its own CRT. The retro image is the screen's
emissive texture, so the 1993 picture lights the 2026 room.

## Determinism

- Every `TimelineFrame` field is a pure function of show time and the generated data; two seek histories that reach
  the same time give bit-identical frames.
- The renderer derives a `HistoryState` {reset, framesSinceReset, mode, spp, maxBounces, frameSeed} and the modern
  stage never recomputes the seed. Export uses `frameSeedFor(t, 0)`; the window uses
  `frameSeedFor(t, framesSinceReset)`, so the first window frame after a reset equals an export frame at equal spp
  and bounces (with `--no-fast-mode`: the window's fast mode, docs/PERFORMANCE.md, roulettes faint shadow
  rays and paths and keeps its photon maps, and is still a pure function of the time and the frames since a reset). Every random number is an integer hash of (frame seed, pixel, sample, dimension).
- History resets on seeks, chapter changes and cuts. The reset is guarded twice (the denoiser's `resetHistory()` and
  withholding the previous camera); the seek tests fail only when both are removed.
- Export (`--capture-dir`, `--render-stream`) has no temporal history: it traces `--spp` samples per frame (64 by
  default) with 6 bounces, in dispatches of 4 samples, so one 4K frame at 64 spp is 16 dispatches in one submit.
  The window uses 1 spp and 4 bounces.
- `EVO_FORCE_ERA=0|1` forces one era in captures and streams (never in the window); storyboards use it.
- Determinism holds within one build on one machine: libm and GPU float maths may differ in the last bit between
  platforms or vendors, so a pixel on a rounding edge can differ.

## Design rules for scenes and lights

- A light is derived from its material's emission; validation throws if they disagree.
- No coplanar emitters and walls (closest-hit ties are a determinism risk on RTX).
- No glass in front of a textured emitter or inside a portal.
- Use the same UV warp on the hit and on the next-event side.
- Only one `trace()` and one TLAS build are allowed per frame.
- The shared GPU structs (`src/rt/gpu_types.hpp`) mirror `shaders/rt/types.glsl`; sizes and `offsetof` are pinned
  in C++ and read back on the GPU by `material_test`. Later features fill reserved fields without moving any.

## Performance targets

docs/PERFORMANCE.md has the RTX 4080 measurements, where the window's time goes, the window's fast mode and
adaptive trace scale, and how to measure on the RTX (`--bench-counters`, Nsight Graphics).

Estimates for an RTX 4090, to be confirmed with `--bench`: a 2560x1440 window frame traced at scale 1 with 1 spp
and 4 bounces should take at most 14 ms on the GPU (trace at most 9 ms; denoise, upscale and tonemap at most
2 ms), a 3840x2160 export frame at 64 spp at most 2.5 s, and VRAM with every scene prepared at 4K at most 5 GB.

## RTX hardware checklist

None of this can be checked on lavapipe.

- Run the whole `ctest` suite with a current validation layer and synchronisation validation: no messages beyond
  the two provoked on purpose. Then mutate the barriers (remove the TLAS barriers in `Tlas::build`, the barrier
  between trace dispatches, the per-level barrier in `generateMips`); each mutation must be caught.
- Shader binding table with padded layout (handle 32, alignment 32, base 64; some GPUs use base 128): the
  `rt_pipeline` probe and the path tracer must agree. The probe with overridden properties (base 128) is not
  written yet; the table is only tested on the CPU.
- Scratch alignment of 128 (`minAccelerationStructureScratchOffsetAlignment`); the constructors throw on a
  misaligned address.
- BLAS compaction really shrinks (lavapipe: 1480 to 1480 bytes); `accel_test` compares compacted against
  uncompacted probes with `memcmp`, which may need a 1e-4 tolerance there.
- Facing: `frontFace` and `gl_HitKindEXT` as on lavapipe (outward-facing CCW seen from outside is front); one-sided
  lights; instances seen from behind.
- `SRGB` + `STORAGE` with `EXTENDED_USAGE` for the retro image (the format probe must succeed); in `path_tracer`
  case (h), byte 128 reads as 0.4331 at scale 2 through the sRGB view.
- Finiteness checks (`shaders/common/finite.glsl`) must survive the driver's fast-math; the stress scene stays
  finite.
- Determinism: the same seed gives the same bytes across runs and fresh tracers (BVH build, closest-hit ties) in
  `stream_capture_studio`, `export_mode` and `seek_determinism`; the tonemap matches its CPU reference within +-1.
- `maxShaderGroupStride` (4096 on RTX) and pipeline creation at recursion depth 1.
- `accel_test` prints `warning: synchronization validation unavailable` rather than `SKIP sync validation` and has
  no `SKIP_REGULAR_EXPRESSION`, unlike the other synchronisation tests.
- **Windows GPU timeout (TDR) at 4K.** No single dispatch traces more than 4 spp (estimated 0.06-0.15 s at 4K on a
  4090), and the exports submit a frame per shutter slice (`FrameRing::split` before each slice's TLAS build):
  at 64 spp 8 submits of 2 dispatches, about 0.1-0.5 s each (unmeasured). Run `--capture-dir` at 3840x2160 with
  `--spp 256` in STUDIO NIGHT (t about 205) and time it; if the driver still resets, raise `TdrDelay` (README,
  "Release and the 4K video").
- **Windows and audio.** `cmake --preset windows`, `ctest --preset windows`, and listen with headphones (act joins,
  A/V offset with `--audio-offset`, unplugging the device).
- **`--bench`** measures the window's rendering without a window. From the build directory:

  ```sh
  Evolution --bench --width 2560 --height 1440 > bench-1440p.jsonl
  Evolution --bench --width 2560 --height 1440 --trace-scale 1 > bench-1440p-native.jsonl
  Evolution --bench --chapters 11,19 --bench-frames 60
  ```

  Each output line is one JSON object per chapter with `gpuMs` per pass (`tlas`, `trace`, `denoise`, `upscale`,
  `tonemap`, `compositor`, `retro`, `fill`, `total`), sizes, `traceScale`, `spp`, `device` and `fps`.
  `"timestamps": false` means the queue has no timestamps (times are 0). Compare against the targets above. Also
  look at the window at 2560x1440 (and with `--trace-scale 1`): the CRT picture should be sharp and current, and
  detail should sharpen within about 16 frames of a cut without ghosts behind moving edges.

## Known open items

- Blotchy first window frames after a history reset at 1 spp (dark holes in lit beige plastic that fade in about
  ten frames); export at 16 spp or more filters lightly (2 à-trous iterations), so the darkest corners can keep
  a little grain at the default 64 spp.
- Single-scatter GGX loses energy in rough metals; glass is tinted by base colour at every pass and transmission
  lacks an eta-squared term; the glass sphere's caustic is weak (photon maps are still to come).
- Below 2 fps on lavapipe every window frame is a time jump, so accumulation never builds up.
- `polys.comp` has no tile binning (worst case 2048 large triangles is untimed; the busiest effect uses about 930).
- THE END: the epoch card stays white while the text fades.
- Exports use `ring.wait(ring.submit())` per frame, which caps 1993 and placeholder chapters at 20-30 fps at 4K;
  per-slot readback and GPU RGB packing are planned.
- The export frame grid: 737.62 s is not a whole number of 60 fps frames, so the audio must be padded to the grid.
- `tools/render_video.py` does not exist yet; a final render is `--render-stream` piped to ffmpeg.
- Small items: `Image` does not expose its usage; `lathe` throws on repeated profile points; a firefly can spread
  through the a-trous filter before remodulation (finite and clamped); `writeGuides` traces one extra unjittered
  primary ray per frame (about 10 % more rays at 1 spp); `WAVE_FORMAT_EXTENSIBLE` WAVs are rejected.
