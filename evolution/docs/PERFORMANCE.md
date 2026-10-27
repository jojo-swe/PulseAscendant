# EVOLUTION performance: the real-time window

The window (and `--bench`, which renders exactly what the window renders) path traces every 2026 chapter at
1 spp per frame, accumulates in time, denoises and upscales. On an RTX 4080 at 2560x1440 (traced at
1715x965, trace scale 0.67) the chapters ran at 1.3 to 7.6 fps, almost all of it in the path tracing step.
The target: at least 30 fps in every chapter at 1280x720 (`Evolution --bench --width 1280 --height 720`),
then as far towards 60 fps at 720p and at 1440p as possible. This document records where the time goes,
what was changed, what it is expected to buy (with a per-chapter estimate against 30 fps at 720p), and how
to measure it on the RTX.

None of the numbers below were measured on an RTX GPU: the only Vulkan driver here is Mesa lavapipe (ray
tracing in software on 4 CPU cores). Lavapipe's GPU timestamps are used for relative before/after numbers
per pass; its absolute times are about 1000 times an RTX GPU's, and its costs are distributed differently
(software traversal is expensive, ALU work cheap, memory latency well hidden by caches, occupancy and
register pressure do not exist). The work counters (rays, next-event estimates, medium steps per pixel) are
the same on every GPU.

## Measured on an RTX 4080 (Windows, Release)

`Evolution.exe --bench --width 2560 --height 1440` (trace 1715x965):

| Chapter | fps | ms per frame |
|---|---|---|
| GLASS CATHEDRAL | 1.3 | 769 |
| CITY NIGHT | 1.8 | 556 |
| ASCENT | 1.9 | 526 |
| VECTOR CITY | 2.4 | 417 |
| TUNNEL | 3.0 | 333 |
| METROPOLIS | 3.4 | 294 |
| RAYTRACE | 3.5 | 286 |
| PARTICLE OCEAN | 3.8 | 263 |
| RAYTRACE INFINITY | 4.5 | 222 |
| LENS OF GOD | 4.7 | 213 |
| FINALE | 5.2 | 192 |
| NEBULA | 6.9 | 145 |
| PLASMA | 7.6 | 132 |

At 1.65 Mpx a frame of TUNNEL is 333 ms for about 18 rays per pixel (below): 30 M rays, 90 M rays per
second, where an RTX 4080 traces a few G rays per second in scenes of this size. Lavapipe traces the same
TUNNEL frame about 210 times slower than the 4080 did, GLASS CATHEDRAL only about 45 times slower: the RTX
loses most of its advantage, more so in the chapters with the most code in play. That points at costs
that grow with the size of the pipeline's code and live state per ray (register pressure, spills around
every trace, occupancy) rather than at the number of samples; the sections below list them.

## How to measure

### --bench, --bench-counters, --bench-capture

```sh
# The acceptance runs: every chapter at 720p as the window renders it (fast mode, each chapter at its table
# scale): the 60 fps table (the default since round 3; every line's "fps" should be at least 60) and the
# 30 fps one (at least 30)
Evolution --bench --width 1280 --height 720 > bench-720p.jsonl
Evolution --bench --width 1280 --height 720 --target-fps 30 > bench-720p-30.jsonl
# Export frames as --render-stream renders them (round 3): s/frame ("fps"), per-pass GPU time, and with
# --bench-counters the work per pixel and the slice count, per chapter
Evolution --bench --bench-export --width 3840 --height 2160 --spp 16 --bench-frames 2 > bench-export-4k16.jsonl
# The half-rate volume layer off (round 3), for an A/B
EVO_HALF_VOLUME=0 Evolution --bench --width 1280 --height 720 --chapters 15,13,12,4 > bench-full-volume.jsonl
# The same at 1440p
Evolution --bench --width 2560 --height 1440 > bench-1440p.jsonl
# The old fixed scale, to compare with the RTX 4080 numbers above (fast mode on, then off)
Evolution --bench --width 2560 --height 1440 --trace-scale 0.67 > bench-1440p-0.67.jsonl
Evolution --bench --width 2560 --height 1440 --trace-scale 0.67 --no-fast-mode > bench-1440p-0.67-nofast.jsonl
# The five slowest chapters with the work counters and scene statistics (slower: the counting raygen)
Evolution --bench --width 1280 --height 720 --chapters 15,13,12,8,4 --bench-frames 30 --bench-counters > counters-720p.jsonl
# A/B of the scene pipelines and of the filtered grid (environment overrides)
EVO_SCENE_PIPELINES=0 Evolution --bench --width 2560 --height 1440 --chapters 15,13,12,8,4 > bench-generic-pipeline.jsonl
EVO_FILTERED_GRID=0 Evolution --bench --width 2560 --height 1440 --chapters 12,14,22 > bench-grid-8-fetches.jsonl
# The pictures of two runs, to compare by eye
Evolution --bench --width 2560 --height 1440 --chapters 12,13,15 --bench-frames 60 --bench-capture fast
Evolution --bench --width 2560 --height 1440 --chapters 12,13,15 --bench-frames 60 --bench-capture full --no-fast-mode
# Which trace scale holds 30 fps per chapter at 720p (fills app/auto_scale.cpp's table)
for s in 0.67 0.6 0.55 0.5 0.45 0.4 0.35 0.3 0.25; do Evolution --bench --width 1280 --height 720 --trace-scale $s > bench-720p-$s.jsonl; done
```

On Windows run `Evolution.exe` from the build's `bin` directory. Chapter indices are `--show-info`'s.

Each line now also has:

- `gpuMs`: `particles` (particle field and its BLAS), `medium` (grid bake), `tlas`, `photons` (caustic
  photon maps; before this work they were counted in `tlas`), `trace`, `splats`, `denoise`, `upscale`,
  `bloom`, `tonemap`, `compositor`, `retro`, `fill`, `total`.
- `cpuMs`: the CPU time of recording a frame (`Renderer::render`: the scene's frame, validation, uploads,
  command recording); `waitMs`: the time `ring.begin()` waited for the GPU (GPU bound when it is large).
- `fast`: the window's fast mode (on unless `--no-fast-mode`). `traceScale`, `traceWidth`, `traceHeight`:
  what was traced (per chapter, unless `--trace-scale F` fixes it).
