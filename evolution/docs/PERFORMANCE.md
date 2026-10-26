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
# The acceptance run: every chapter at 720p as the window renders it (fast mode, each chapter at its table
# scale); every line's "fps" must be at least 30
Evolution --bench --width 1280 --height 720 > bench-720p.jsonl
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

## Open items

- Fill `app/auto_scale.cpp`'s table from `--bench --trace-scale S` runs on the RTX.
- If Nsight shows the raygen spilling heavily even in the scene pipelines, the next step is to split the
  medium continuation pass and the primary segment's in-scatter estimates out of the bounce loop (a second
  raygen dispatch for the volume layer), so the surface path's loop carries less state across traces.
- `path_tracer_test` takes 635 s on this 4-core machine (shared with other jobs), past its 300 s ctest
  timeout; it passes when run directly.