- With `--bench-counters`, `counters`: the path tracer's work per trace pixel per frame, from the counting
  raygen (`rt_pathtrace_counters.rgen.spv`, `shaders/rt/counters.glsl`; slower, same picture):

  | counter | meaning |
  |---|---|
  | `paths` | samples traced (1 at 1 spp; more on a history's first two frames) |
  | `primaryRays`, `bounceRays` | the surface path's camera rays and later traces |
  | `continuationRays` | traces of the primary segment's in-scattering continuation (media) |
  | `heroRays` | the extra hero wavelengths' traces (exports only) |
  | `shadowRays` | occlusion rays (NEE, surfaces and media) |
  | `paneRays` | closest-hit traces of the walk through thin panes on shadow rays |
  | `guideRays` | the unjittered centre ray of the depth/normal/motion guides |
  | `lensSegments` | bent segments beyond the first of a lensed main ray |
  | `surfaceHits` | path vertices on surfaces |
  | `surfaceNee`, `mediumNee` | next-event estimates at surfaces and in media |
  | `mediumScatter`, `inScatterPoints` | medium vertices; the primary segment's in-scatter points |
  | `gridWalks`, `gridCells`, `gridLookups`, `trackingSteps` | grid transmittance walks, majorant cells visited, density lookups (8 texel fetches each, or 1 filtered), tentative collisions |
  | `rectImportance`, `treeNodes`, `treeWalks` | rect light importance evaluations, light-tree node evaluations (8 box corners each), tree walks |

  and `stats`: the frame's instances, rect lights, portals, tree lights, thin panes, fog, lensing, grid
  voxels baked, photons shot (0 when the maps were kept), caustic texels resolved, particles (their BLAS
  built or refit), splats, spp, bounce limit, the pipeline's raygen and largest hit-shader stack size as the
  driver reports them (`vkGetRayTracingShaderGroupStackSizeKHR`: on the RTX the raygen's stack is its live
  state spilled around traces), and the features of the pipeline that traced (`pipelineFeatures`,
  4294967295 for the generic one).

### Nsight Graphics on the RTX

1. Nsight Graphics, *Start Activity* -> *GPU Trace Profiler*. Application: `Evolution.exe`, arguments
   `--bench --width 2560 --height 1440 --chapters 15 --bench-frames 600` (a chapter that runs ten seconds
   or more), working directory the `bin` directory.
2. Options: *Ray Tracing* and *Shader Profiler* metric sets on; *Start after* 5 s (past the pipeline
   compiles), *Frame count* 3.
3. In the trace, the `vkCmdTraceRaysKHR` range is the `trace` pass. Read:
   - *SM occupancy* and the raygen's *registers per thread*: the pipeline's register count is that of
     its largest shader. Below about 16 warps per SM the RT cores starve.
   - *Local memory* traffic (L1 `lmem` loads and stores): live state spilled around `traceRayEXT`. Compare
     with `raygenStackBytes` from `--bench-counters`.
   - *Warp stall reasons*: `long scoreboard` (memory latency: light tree nodes, rect lights, the grid) vs
     `RT core` (traversal).
   - *RT core* throughput and *rays per second*: with the counters' rays per pixel this gives the cost per
     ray.
   - The *Shader Profiler* hot lines in `rt_pathtrace*.rgen` (source correlation needs the SPIR-V with
     debug info: build with `glslangValidator -gVS` in `cmake/Shaders.cmake` for that run).
4. Repeat with `EVO_SCENE_PIPELINES=0` (the generic pipeline) and with `--no-fast-mode`: the difference in
   registers, occupancy and spills is what the per-scene pipelines and the fast build buy.
5. *GPU Trace* also shows the photon pass (`photons`) and the particle BLAS build (`particles`) as their own
   ranges, and whether the `tlas` build and the `trace` overlap the previous frame (they should, two frames
   in flight).

RenderDoc does not profile ray tracing; Nsight Systems shows the CPU side (`cpuMs`) and the queue
submissions if `waitMs` is not near the frame time.

## Findings: the five slowest chapters

Per trace pixel per frame (1 spp), from `--bench-counters` before the changes (`--no-fast-mode` at the
base commit), lavapipe at 480x270 (322x181 traced), 20 % into each chapter. The counts do not depend on
the resolution or the GPU.

| | TUNNEL | VECTOR CITY | ASCENT | CITY NIGHT (per path) | GLASS CATHEDRAL |
|---|---|---|---|---|---|
| instances / rect lights / tree lights | 164 / 1 / 6,144 | 58,925 / 24 / 41,839 | 58,953 / 24 / 41,839 | 58,581 / 24 / 41,839 | 732 / 15 / 0 |
| media | fog | fog | fog + grid 192x48x192 | fog, 480,000 rain splats | fog, thin panes |
| camera rays | 1 | 1 | 1 | 1 | 1 |
| bounce rays | 2.89 | 2.39 | 2.46 | 2.34 | 3.29 |
| medium continuation rays | 2.97 | 2.57 | 2.66 | 2.60 | 3.11 |
| shadow rays | 10.26 | 8.67 | 9.14 | 8.71 | 7.41 |
| pane rays (closest-hit, on shadow rays) | 0 | 0 | 0 | 0 | 3.01 |
| guide rays | 1 | 1 | 1 | 0.43 | 1 |
| all traces | 18.1 | 15.6 | 16.3 | 15.1 | 18.8 |
| surface vertices | 6.67 | 4.67 | 3.59 | 3.75 | 6.69 |
| NEE: surfaces / media | 6.58 / 4.18 | 4.64 / 4.40 | 3.57 / 5.82 | 3.72 / 5.26 | 4.62 / 4.63 |
| in-scatter points | 4 | 4 | 4 | 4 | 4 |
| medium vertices | 0.18 | 0.40 | 1.82 | 1.25 | 0.63 |
| grid: walks / cells / lookups / tracking steps | - | - | 1.59 / 19.3 / 92 (738 texel fetches) / 15.7 | - | - |
| light-tree walks / node evaluations | 10.9 / 297 | 7.2 / 235 | 7.5 / 245 | 5.2 / 176 | - |
| rect light importance evaluations | 22 | 181 | 188 | 184 | 87 |
| photons per frame | - | - | - | - | 1,048,576 (2 maps of 512^2) |
| particle BLAS (full rebuild per frame) | 6,144 | - | - | - | - |
| CPU per frame on this machine | 3.5-4.2 ms | 14-15 ms | 18-21 ms | 15-17 ms | 0.8 ms |

CITY NIGHT's timed frames contained a cut (a history reset traces 4 then 2 spp), so its row is divided by
its 2.33 paths per pixel. Bounces: the window traces at most 4 scattering events after the primary hit,
the exports 6 (`render::resolveMaxBounces`); Russian roulette starts at bounce 3. Light-tree depth is
about 13 (6,144 lights) and 16 (41,839): each walk evaluates two children per level, 8 box corners each.

What the counts say, chapter by chapter:

- **TUNNEL.** 18 traces per pixel, of which 10 are shadow rays: 4 in-scatter points of the fog's primary
  segment each send one, the medium continuation path another 3, the surfaces 3. The 6,144 beads are a
  particle field: their BLAS is rebuilt every frame, and since they move the CPU rebuilds the light tree
  every frame too (`PathTracer::lightTreeFor`). 297 light-tree node evaluations per pixel.
- **VECTOR CITY.** 59,000 instances, so the TLAS is built from 59,000 instances every frame (fine on the
  RTX, 125 ms on lavapipe). 24 sign lights are picked by `pickLight`, which evaluates every rect light's
  importance twice per pick (the sum, then the choice) and again for the MIS pdf of every BSDF ray that
  hits one: 181 evaluations per pixel. The light tree (41,839 lamps and beacons) is refit per frame on the
  CPU. The facades' procedural shading already falls back to its average at a distance and after rough
  bounces (`cityFacade`'s `detail`), so it is not a hot spot.
- **ASCENT.** VECTOR CITY's costs plus the cloud deck: a 192x48x192 grid baked every frame (1.77 M voxels)
  and, per pixel, 1.6 transmittance walks, 19 majorant cells, 92 density lookups of 8 `texelFetch`es each
  (the lavapipe workaround in `gridVoxel`, 738 fetches a pixel), 16 tracking steps. Every shadow ray that
  crosses the deck walks it voxel by voxel (residual ratio tracking with a control per voxel).
- **CITY NIGHT.** As VECTOR CITY, with heavier fog (more medium vertices), the rain's 480,000 splats, and
  cuts: each cut resets the history and the next two frames trace 4 and 2 spp.
- **GLASS CATHEDRAL.** Few instances but every shadow ray walks the stained glass: after the occlusion ray,
  closest-hit traces through each pane in the way and one more that misses (3 per pixel), each copying the
  31-word payload, and the miss ran the sky shader for nothing. 1,048,576 caustic photons per frame through
  the glass pillars (up to 10 delta events each), splatted with atomics into two 512^2 maps and resolved
  every frame, from a new seed every frame, so the primary hits' caustics (which bypass the denoiser's
  history) flicker. On lavapipe the photon pass was 42 % of the frame.

Across all chapters:

- **One raygen, one pipeline, every feature inlined.** `pathtrace.rgen` compiles the medium integrator
  (fog closed forms, the grid's DDA, ratio and delta tracking, four in-scatter requests in local arrays,
  the continuation pass), the lens's RK4 geodesics, the light tree, the caustic exclusion and the export's
  four-wavelength hero split into one function, and `pbr.rchit` every scene's surface patterns. All of it
  is live in every scene. On NVIDIA GPUs a ray tracing pipeline runs at the register count of its largest
  shader, and the raygen's state that is live across `traceRayEXT` (the path, the hero split's saved
  state, the medium requests, the caustic chain) is spilled to the ray tracing stack around every trace:
  about 16-19 traces per pixel each pay it. This is the most likely source of the RTX's slowness relative
  to the work done; the counters' `raygenStackBytes` and Nsight's register and local-memory numbers will
  confirm or refute it.
- **Media everywhere.** All five chapters have fog: 4 in-scatter points per pixel, each a full next-event
  estimate with its own shadow ray and light pick (4.2-5.8 medium NEE per pixel), plus a continuation path.
- **No any-hit shaders.** All geometry is opaque; thin panes are handled by the closest-hit walk above.
- **Acceleration structures.** Static BLASes are built once at prepare (compacted). The TLAS is rebuilt
  every frame (PREFER_FAST_TRACE); a refit is not possible where instance counts change (the City's
  traffic). The particle BLAS is rebuilt every frame (PREFER_FAST_BUILD): 6,144 AABBs in TUNNEL, 2,097,152
  in DOTS, PARTICLE OCEAN and ASCENDANT.
- **CPU per frame.** The City's frame is 15-21 ms of CPU on this machine (a 4-core VM shared with lavapipe):
  `City::frame` copies 59,000 instances and 41,839 tree lights (2.8 ms), `checkSceneFrame` validates them
  all (2.6 ms), the tracer packs instance records and previous transforms (3 ms), builds the light tree's
  key, compares it, refits and uploads 4.7 MB of nodes and lights (2.5 ms), and converts the TLAS instances
  (1.8 ms). On a desktop CPU about half that: inside a 16.7 ms frame with two frames in flight, but not by
  much.
- **Sync.** The window's loop waits only in `ring.begin()` for the slot it reuses (two frames in flight);
  no `vkQueueWaitIdle` or fence wait per frame. `vkQueueWaitIdle` appears only in setup and resize paths
  (`reserve`, `resize`, `setTraceScale`). `waitMs` in `--bench` is the GPU-bound wait. The exports wait for
  every frame's readback (`ring.wait(ring.submit())`), which costs nothing at 16-180 s a frame.
- **Denoiser and post.** Accumulation, 3 a-trous passes (5 on reset frames), 4 on the volume layer, combine,
  upscale, bloom, tonemap: about 10 full-screen compute passes, a few ms at 1440p on the RTX; 3-6 % of the
  frame on lavapipe.
- **Offline cold start.** Each `--render-stream` process (each 10 s chunk of `tools/render_video.py`)
  prepared its scenes (BLASes, 0.5 s) and compiled the path tracer's pipeline (4.3-4.9 s on lavapipe;
  seconds on the RTX where the driver's own shader cache does not keep it) and some 30 compute pipelines
  again. Nothing is rebuilt per frame within a process.

## What changed

Each item is one commit on `perf-realtime`. "Window" means the window and `--bench`; exports
(`--capture-dir`, `--render-stream`) never use fast mode, and their pictures are unchanged: the baseline
sums (`tests/baseline/hash_outputs.py --check`) match after every commit.

1. **Measurement** (`--bench-counters`, per-pass `photons`/`medium`/`particles`/`splats`/`bloom` times,
   `cpuMs`, `waitMs`, the pipeline's stack sizes; `--bench-capture`). The counting raygen is a separate
   SPIR-V; the shaders the show runs are unchanged.
2. **The pane walk's misses skip the sky** (all modes, identical results): a third miss shader that only
   marks the miss, instead of evaluating the sky's lighting for every shadow ray through stained glass.
3. **Hardware-filtered grid lookups** (all modes, every driver but lavapipe, detected by
   `VkPhysicalDeviceDriverProperties::driverID`): one trilinear `textureLod` instead of 8 `texelFetch`es
   and the arithmetic. Lavapipe keeps the 8 fetches and its bytes. `EVO_FILTERED_GRID=0|1` overrides.
   Forced on in lavapipe, ASCENT's picture differs by 1 level in 1 pixel.
   The grid's empty-space skipping was already there (cells whose majorant is 0 are stepped over by the
   DDA); the per-voxel control of residual ratio tracking stays: it keeps the clouds' transmittance nearly
   noise-free, which the 1 spp window needs more than the exports do.
4. **The window's fast mode** (`--no-fast-mode` turns it off):
   - the fast raygen (`rt_pathtrace_fast.rgen.spv`): 2 in-scatter points on the primary segment instead of
     4 (the request arrays shrink with them), one wavelength per path (the hero split's saved state is
     compiled out);
   - photon maps from a seed fixed per chapter, kept while nothing they read changes (a hash of the
     scene's instances, lights, materials, environment and caustics): GLASS CATHEDRAL shoots no photons
     while the nave stands still, and the caustics' grain no longer flickers frame to frame;
   - a history's first two frames trace 2 and 1 spp instead of 4 and 2 (a cut no longer costs 4 frames'
     work);
   - one pipeline per scene, specialized for the features its frames use (next item);
   - the particle field's BLAS refit between rebuilds (item 6).
   Tried and dropped because they changed the picture (`--bench-capture`, 30 frames, lavapipe): Russian
   roulette from bounce 2 (-1.8 to -3.0 % mean level: the survivors of dark bounces were amplified past
   the scenes' low indirect clamps, which then cut them), one in-scatter point (ASCENT -2.5 %: the
   noisier volume layer loses its peaks to the denoiser's history clamp; two points leave -1.5 % there,
   -0.7 % in CITY NIGHT, -0.4 % in the cathedral) and a quarter of the photons (the primary hit's caustic
   bypasses the temporal history, so the floors' caustics turned visibly grainier). With four points the
   rest of fast mode matches the full window to 0.01 %.
   "One NEE per vertex" was already the case; "cheaper facades at a distance" were already there.
5. **A pipeline per scene** (window): specialization constant 0 of every path tracer shader is a feature
   mask (lensing, fog, grid, caustics, panes, light tree, dispersion, retro screen, and the pattern groups
   of the Studio, the City, the Ship, the sea, PLASMA PLANET's sea and the tesserae). `ModernStage::prepare`
   probes the scene's chapters a second apart and builds a pipeline for what they use; a frame that needs
   more falls back to the generic pipeline. The generic pipeline's pictures and the specialized ones are
   byte-identical on lavapipe (`bench_test`). `EVO_SCENE_PIPELINES=0` keeps the generic one.
6. **Particle BLAS refit** (window): built with ALLOW_UPDATE, updated in place while the count is the same
   and the slot's last record is less than 0.25 s old, rebuilt every 9th time.
7. **Adaptive trace scale** (`--trace-scale auto`, the default of the window and of `--bench`, which uses
   the table alone so its numbers are reproducible): each chapter starts at its table scale for 1280x720
   (`app/auto_scale.cpp`, estimated for 30 fps on the RTX 4080, below), times the square root of the pixel
   ratio for other outputs, within 0.3..0.67; in the window a controller lowers it after 30 frames over
   1.1 x 31 ms of GPU time and raises it a step after 180 frames under 0.5 x (room for 60 fps). A change
   resets the history (like a resize), so it is applied at once only at chapter changes. The table holds
   estimates; fill it from the per-scale benches above.
8. **Pipeline cache on disk** (all modes): `pipeline_cache.bin` beside the shaders, so a second process
   (every chunk of a render) compiles nothing again. `EVO_PIPELINE_CACHE=<path>|0`.
9. **CPU**: the last frame's instances move instead of being copied (4.7 MB a frame in the City).

Not done, and why:

- **TLAS update instead of rebuild**: the City's instance count changes with its traffic, and a 59,000
  instance TLAS build is well under a millisecond on the RTX.
- **Light tree refit**: it already is (`lightTreeFor` rebuilds only when positions change: TUNNEL's beads
  move, so it rebuilds; the City's refit for blinking).
- **Grid bakes at a lower resolution or only on change**: ASCENT's clouds change every frame (they open on
  the kicks), and a lower resolution would show; the bake is about 2 % of the frame on lavapipe.
- **Fewer glass bounces in GLASS CATHEDRAL**: the window's 4 bounces already cut the piers' refraction
  chains short; fewer would visibly darken the glass.
- **Faster light selection**: the rect-light and tree evaluations are 10-30 thousand flops per pixel,
  about a millisecond at 1440p on the RTX.

## Measured on lavapipe (480x270 output, 322x181 traced)

Per-pass GPU milliseconds, the best of four runs of 8 timed frames each (the machine was shared with
other jobs, so single runs vary by up to 40 %), `--no-fast-mode` (the old window path, plus the changes that
are always on) against the default fast mode, both at the fixed scale 0.67. Lavapipe has no RT cores and no
register file to run out of: it shows the work removed, not the RTX-specific gains (scene pipelines,
filtered grid lookups, BLAS refits).

| Chapter | trace | photons | total | traces per pixel (counters) | shadow rays | medium NEE |
|---|---|---|---|---|---|---|
| TUNNEL | 2245 -> 2456 (noise) | - | 2373 -> 2611 | 18.1 -> 16.1 | 10.3 -> 8.3 | 4.2 -> 2.2 |
| VECTOR CITY | 1100 -> 1036 | - | 1617 -> 1590 | 15.6 -> 13.6 | 8.7 -> 6.7 | 4.4 -> 2.4 |
| ASCENT | 2231 -> 1901 | - | 2759 -> 2451 | 16.3 -> 14.3 | 9.1 -> 7.1 | 5.8 -> 3.8 |
| CITY NIGHT (a cut in the timed frames) | 1879 -> 1283 | - | 2330 -> 1812 | 35.1 -> 17.7 | 20.3 -> 8.9 | 12.3 -> 4.4 |
| GLASS CATHEDRAL | 662 -> 634 | 621 -> 0 | 1395 -> 751 | 18.8 -> 16.8 | 7.4 -> 5.4 | 4.6 -> 2.6 |

Other chapters (counters, `--detail 0.25`, traces per pixel / grid density lookups per pixel):
PLASMA 7.8 -> 6.3 / 352 -> 257, NEBULA 7.9 -> 6.1 / 709 -> 462, RAYTRACE 12.1 -> 10.1, PARTICLE OCEAN
11.5 -> 9.5, RAYTRACE INFINITY 12.4 -> 10.4, LENS OF GOD 13.6 -> 11.6, METROPOLIS 11.7 -> 9.7, FINALE 2.9
(its cost is the 2,097,152-particle field's BLAS, now refit). NEBULA and PLASMA read the grid 700 and 350
times per pixel at 8 texel fetches each: on the RTX the filtered lookup removes 7 of every 8 fetches there.

Single changes:

- Filtered grid lookups (`EVO_FILTERED_GRID=1` forced on lavapipe, ASCENT, fast mode): trace 1980 -> 1685 ms
  (-15 %); an earlier run without fast mode 1684 -> 1302 ms (-23 %).
- Photon maps kept while the nave stands still: GLASS CATHEDRAL's photon pass 621 -> 0 ms (42 % of the
  lavapipe frame before).
- Particle BLAS refit (PARTICLE OCEAN at `--detail 0.25`, 524,288 particles): 522 -> 619 ms on lavapipe,
  within its noise (lavapipe's update is no cheaper than its build); on NVIDIA a refit costs a fraction of a
  build of 2 M AABBs.
- Scene pipelines: byte-identical pictures, the same speed on lavapipe (LLVM on a CPU has no occupancy to
  gain); the RTX gain is in registers and occupancy, see the Nsight steps.
- The pane walk's miss shader: 1 of the cathedral's 3 pane traces per pixel no longer evaluates the sky.
- At the original commit (single runs, less load on the machine) the same five chapters traced in 2350,
  843, 1580, 1981 and 577 ms, the cathedral's photons 492 ms (then counted in `tlas`).

## Expected on the RTX 4080, and 30 fps at 720p

The model: a chapter's RTX frame time at 1440p and scale 0.67 (the RTX 4080 numbers) times the fast mode's
remaining work f (from the counters: shadow rays and medium NEE fewer, photons kept, grid lookups filtered,
BLAS refit; scene pipelines counted as no gain, so any gain there is margin), times the traced pixels
relative to 1715x965, plus 2 ms that do not scale with the trace (TLAS, BLAS, bake, photons, post).
NEBULA and PLASMA's f assumes the filtered grid halves their cost (they do 350-700 lookups a pixel).

| Chapter | RTX 4080 fps, 1440p @0.67 | f | est. fps, 720p @0.67 | table scale @720p | est. fps, 720p (default) | clears 30 @720p? | est. fps, 1440p (default, scale 0.3) |
|---|---|---|---|---|---|---|---|
| GLASS CATHEDRAL | 1.3 | 0.76 | 7 | 0.30 | 32 | just, if the RTX scales with traced pixels | 8 |
| CITY NIGHT | 1.8 | 0.85 | 8 | 0.30 | 39 | yes | 10 |
| ASCENT | 1.9 | 0.82 | 9 | 0.35 | 32 | just | 11 |
| VECTOR CITY | 2.4 | 0.87 | 11 | 0.35 | 37 | yes | 13 |
| TUNNEL | 3.0 | 0.89 | 13 | 0.40 | 35 | yes | 16 |
| METROPOLIS | 3.4 | 0.83 | 16 | 0.45 | 34 | yes | 20 |
| RAYTRACE | 3.5 | 0.83 | 16 | 0.45 | 35 | yes (photons every frame: its spheres move) | 20 |
| PARTICLE OCEAN | 3.8 | 0.75 | 19 | 0.50 | 34 | yes | 24 |
| RAYTRACE INFINITY | 4.5 | 0.84 | 21 | 0.50 | 36 | yes | 25 |
| LENS OF GOD | 4.7 | 0.80 | 22 | 0.55 | 33 | just | 28 |
| FINALE | 5.2 | 0.85 | 23 | 0.55 | 34 | yes (its cuts reset at 2 then 1 spp now) | 29 |
| NEBULA | 6.9 | 0.50 | 50 | 0.67 | 50 | yes | 50 (scale 0.3) |
| PLASMA | 7.6 | 0.50 | 54 | 0.67 | 54 | yes | 54 (scale 0.3) |
| every other 2026 chapter | not listed (taken as > 7.6) | <= 0.9 | > 33 | 0.67 | > 33 | yes, if it was above 7.6 fps at 1440p | |

Why these and not more: at the old fixed scale the fast mode alone takes the five slowest chapters from
1.3-3.0 fps at 1440p to an estimated 7-13 fps at 720p; the rest of the 30 fps comes from tracing fewer
pixels (0.3 of 1280x720 is 384x216, upscaled in time by the temporal upscaler). That is a softer picture
in those chapters at 720p; it is the price of the hard 30 fps target while the RTX-specific cost per ray is
unknown. The estimates for the three chapters marked "just" have no margin: if the RTX numbers come in
under 30, lower their table entries by 0.05 (below 0.3, lower `kAutoMinTraceScale` with them; 0.25 is the lowest
trace scale the program accepts), or measure
whether the scene pipelines already gave the margin (`EVO_SCENE_PIPELINES=0` A/B). If the RTX numbers show
headroom, raise the entries: the table is the one place to tune (`app/auto_scale.cpp`), and
`--bench --width 1280 --height 720 --trace-scale S` for S = 0.3 ... 0.67 gives the data. In the window the
controller corrects a wrong entry within about half a second (it lowers the scale while the GPU misses
31 ms, down to 0.3, and raises it while there is room for 60 fps).

60 fps at 720p would need about another 2x in the five slowest chapters, and 1440p about 4x on top: that
needs the RTX's cost per ray, which the counters and Nsight will show (see "Open items").

## Round 2: fewer rays per pixel (perf-realtime-2)

Round 1 got to 30 fps at 720p mostly by tracing fewer pixels. Round 2 cuts the work per traced pixel in the
window's fast mode. The picture's expectation stays the same, and exports are unchanged: the baseline sums match
after every commit, and none of the new code is in the export build. The work counters below come from
`--bench-counters` on lavapipe at 480x270 (322x181 traced), 4 frames from 20 % into each chapter. They do not
depend on the GPU. The RTX figures are **estimates** from the round-1 model, with f scaled by the traces per
pixel; nothing here was measured on an RTX.

### What changed

1. **One trace for a shadow ray through stained glass** (7a71aef). In fast mode, when every pane in a frame is
   a Glass mesh, the pane instances are FORCE_NO_OPAQUE. They are not opaque only to a shadow ray traced
   without the opaque flag; every other ray keeps the flag. The Glass hit group's any-hit (`shaders/rt/pane.rahit`)
   multiplies the pane's transmission (tint times 1 - R', as the walk did) into a four-word payload and ignores
   the hit. A fourth miss shader marks the ray unoccluded. The Glass scene's BLASes are built with
   NO_DUPLICATE_ANY_HIT_INVOCATION, so each pane counts once. This replaces one occlusion ray, then one
   closest-hit trace per pane with the 37-word payload saved and restored, then one more trace that missed.
   GLASS CATHEDRAL drops from 15.90 to 13.84 traces per pixel, and its fast-mode picture on lavapipe is
   byte-identical to the walk's.
2. **Roulette on faint shadow rays and on paths from bounce 2, weighted after the clamp** (065c8e9).
   - **Shadow rays.** A next-event estimate made after a rough vertex has an upper bound before its shadow ray
     is traced: the unshadowed estimate, clamped. If that bound is below a level, the shadow ray is traced with
     probability bound / level. The level is 1/4096 of display white at the frame's exposure, or 1/32 of what
     the sample has added to its pixel so far, whichever is larger. A rouletted ray also skips the light's
     transmittance through the media and the panes.
   - **Paths.** Russian roulette also runs at bounce 2, with the survival probability already used from
     bounce 3.
   - **Why the mean holds.** Both weights (1 / probability) multiply the contribution *after* the indirect
     clamp, so the expectation is exactly that of the clamped estimate. Round 1's roulette from bounce 2
     divided the throughput before the clamp; the clamp then cut the amplified survivors, and that was the
     darkening it measured.
   - **Scope.** Only the surface path is rouletted. Rouletting the volume layer's continuation too made its
     noise high enough for the denoiser's history clamp to cut light: up to 0.9 % darker, and 3-13 % with a
     roulette at the continuation's start (LENS OF GOD's clouds, METROPOLIS's haze).
   - `EVO_NEE_ROULETTE=<display level>` overrides 1/4096; `0` turns the shadow-ray roulette off, for A/B runs.
3. **Four in-scatter points in fast mode again** (1292936). The look check below found the two points of
   round 1 out of tolerance against the full window: METROPOLIS's lit haze was 7.3 % darker and ASCENT's
   clouds 1.6 % darker, both through the volume history clamp. Three points still left METROPOLIS at -3.3 %.
   Four, as the exports trace, bring every chapter within 0.5 %. This costs two shadow rays per pixel in the
   chapters with a medium, paid for by items 1 and 2.

Not done:
- **A single-pass light pick.** It would halve the rect-importance evaluations, but those are a few
  thousand flops per pixel.
- **Guides from the jittered primary hit** (saves the guide trace, 1 per pixel). It would move the
  denoiser's and upscaler's guides off the pixel centre, which changes the picture at edges and with depth of
  field.
- **Fewer photons for LENS OF GOD.** It re-shoots its 1,048,576 photons every frame because the lens moves;
  fewer photons showed in round 1.

### Counters and estimates

Per trace pixel per frame, fast mode, round 1 (69d140e) -> now (1292936). The estimate columns are the
round-1 model at the round-1 table scales:

| Chapter | traces/px, round 1 -> now | shadow | bounce + continuation | grid lookups | f round 1 -> now | est. fps 720p at the table scale, before -> after | table scale | est. fps 1440p (scale 0.30), before -> after |
|---|---|---|---|---|---|---|---|---|
| GLASS CATHEDRAL | 15.90 -> 14.84 | 7.50 -> 7.04 | 6.40 -> 5.81 | 0 -> 0 | 0.76 -> 0.71 | 32 -> 34 | 0.30 | 8 -> 9 |
| CITY NIGHT | 13.39 -> 14.03 | 6.67 -> 7.89 | 4.91 -> 4.34 | 0 -> 0 | 0.85 -> 0.89 | 39 -> 37 | 0.30 | 10 -> 10 |
| ASCENT | 14.25 -> 14.76 | 7.14 -> 8.30 | 5.11 -> 4.46 | 71 -> 83 | 0.82 -> 0.85 | 32 -> 31 | 0.35 | 11 -> 11 |
| VECTOR CITY | 13.63 -> 13.99 | 6.67 -> 7.64 | 4.96 -> 4.34 | 0 -> 0 | 0.87 -> 0.89 | 37 -> 37 | 0.35 | 13 -> 13 |
| TUNNEL | 16.12 -> 16.13 | 8.26 -> 9.15 | 5.86 -> 4.98 | 0 -> 0 | 0.89 -> 0.89 | 35 -> 35 | 0.40 | 16 -> 16 |
| METROPOLIS | 10.78 -> 12.01 | 5.13 -> 6.66 | 3.65 -> 3.35 | 0 -> 0 | 0.83 -> 0.92 | 34 -> 31 | 0.45 | 20 -> 18 |
| RAYTRACE | 10.08 -> 11.76 | 4.45 -> 6.29 | 3.63 -> 3.46 | 60 -> 80 | 0.83 -> 0.97 | 35 -> 30 | 0.45 | 20 -> 17 |
| PARTICLE OCEAN | 9.52 -> 11.06 | 3.85 -> 5.60 | 3.67 -> 3.46 | 0 -> 0 | 0.75 -> 0.87 | 34 -> 29 | 0.50 | 24 -> 21 |
| RAYTRACE INFINITY | 11.25 -> 12.88 | 4.91 -> 6.77 | 4.34 -> 4.11 | 0 -> 0 | 0.84 -> 0.96 | 36 -> 32 | 0.50 | 25 -> 22 |
| LENS OF GOD | 12.68 -> 14.13 | 6.09 -> 7.86 | 4.59 -> 4.27 | 180 -> 212 | 0.80 -> 0.89 | 33 -> 29 | 0.55 | 28 -> 25 |
| FINALE | 2.98 -> 2.89 | 0.46 -> 0.40 | 0.52 -> 0.49 | 0 -> 0 | 0.85 -> 0.82 | 34 -> 35 | 0.55 | 29 -> 30 |
| NEBULA | 6.09 -> 7.95 | 2.60 -> 4.46 | 1.49 -> 1.49 | 674 -> 1036 | 0.50 -> 0.65 | 50 -> 39 | 0.67 | 60 -> 48 |
| PLASMA | 6.43 -> 7.99 | 2.72 -> 4.28 | 1.71 -> 1.71 | 367 -> 508 | 0.50 -> 0.62 | 54 -> 44 | 0.67 | 66 -> 54 |

The model is round 1's: t = T(1440p, 0.67) x f x traced pixels / (1715x965) + 2 ms, now with f = f(round 1) x
traces after / traces before. It treats every trace as equally expensive. That understates the gain in GLASS
CATHEDRAL, whose removed pane traces each ran a closest-hit and copied the payload twice. It also ignores
the LENS OF GOD photon pass, which runs every frame and does not shrink with the trace scale.

The look: mean display level of the last of 30 frames at 480x270 on lavapipe, fast mode against
`--no-fast-mode`, at the base commit (69d140e) and after this round:

| Chapter | fast vs full, round 1 | fast vs full, now |
|---|---|---|
| GLASS CATHEDRAL | -0.56 % | -0.42 % |
| ASCENT | -1.58 % | -0.15 % |
| CITY NIGHT | -0.72 % | -0.13 % |
| LENS OF GOD | +0.07 % | -0.11 % |
| RAYTRACE INFINITY | +1.76 % | -0.08 % |
| METROPOLIS | -7.25 % | -0.15 % |
| RAYTRACE | -0.34 % | -0.03 % |
| PARTICLE OCEAN | +0.01 % | -0.19 % |
| NEBULA | -1.11 % | -0.02 % |
| FINALE, STUDIO NIGHT, PLASMA PLANET, CRYSTAL CITY | - | -0.03, +0.01, +0.00, +0.06 % |

TUNNEL and VECTOR CITY are still in their 1993 halves at that point of the chapter, so this check does not
cover them. Their counters move the same way as the City's and the medium chapters'. By eye, the captures of
LENS OF GOD, METROPOLIS, ASCENT and GLASS CATHEDRAL match the full window. Before this round, the two-point
volume layer had made the clouds and haze visibly flatter.

### The result, and the table

What the round bought, per chapter (estimates):
- **Items 1 and 2 on their own** (two in-scatter points; an intermediate build that also rouletted the
  continuation): GLASS CATHEDRAL went from 15.90 to 11.25 traces per pixel, ASCENT 14.25 to 10.90, CITY
  NIGHT 13.39 to 10.12, TUNNEL 16.12 to 11.38. That is an estimated 41-50 fps at the round-1 scales. Leaving the
  continuation alone gave back about 0.3-0.6 of those traces.
- **What the look cost.** Item 3 adds about 1.8-2.0 in-scatter shadow rays per pixel in every chapter with a
  medium. That brings the medium chapters back to about round 1's ray counts, now with every chapter within
  0.5 % of the full window instead of 7 %. Without those added rays, items 1 and 2 save 3.1 traces per pixel
  in GLASS CATHEDRAL, 2.0 in TUNNEL, 1.5 in ASCENT, 0.7 in METROPOLIS and 0.2-0.4 in LENS OF GOD and RAYTRACE
  INFINITY.
- **Net, against round 1's table:**
  - GLASS CATHEDRAL gains: est. 32 -> 34 fps.
  - TUNNEL, VECTOR CITY, CITY NIGHT, ASCENT and FINALE are within about ±1.5 fps of round 1.
  - The chapters whose fog round 1 had thinned to two points lose 10-17 % of their estimate: METROPOLIS,
    RAYTRACE, PARTICLE OCEAN, RAYTRACE INFINITY, LENS OF GOD, NEBULA and PLASMA.
- **Not met: the 40 fps target.** No chapter has a 40 fps margin at its table scale.
- **Table changes** (`app/auto_scale.cpp`), to keep every chapter at or above 30 est. fps: two entries go down,
  PARTICLE OCEAN 0.50 -> 0.45 (est. 36 fps) and LENS OF GOD 0.55 -> 0.50 (est. 35 fps). Every other entry
  stays (the lowest estimates are RAYTRACE at 30.1 and METROPOLIS at 30.6, both at 0.45). The window's
  controller still lowers any chapter that misses 31 ms.
- **Margin for the four chapters nearest 30** (est., same model): RAYTRACE 0.45 -> 0.40 (about 37 fps),
  METROPOLIS 0.45 -> 0.40 (about 39), RAYTRACE INFINITY 0.50 -> 0.45 (about 39) and ASCENT 0.35 -> 0.30
  (about 41). Every chapter is then estimated at 34 fps or more at 720p.
- **Scales for 40 fps** (est.): GLASS CATHEDRAL and CITY NIGHT 0.25, ASCENT and VECTOR CITY 0.30, TUNNEL,
  METROPOLIS and RAYTRACE 0.35, PARTICLE OCEAN and RAYTRACE INFINITY 0.40, LENS OF GOD 0.45, NEBULA 0.65,
  PLASMA 0.67.

### Open items after round 2

- **The volume layer is now the largest cost in the medium chapters:** four in-scatter shadow rays and a
  continuation path of 2-3 traces, 40-55 % of their rays. It cannot be thinned without darkening, because its
  noise meets the denoiser's history clamp. Two directions:
  - a volume-layer history that tolerates more noise (a wider clamp on that layer only, checked against the
    full window);
  - the round-1 item of a second dispatch for it.
- **Measure on the RTX:** `Evolution --bench --width 1280 --height 720`, with `EVO_NEE_ROULETTE=0` as the A/B
  for item 2. The any-hit's cost on the RTX and the photon pass of LENS OF GOD (1,048,576 photons every frame)
  are not in the model.

## Round 3: the volume layer at half rate, 60 fps, and the export (perf-realtime-3)

As in round 2, the counters come from `--bench-counters` on lavapipe at 480x270 (322x181 traced), 4 frames from
20 % into each chapter, and the look check is the mean display level of the last of 30 frames at 480x270 against
`--no-fast-mode`. Every RTX figure is an **estimate** from the round-1 model with f scaled by traces per pixel.
Nothing here was measured on an RTX GPU. The default exports are byte-identical after every commit
(`hash_outputs.py --check`).

### What changed (window)

1. **The volume layer at half rate** (8837ac2; fast mode). The volume layer is the primary segment's
   in-scattered light: four shadow rays and a continuation path of 2-3 traces with its own shadow rays, which
   was 40-70 % of the rays in the chapters with fog. It is now traced on a checkerboard that flips every frame.
   - **Traced pixels** (`medium_path.glsl`'s `mediumLayerSkipped`) do the full layer.
   - **The other pixels** only estimate the transmittance (the fog's closed form and the grid's ratio-tracking
     walk; no rays).
   - **The fill.** The denoiser's temporal pass (`accumulate.comp`'s `volumeAt`) fills each such pixel from its
     four traced neighbours. It takes their radiance per unit opacity (1 - T), weighted by how close their
     transmittance is to its own (the volume à-trous's stop) and never across a hit and a miss, and multiplies
     it by its own opacity. This happens before the history clamp, which then takes the 3x3 min/max of the
     filled layer.
   - **Where it is off.** Frames with an emitting grid (LENS OF GOD's disc, NEBULA, PLASMA, PLASMA PLANET) keep
     the whole layer, because their emission is the layer's detail. So does a medium whose scene sets
     `MediumFrame::halfRate = false`. The city's cloud deck now declares `emissionScale = 0` (its bake writes no
     emission, and the exports are byte-identical), so ASCENT gets the half rate.
   - **METROPOLIS keeps the whole layer.** Its haze is lit by 41,839 small lamps, so its in-scattered light
     arrives on few samples. The half-rate layer was 3.5 % darker there than `--no-fast-mode` with the 3x3 box.
     Wider boxes (the traced pixels of the 5x5, or the nearest 9-12 traced) were 6-8 % brighter: the full
     window's own clamp darkens that haze. The boxes tried were the traced 5x5, the nearest traced pixels, and
     one neighbour copied instead of the mean. They moved the other chapters by up to ±2 %, so the 3x3 of the
     filled layer stays.
   - `EVO_HALF_VOLUME=0` turns it off (A/B). `denoiser_test` checks the fill on both parities and at a
     transmittance edge.
2. **A 60 fps target, the window's default** (2461a27). `--target-fps 30|60` (window and `--bench`, default 60)
   picks the table the chapters start from and the controller's GPU budget:
   - **60:** a 15.5 ms budget and scales 0.25..0.67.
   - **30:** the previous 31 ms and 0.3..0.67. Its table is unchanged, so it is now the sharper mode with margin.

   The 60 fps table (`autoTraceScaleTable60`) holds estimates:
   - **The 13 chapters measured on the RTX:** the round-1 model with the round-3 counters.
   - **The others:** their export traces per sample at the measured chapters' median cost per trace
     (13.8 ns at 1440p), with the fast mode's f taken as 0.85.

### Counters and estimates (window, 1280x720)

Traces per pixel and f: round 2 (63dd1c1) -> round 3. "30 fps table" is the current 30 fps table, with the
round-2 work -> the round-3 work. The 60 fps columns use the new table. At 1440p the 60 fps table's scales
reach the 0.25 floor in every listed chapter.

| Chapter | traces/px | f | 30 fps table @720p: est. fps | 60 fps table @720p | est. fps @720p (60 table) | est. fps @1440p (60 table, 0.25) | est. fps @1440p (30 table, 0.3) |
|---|---|---|---|---|---|---|---|
| GLASS CATHEDRAL | 14.84 -> 10.38 | 0.71 -> 0.50 | 0.30: 34 -> 47 | 0.25 | 65 | 18 | 9 -> 13 |
| CITY NIGHT | 14.03 -> 9.74 | 0.89 -> 0.62 | 0.30: 37 -> 52 | 0.25 | 72 | 20 | 10 -> 14 |
| ASCENT | 14.76 -> 10.34 | 0.85 -> 0.60 | 0.30: 41 -> 57 | 0.25 | 78 | 22 | 11 -> 15 |
| VECTOR CITY | 13.99 -> 9.72 | 0.89 -> 0.62 | 0.35: 37 -> 51 | 0.30 | 67 | 26 | 13 -> 19 |
| TUNNEL | 16.13 -> 11.29 | 0.89 -> 0.62 | 0.40: 35 -> 49 | 0.30 | 81 | 32 | 16 -> 23 |
| METROPOLIS | 12.01 -> 12.01 | 0.92 -> 0.92 | 0.40: 38 -> 38 | 0.25 | 88 | 25 | 18 -> 18 |
| RAYTRACE | 11.76 -> 8.04 | 0.97 -> 0.66 | 0.40: 37 -> 53 | 0.35 | 67 | 35 | 17 -> 25 |
| PARTICLE OCEAN | 11.06 -> 7.49 | 0.87 -> 0.59 | 0.45: 36 -> 51 | 0.35 | 80 | 42 | 21 -> 30 |
| RAYTRACE INFINITY | 12.88 -> 8.43 | 0.96 -> 0.63 | 0.45: 38 -> 56 | 0.40 | 69 | 47 | 22 -> 33 |
| LENS OF GOD | 14.13 -> 14.13 | 0.89 -> 0.89 | 0.50: 35 -> 35 | 0.35 | 67 | 35 | 25 -> 25 |
| FINALE | 2.89 -> 2.89 | 0.82 -> 0.82 | 0.55: 35 -> 35 | 0.35 | 79 | 42 | 30 -> 30 |
| NEBULA | 7.95 -> 7.95 | 0.65 -> 0.65 | 0.67: 39 -> 39 | 0.50 | 66 | 66 | 48 -> 48 |
| PLASMA | 7.99 -> 7.99 | 0.62 -> 0.62 | 0.67: 45 -> 45 | 0.50 | 75 | 75 | 54 -> 54 |

The 60 fps table's entries for the chapters not measured on the RTX (estimated from traces per pixel): FLYBY,
PORTAL, RETURN, REBOOT, GLENZ and CRYSTAL CITY 0.4, STUDIO NIGHT and PLASMA PLANET 0.45, LENS 0.55, DOTS 0.35;
SPHERE STORM, SINGULARITY, GALAXY, WORMHOLE and ASCENDANT (1-2.5 traces per pixel) 0.67.

**60 fps status (estimates).** Every listed chapter is at 60 fps or more at 720p with the 60 fps table: the
slowest are GLASS CATHEDRAL (65), NEBULA (66), and VECTOR CITY, RAYTRACE and LENS OF GOD (67).
- **Where it comes from, per chapter** (round 2's estimate at its 30 fps scale -> now):
  - **The eight chapters with the half-rate layer:** about 1.4x from real work (30-33 % fewer traces at the
    same scale). The rest is fewer pixels: GLASS CATHEDRAL 34 -> 47 fps (work) -> 65 (scale 0.3 -> 0.25);
    TUNNEL 35 -> 49 -> 81 (0.4 -> 0.3); RAYTRACE INFINITY 38 -> 56 -> 69 (0.45 -> 0.4).
  - **METROPOLIS, LENS OF GOD, FINALE, NEBULA and PLASMA:** all of it is fewer pixels. METROPOLIS goes 0.4 ->
    0.25, which is 0.39x the traced pixels; LENS OF GOD and FINALE 0.5/0.55 -> 0.35; NEBULA and PLASMA 0.67 ->
    0.5.
- **What it looks like.** At 0.25 the window traces 320x180 for a 720p picture. Next to the 30 fps table (0.3),
  GLASS CATHEDRAL and ASCENT look a little softer by eye on lavapipe, with more stair-stepping on window edges
  and facades. The temporal upscaler hides most of it while the camera moves.
- **1440p.** 60 fps is out of reach for the heavy chapters: 18-35 est. fps at the 0.25 floor (640x360 traced).

**The look** (mean display level, fast mode against `--no-fast-mode`, last of 30 frames at 480x270, which
the table clamps to scale 0.65-0.67):

| Chapter | round 2 | round 3 |
|---|---|---|
| GLASS CATHEDRAL | -0.42 % | -0.86 % |
| ASCENT | -0.15 % | -1.19 % |
| CITY NIGHT | -0.13 % | -0.27 % |
| LENS OF GOD (whole layer) | -0.11 % | -0.11 % |
| RAYTRACE INFINITY | -0.08 % | -1.25 % |
| METROPOLIS (whole layer) | -0.15 % | -0.08 % |
| RAYTRACE | -0.03 % | -0.89 % |
| PARTICLE OCEAN | -0.19 % | -0.50 % |

All are within ±1.5 %, but the half-rate layer moves every chapter that has it about 0.3-1.2 % darker. The
filled pixels are smoother than traced ones, so the history clamp's box is a little narrower. TUNNEL and VECTOR
CITY are in their 1993 halves at that point, as before. By eye the captures match the full window's, haze and
shafts included.

**Lavapipe, same-load A/B** (480x270, scale 0.67, 8 frames, two interleaved pairs, best of each; GPU ms of
the trace pass, `EVO_HALF_VOLUME=0` -> on):
- TUNNEL 2743 -> 2256 (-18 %), RAYTRACE 549 -> 431 (-21 %), CITY NIGHT 1256 -> 1066 (-15 %), GLASS CATHEDRAL
  579 -> 489 (-16 %).
- METROPOLIS, which keeps the whole layer, is the control: 926 -> 957 (+3 %, noise).

Lavapipe gains less than the 30 % fewer traces: the skipped pixels still walk the medium for their
transmittance, and ALU-heavy shading weighs more there than on an RTX.

### What changed (exports)

3. **A pipeline per scene's features in exports** (60dbd22, 477f66e). Exports now trace with the specialized
   pipelines of the window's fast mode, which so far only the window used.
   - **One pipeline per set of features.** The first frame that uses a set builds its pipeline at the trace, so
     no prepare-time probe is needed (the probe had asked scenes for extra frames, which `motion_test`
     counts).
   - **Same pictures.** The pictures are the generic pipeline's: the baseline sums are byte-identical on
     lavapipe, where specialization costs nothing.
   - **Expected RTX gain (not measured).** On the RTX the export raygen drops the code and live state of every
     feature a frame does not use. Outside dispersive glass that includes the hero-wavelength split: four
     wavelengths at 16 spp and 15 saved variables live across every trace. Round 1 put the RTX's per-ray
     slowness down to live state and register pressure, so this is the most likely export gain, but its size
     is unknown until measured: run the export bench with `EVO_SCENE_PIPELINES=0` and without it.
4. **`--bench --bench-export`** (472105f): times export frames per chapter as `--render-stream` renders them
   (Export mode, `--spp`, the shutter, a submit per slice, each frame read back before the next) and reports
   each pass's GPU time and, with `--bench-counters`, the work per pixel and the slice count.
5. **`--shutter-slices N`** (477f66e; exports, 1..8, at most `--spp`; `render_4k.ps1 -ShutterSlices N`): traces
   each frame in N slices of the 180 degree shutter instead of one per 4 samples.
   - **Why.** An export of 4 spp or fewer had no motion blur at all; with `--shutter-slices 4` it traces four
     slices of 1 sample.
   - **Cost.** The same number of samples, plus a TLAS build, a particle rebuild and the scene's frame per extra
     slice.
   - **Opt-in.** It changes the bytes, so it is an option. Without it every export is as before.
6. `render_4k.ps1 -Height 2160|1440|1080` picks the size (output `EVOLUTION-<height>p60[-s<slices>].mp4`).

**What else was checked, and is not redundant or not worth it:**
- **The particle field's BLAS** is rebuilt per slice, because the particles move between slices. A refit would
  save a few ms per frame on the RTX.
- **Photon maps** are shot once per frame, from the first slice, with a seed per frame (byte identity).
- **The grid bake** happens once per frame. **The TLAS** is built once per slice (0.1-1 ms on the RTX).
- **The host waits.** The host waits once per slice (`FrameRing::split`, against Windows' TDR) and once per frame
  (the readback). The CPU work between them (the slices' scene frames, light trees, instance packing, the
  RGB conversion) is roughly 20-200 ms per frame on a desktop CPU. That is under 1 % of a 4K frame of 10-130 s,
  and a few % at 1080p and 4 spp.
- **Pipelines, descriptors and BLASes** are built once per process (`pipeline_cache.bin` keeps compiles across
  chunks).
- **The denoiser's 4K passes** are a few ms.

The cost of an export frame is the path tracing: 16 spp at 4K is 133 M paths and 2-5 G traces.

### The export on an RTX 4080: ETA (estimates)

The model, per chapter, uses its 2026 frames at 60 fps (`planStages`: 38,000 of the film's 44,258 frames trace)
and its export work:
- **Export work per sample.** From `--bench --bench-export --spp 16 --bench-counters` at 160x90: traces per
  sample. Against the window's (round 1, `--no-fast-mode`) that is 0.74-1.19x, but 2.07x in GLASS CATHEDRAL,
  which walks its panes, splits into four hero wavelengths and shoots 4x the photons.
- **The 13 chapters measured on the RTX:** the 1440p window time x 4K/1440p pixels x 16 x that ratio.
- **The rest:** traces per sample x 13.8 ns (24.7 ns for the glass scenes), the measured chapters' median cost
  per trace.

The model gives 1.9-128 s per 4K/16 spp frame. The owner measured 16-180 s, so the table gives the model (k = 1)
and the model scaled to the measured maximum (k = 1.4). The 1993 frames are counted at 0.05 s. The model does not
count the export's scene pipelines (item 3), so it applies to the build the owner measured.

| Setting (60 fps, whole film) | k = 1 | k = 1.4 |
|---|---|---|
| 3840x2160, 16 spp | 252 h (10.5 days) | 352 h (14.7 days) |
| 2560x1440, 16 spp | 112 h (4.7 days) | 157 h (6.5 days) |
| 1920x1080, 16 spp | 63 h (2.6 days) | 88 h (3.7 days) |
| 1920x1080, 8 spp | 32 h | 44 h |
| 1920x1080, 4 spp (`--shutter-slices 4`) | 16 h | 22 h |
| 1280x720, 8 spp (`--shutter-slices 4`) | 14 h | 20 h |
| 1280x720, 16 spp | 28 h | 39 h |

**Where the time goes.** Glass scenes are about 44 % of it: GLASS CATHEDRAL alone is 27 % (an estimated
128 s per 4K frame), CRYSTAL CITY 13 % and GLENZ 3 %. Then CITY NIGHT, METROPOLIS and ASCENT, about 6-7 % each.

**Recommended within about 24 hours: 1920x1080, 60 fps, 4 spp, the shutter in 4 slices** (est. 16-22 h):

```
tools\render_4k.ps1 -Height 1080 -Spp 4 -ShutterSlices 4 -Bench   # first: the ETA on this card
tools\render_4k.ps1 -Height 1080 -Spp 4 -ShutterSlices 4
```

or directly:

```
py tools\render_video.py --exe build\windows\bin\Evolution.exe --music assets\music\show.wav --out dist\render\EVOLUTION-1080p60-s4.mp4 --width 1920 --height 1080 --fps 60 --spp 4 -- --shutter-slices 4
```

**What 4 spp gives up** against 16:
- **Denoiser:** the export firefly filter and the finer à-trous and chroma passes (they start at 16 spp).
- **Spectrum:** the four-wavelength hero split, so the glass's dispersion is noisier in colour.
- **Caustics:** the 4x photons and finer caustic maps.
- **Noise:** the per-pixel noise is 2x.
- **Kept:** motion blur, through `--shutter-slices 4`. Without it a 4 spp frame has none.

The best-looking option is 1080p at 16 spp: est. 2.6-3.7 days, or less if the scene pipelines pay off on the
RTX. If 8 spp at 1080p comes in at 24 h or less on the card (`-Bench`), it is the better trade: two to four
slices of motion blur, half the noise of 4 spp.

**Within 5 hours** (the owner's later limit, target ≤ 4.5 h). The estimates below also count fixed costs:
about 15 ms + 12 ms per slice of CPU and readback per frame, 10 s of start-up per chunk, and the 1993 frames.

| Setting (whole film) | est. h, k = 1 | k = 1.4 |
|---|---|---|
| **1600x900, 30 fps, 2 spp, `--shutter-slices 2`** | **3.1** | **4.2** |
| 1280x720, 30 fps, 3 spp, `--shutter-slices 3` | 3.1 | 4.1 |
| 1152x648, 30 fps, 4 spp, `--shutter-slices 4` | 3.3 | 4.5 |
| 1280x720, 30 fps, 4 spp, `--shutter-slices 4` | 4.0 | 5.4 |
| 1920x1080, 30 fps, 2 spp | 4.3 | 5.9 |
| 1280x720, 60 fps, 2 spp | 4.1 | 5.5 |
| 1920x1080, 30 fps, 1 spp (no motion blur) | 2.2 | 3.0 |

**Recommended: 1600x900 at 30 fps, 2 spp, two shutter slices.**
- **Why 30 fps.** Every 60 fps setting under the limit drops to 1-2 spp at 720p or less. 30 fps halves the
  frames, and its 180 degree shutter blurs over 1/60 s.
- **Why 900p.** For the same samples per frame, 900p at 2 spp looked about as clean after the export denoiser as
  720p at 3-4 spp (480x270 proxies on lavapipe), with more detail.
- **What below 16 spp gives up.**
  - The firefly filter, the hero split and the 4x photons.
  - The colour of GLASS CATHEDRAL's dispersed flare changes from frame to frame: one wavelength per sample.
  - Two slices give two-step motion blur on the fastest lights.

```
py tools\render_video.py --exe build\windows\bin\Evolution.exe --width 1600 --height 900 --fps 30 --spp 2 --chunk 30 --bench -- --shutter-slices 2
py tools\render_video.py --exe build\windows\bin\Evolution.exe --music assets\music\show.wav --out dist\render\EVOLUTION-900p30.mp4 --width 1600 --height 900 --fps 30 --spp 2 --chunk 30 -- --shutter-slices 2
```

### Open items after round 3

- **Measure on the RTX.**
  - Window: `Evolution --bench --width 1280 --height 720` (the 60 fps table) and `--target-fps 30`, with
    `EVO_HALF_VOLUME=0` as the A/B for item 1.
  - Exports: `Evolution --bench --bench-export --width 1920 --height 1080 --spp 16 --bench-frames 2`, with and
    without `EVO_SCENE_PIPELINES=0` (item 3's gain), then `render_video.py --bench` for the ETA.
- **METROPOLIS's haze**, and the volume layer's history clamp in general. The full window's 3x3 min/max clamp
  darkens a haze lit by thousands of lamps (a wider box gives 6-8 % more light). A clamp that holds the mean of
  heavy-tailed samples would let METROPOLIS take the half rate too, but it changes the full window's look.
- **The glass in exports.**
  - The window's one-trace any-hit for shadow rays through panes (round 2) would save GLASS CATHEDRAL's pane walk
    in exports too, about 11 % of its traces. It multiplies the panes' transmission in traversal order, which an
    RTX does not fix, so the result's last bits could differ between runs; it was left out.
  - Shader execution reordering (`VK_NV_ray_tracing_invocation_reorder`, RTX 40 series) is the next candidate
    for the incoherent export paths. It cannot be tested on lavapipe.
- **A split volume dispatch** (round 1's item). The half-rate layer already halves the volume work, and on
  lavapipe a split would show nothing, so it waits for Nsight's register and spill numbers.

## Open items

- Fill `app/auto_scale.cpp`'s table from `--bench --trace-scale S` runs on the RTX.
- If Nsight shows the raygen spilling heavily even in the scene pipelines, the next step is to split the
  medium continuation pass and the primary segment's in-scatter estimates out of the bounce loop (a second
  raygen dispatch for the volume layer), so the surface path's loop carries less state across traces.
- `path_tracer_test` takes 635 s on this 4-core machine (shared with other jobs), past its 300 s ctest
  timeout; it passes when run directly.
