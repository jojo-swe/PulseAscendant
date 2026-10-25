# EVOLUTION

EVOLUTION is a real-time demo that puts 1993 and 2026 in the same frame: every effect first appears as
it looked on a 486 and then as modern hardware can render it, with Vulkan 1.3 and hardware ray tracing
as a hard requirement. It is a standalone project inside PulseAscendant; the architecture, the determinism
rules and the RTX hardware checklist are in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

> **Status:** steps 0–4 are done. The music drives a timeline of 31 chapters in three acts, and
> every chapter with a 1993 side (act I, REBOOT, CREDITS and THE END) shows its 1993 effect through its own
> pipeline (below). The act II and act III planetary chapters are 2026 only (forced into 1993 they show a grey
> test pattern; STUDIO NIGHT and RETURN show the DOS prompt the demo exited to). The 2026 side is a path
> tracer with a denoiser and a PBR Neutral tonemap (below), and it renders the Studio in FINALE, PORTAL, STUDIO
> NIGHT, RETURN and REBOOT; the other 2026 chapters show a placeholder gradient until step 5 brings the
> modern scenes.

## Requirements

- A GPU with Vulkan 1.3 and ray tracing: `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`,
  `VK_KHR_ray_query` and `VK_KHR_deferred_host_operations`. Without them EVOLUTION names what is missing
  and exits with code 2; there is no fallback.
- Windows 10/11 or Linux.

Dependencies (SDL3, Vulkan-Headers, volk, VulkanMemoryAllocator) are fetched by CMake with git at
configure time, each pinned to an exact commit, so the first configure needs network access and `git`.
Shaders are compiled at build time with `glslangValidator`.

## Build on Linux

```sh
sudo apt install build-essential cmake ninja-build glslang-tools libx11-dev libxext-dev libxrandr-dev \
    libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev \
    libegl1-mesa-dev libgl1-mesa-dev libasound2-dev libpulse-dev libvulkan-dev
sudo apt install mesa-vulkan-drivers   # optional: lavapipe, a software Vulkan driver for testing

cd evolution
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
```

The executable is `build/linux/bin/Evolution`, with its shaders in `build/linux/bin/shaders/`. When
`assets/music/show.wav` exists at configure time, it is linked to `build/linux/bin/assets/music/show.wav`
(Windows copies it), so a dev build plays the music with no flags; without it the demo runs in SILENT MODE.

The 50 tests run headless (they also pass on lavapipe, in about 2 minutes at 8 at a time) and need Python 3
(`python_tools` and `storyboard` also numpy). Every GPU test runs with the validation layer on and fails on any
message it reports, except the two provoked on purpose: `context`'s, to check that messages are counted, and
`descriptors`' synchronisation hazard, to check that synchronisation validation is on. The `accel`,
`path_tracer`, `denoiser` and `image` tests (and that case of `descriptors`) also run synchronisation
validation.
- The show and its music: `options`, `timeline`, `era`, `clock`, `audio_transport` and `show_info`.
- The render foundation: `png`, `png_decode`, `context`, `gfx_foundation`, `descriptors` (layouts, per-slot
  pools, acceleration structure writes), `image` (mip chains, depth, the sRGB sampled view), `renderer`,
  `stage_plan` (which stages a frame runs and when the 2026 history resets, swept over the whole show),
  `compositor` (a hidden stage's image never reaches the output), `swapchain`, `font`, `text_pass` and
  `no_wait_idle`.
- The 1993 side: one test per effect (`boot`, `plasma`, `scroller`, `flyby`, `dots`, `tunnel`, `glenz`,
  `rotozoom`, `raytrace`, `city`, `finale`, and `closing` for PORTAL, REBOOT and THE END), `raster` (the polygon
  and point rasterisers against a CPU reference) and `retro_registry` (every effect a pure function of its retro
  frame, every scene within the rasterisers' limits).
- Ray tracing: `accel` (BLASes, compaction and a TLAS rebuilt every frame with two frames in flight, probed
  with ray queries), `rt_pipeline` (the shader binding table layout for lavapipe and RTX alignments, every hit
  group and miss reached, recursion refused outside the raygen), `mesh_builder` (the procedural meshes
  watertight), `material` (the GPU struct layouts, and the BSDFs against furnace means, Fresnel and a
  quadrature of their own evaluation), `path_tracer` (emitters, MIS against analytic and quadrature references
  with several lights, a portal and a two-tone screen, the furnace, determinism, finiteness under stress, the
  guides, batched dispatches), `denoiser` (accumulation, resets, reprojection, edges, the emission bypass,
  NaN/Inf), `tonemap` (the GPU against the CPU reference and golden values) and `spheres` (the sphere
  intersection against a CPU reference, depth and normals, glass against a sphere mesh, 4096 particles over 32
  frames two in flight against their analytic positions; `spheres_test --look <dir> [spp]` renders a picture),
  `motion` (the per-object motion guide of a moving instance, sphere and particle field against the analytic
  reprojection; the denoiser keeping its history on a moving object; the export shutter's edge ramp, energy,
  chapter clamping and purity; the window ignoring it; `motion_test --look <dir> [spp]` renders a motion-blurred
  picture) and
  `caustics` (photon maps under a glass sphere: their energy against the sphere's cross-section and against
  the converged path-traced caustic, dispersion, the same map in any photon order, the double-count exclusion;
  `caustics_test --look <dir> [spp]` renders a glass sphere and a prism's spectrum).
- The Studio: `studio` (STUDIO NIGHT lit but not blown out, the CRT as STUDIO NIGHT's key light, the screen
  following the 1993 picture on the very next window frame, the cameras and their cuts, the scene, the DOS
  prompt, the light rig per chapter, little speckle in the moonlit wide shot, the chrome sphere reflecting the
  screen, camera-only LEDs, the moonbeam on the floor) and
  `seek_determinism` (in the window, after any seek, chapter change, camera cut or step back in time, the
  frames equal a fresh renderer's from the same point byte for byte; BeatSwap keeps the hidden 2026 side warm
  in the window and skips it in exports).
- The executable end to end, its exports at `--spp 2`: `render_stream`, `capture`, `stream_capture` and
  `stream_capture_studio` (a frame of a stream equals a fresh capture at the same time, for every chapter; the
  Studio chapters, path traced, in a part of their own that runs beside the rest), `export_mode` (the seeds,
  export purity and the window's first frame after a reset equal to an export frame), `storyboard` (into
  `build-out/storyboard_test`, so it never touches the storyboards below), `exit_codes`, and `python_tools`
  (the music analysis pipeline).

The `linux` test preset runs 8 tests at a time; `ctest --preset linux -j 1` runs them one by one.

The three that open a window, `windowed`, `windowed_unsupported` and `silent_mode`, are off by default;
configure with `-DEVO_RUNTIME_TESTS=ON` to run them under `xvfb-run`.

Shaders are listed in `CMakeLists.txt`, except the 2026 scenes' compute shaders: every
`shaders/modern/*.comp` is globbed (`CONFIGURE_DEPENDS`). Ninja and Makefiles notice a new file there on the
next build; CMake does not promise that for every generator, so if a new scene shader is not picked up
(`modern_<scene>_<what>.comp.spv` missing beside the executable), re-run `cmake --preset ...`.
`evo_add_shader_variants` (`cmake/Shaders.cmake`) compiles a shader again with defines under a suffixed name
(`rt_pbr_sphere.rchit.spv`).

`tests/baseline/hash_outputs.py` renders a fixed set of captures and `--render-stream` runs (the Studio, the
placeholder, 1993 effects, a wipe, forced 2026) and prints their SHA-256 sums and the Studio's mean
luminances; `tests/baseline/SUMS` and `LUMINANCE` are the reference on lavapipe. A change that must not move
a pixel checks against it (`--check tests/baseline/SUMS`), and a change that moves the picture on purpose
commits new files (see the script's docstring).

## Build on Windows

> The Windows preset has not been exercised yet. The steps below are what it is designed for.

Requirements:

- Visual Studio 2026 with the "Desktop development with C++" workload.
- CMake 4.2 or newer: the preset uses the `Visual Studio 18 2026` generator, which first shipped in
  CMake 4.2 (the project itself needs only 3.25).
- The [Vulkan SDK](https://vulkan.lunarg.com/), for `glslangValidator`; CMake looks for it in
  `%VULKAN_SDK%\Bin`. The Vulkan runtime itself comes with the GPU driver.
- Git, and Python 3 (configure looks for it; the tests run it).

```bat
cd evolution
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
```

The preset builds Release into `build\windows\`; the executable is `build\windows\bin\Evolution.exe`,
with its shaders in `build\windows\bin\shaders\` (no per-configuration subdirectory, so a Debug build
replaces the Release one).

## Music

The soundtrack is three tracks by joyolo, one per act, played back to back without
gaps (credits and provenance in [`licenses/MUSIC-NOTICE.md`](licenses/MUSIC-NOTICE.md)):

| Act | Track | Tempo | Starts at | Length |
|---|---|---|---|---|
| I | Amiga Trance Demo | 141.6 BPM | 0 s | 194.00 s |
| II | Guitar Trance | 140.5 BPM | 194.0 s | 218.40 s |
| III | Rigid Arpeggios | 138.3 BPM | 412.4 s | 298.76 s |

The show is 711.16 s long. The MP3s in `music/source/` and the WAVs in `assets/music/` are git-ignored.
With the MP3s in place, this regenerates `assets/music/show.wav`, the generated headers in
`src/show/generated/` and the receipts in [`music/`](music/README.md) (needs numpy and ffmpeg):

```sh
cd evolution
python3 tools/prepare_show.py "music/source/Amiga Trance Demo.mp3" "music/source/Guitar Trance.mp3" "music/source/Rigid Arpeggios.mp3"
```

The headers and receipts are committed, so the build has the whole show without the audio; chapter
targets come from `tools/show_targets.json`. Trance delays at 3/16 put strong onsets 1.5 beats apart, so
`beat_grid.disambiguate_period()` rescores the candidate tempos by the bass onsets on each one's grid
rather than trusting the autocorrelation, which reads them at 2/3 of their tempo. The `python_tools` test
runs the pipeline into a temporary directory on a 270 s, 134 BPM placeholder song synthesised by
`tools/make_placeholder.py`, with `tools/show_targets_placeholder.json`.

## How a frame is made

Every frame is rendered into one RGBA8 image at the output size, which the window shows and the exports read
back. Only the stages a frame shows run (a pure 1993 frame never path traces, a 2026-only chapter never
renders the 1993 picture unless the Studio's CRT shows it), and the compositor is told which images are
visible, so a skipped stage's stale image never reaches the output:

1. **Retro stage.** The chapter's 1993 effect writes palette indices into a 320×200 8-bit image, as a VGA
   mode 13h screen would hold them, and a second pass maps them through the 256 colours that the effect's
   palette builder makes on the CPU. Palette cycling happens there, never in a shader. An effect is an entry
   in the registry (`src/retro/effects.cpp`): an optional compute shader (or a clear index), an optional
   scene of triangles and points for the rasteriser (below), optional text in the 8×8 font, and its palette.
   An effect may own a texture, built once on the CPU and read by its shader as an R8_UINT storage image
   (LENS's 128×128 logo checker). Each effect has its own retro frame rate (35 Hz, RAYTRACE 10 Hz); the stage
   renders only when the retro frame changes, and everything it draws is a function of the chapter and the
   retro frame alone, so seeking, playback and export give the same picture. That holds within one build on
   one machine: the C library's `sin`, `atan2` or `pow` may differ in the last bit between platforms (and the
   shaders' float maths between GPUs), which can move a pixel at a rounding edge between Linux and Windows.
2. **CRT.** The 320×200 picture fills a centred 4:3 rectangle (its pixels are 1.2 times taller than wide),
   black around it. With the CRT look on it also gets a 2 % barrel, a little bloom, darker odd scanlines, a
   phosphor mask and a vignette. Key C or `--no-crt` turns the look off; the rectangle stays the same.
3. **Modern stage.** The 2026 side, in HDR light (RGBA16F) tone mapped once. FINALE, PORTAL, STUDIO NIGHT,
   RETURN and REBOOT path trace the Studio (below) and denoise it; every other 2026 chapter shows a
   placeholder gradient in the chapter's hue with a circle on kick and beat until step 5.
4. **Era compositor.** Blends the two by the timeline's era: a Cut or BeatSwap chapter shows one era whole,
   a Wipe sweeps 2026 in from the left behind a 4-pixel refractive glass edge, a Portal cross-fades for now,
   and a BeatSwap flip flashes a quarter of white (not in FINALE or a Portal chapter).
5. **Epoch cards.** The 1993 card types in at 30 characters a second in the demo's 8×8 font, bottom-left
   inside the 4:3 picture, so the CRT bends and masks it like the rest of the 1993 side; a block cursor
   blinks on the beat. The 2026 card sets the same glyph skeletons as rounded strokes with kerning and
   tabular figures: the year large, the feat small, bottom-left in the 16:9 margin in white at 0.85 alpha,
   behind a hairline rule that draws in over one beat. Their numbers come from the configuration. Cards
   appear one bar into a chapter and go one bar after its accent, never later than two bars before its end;
   during a wipe the line splits them. The window also shows the chapter title top-left as a development
   aid; captures and `--render-stream` never do. Key H or `--no-hud` hides the HUD.

### Scene cuts and the megamix

A frame may show another chapter's 2026 scene than its own (`src/show/director.cpp`). FINALE is a megamix of
hard cuts on the beat grid, from one shot every two beats down to a quarter beat: RAYTRACE between the balls,
the CITY puddle, the DOTS logo, the LENS sphere, the GLENZ caustic, the room, TUNNEL, NEBULA, the WORMHOLE
ship, and the RAYTRACE drop held still, stopping dead on the chapter's downbeat, after which the Studio holds.
REBOOT looks at the galaxy from its glass crossing on. A cut renders the source scene at its own scene time
(advancing from the source time, or held), so its 2026 picture is that scene's picture; each cut resets the
history, so the window's first frame after it traces the reset frame's samples. The 1993 side, the
compositor and the cards follow the frame itself: FINALE always shows the 2026 picture (the 1993 grid is on
the Studio's CRT, and the retro stage runs only while the Studio is shown), with the 2026 card during a cut
and the 1993 card in the room. A Wipe's line is a strip of glass four pixels wide that shows the 1993 side
refracted and a little brighter, and only the BeatSwap flips of DOTS, GLENZ and RAYTRACE flash, at a quarter
of white.

### The 1993 rasteriser

Polygon and dot effects work as they did on a 486: the CPU transforms the scene once per retro frame (in
`double`), clips each triangle against the near plane (z ≥ 0.1) and against a guard band 512 px beyond each
screen edge, projects it with the 1.2 pixel aspect of a 4:3 display and quantises the vertices to 28.4 fixed
point (`std::lround`). The effect orders its triangles back to front (a sort by depth, or a fixed painter's
order such as FLYBY's separating planes), and the GPU draws them in upload order:

- `retro/polys.comp`: one invocation per pixel over the triangle list, integer edge functions with the
  top-left fill rule, so a mesh is watertight (no pixel drawn twice or missed on a shared edge). Three modes:
  flat (one index), gouraud (the index interpolated exactly in integers and Bayer-dithered) and glenz (the
  face's colour code ORed into the pixel, so overlaps tint without blending).
- `retro/points.comp`: square dots of a few pixels, later points winning through a per-pixel order
  buffer.

A frame holds at most 2048 triangles and 8192 points; the limits and the guard band are checked on every
frame (the stage throws past them, and `retro_registry` checks every retro frame of every effect against
them). The primitive lists live in host-visible buffers per frame-in-flight slot.

### The 1993 effects

- **BOOT** (chapter 0): a DOS prompt types the demo's start-up in light grey on black, 40 columns, with a
  blinking cursor, and cuts to FLYBY with two white frames.
- **FLYBY** (chapter 1): a flat-shaded low-poly ship over a streaming starfield, the camera swooping from
  below and behind to a side view, with its greeting scrolling along the bottom.
- **DOTS** (chapter 2): 512 depth-shaded points morphing cube → sphere → torus → "1993" and back.
- **PLASMA** (chapter 3): the classic plasma, sine waves in three octaves folded round a 224-colour ring that
  the palette rotates one entry per retro frame, until the wipe brings 2026 in.
- **TUNNEL** (chapter 4): a twisting tunnel of 96 dot rings along a winding path, which turns into a solid
  checkered tube halfway through.
- **GLENZ** (chapter 5): Future Crew's 24-triangle glenz vector from Second Reality (Unlicense; credited in
  [`licenses/FUTURE-CREW-NOTICE.md`](licenses/FUTURE-CREW-NOTICE.md)), spinning on three axes and bouncing
  on the chapter's beats, its translucent faces ORed into the palette index over moving copper bars, so the
  bars and the far side show through, tinted.
- **LENS** (chapter 6): a rotozoomer, an original 128×128 checker texture with EVOLUTION stamped from the
  font, swung and zoomed in 16.16 fixed point, under a bouncing glass lens that magnifies 2× at its middle.
- **RAYTRACE** (chapter 7): a "precalculated" ray-traced animation at 10 fps: chrome spheres and a
  glass-coloured one bouncing round a big chrome sphere over a red and white checkerboard, hard shadows, two
  reflection bounces, one ray per pixel, dithered into a fixed 6×6×6 colour cube and 24 greys.
- **VECTOR CITY** (chapter 8): a gouraud-shaded city at sunset, generated from a fixed seed on an endless
  street grid. The camera flies low down the main street towards a striped sun at its vanishing point, then
  climbs over the roofs and banks into a turn as the wipe brings 2026 in; the city fades into the haze with
  distance, so a frame draws about 300 polygons on average (at most about 390): walls, roofs and street
  tiles, plus some 45 lit windows and centre-line dashes, which shrink into the haze before they are cut.
- **FINALE** (chapter 9): a megamix that cuts every two beats between FLYBY, DOTS, TUNNEL, GLENZ, LENS and
  RAYTRACE, with one white flash frame on each cut. It is one registry entry with a resolve hook: for each
  retro frame it hands the stage another effect's entry with a frame of that effect's own (home) chapter and
  a home retro frame, so each sub-effect looks exactly as in its own chapter while the cache stays keyed on
  FINALE.
- **PORTAL** (chapter 10): FLYBY's ship turns away and shrinks into the stars while a beige monitor bezel
  closes in to a 4:3 frame, `THE SCREEN` typed in its corner, before the camera pulls out of the CRT.
- **REBOOT** (chapter 20): BOOT's prompt again with a second script, `evolution.exe /2026` and on to
  `Entering the glass`, until the portal takes the camera through the glass.
- **THE END** (chapter 30): `THE END` centred at three times the font's size, fading from white to black
  through the palette's 64 DAC levels, then black.
- **Scrollers**: CREDITS (chapter 29, now a 1993 chapter) rolls the credits on a sine wave over deep blue
  under a static EVOLUTION title; FLYBY's greeting is the other.

## The 2026 side

### The path tracer

The 2026 side is a path tracer on Vulkan's ray tracing pipeline (`src/rt`, `src/modern`, `shaders/rt`). A
scene's meshes share one vertex and one index buffer; each mesh gets one compacted bottom-level acceleration
structure at setup, and the top-level one is rebuilt every frame from a per-frame-in-flight instance buffer. One
raygen shader loops over the bounces and is the only shader that traces (recursion depth 1; the pipeline
refuses a closest-hit or miss shader that traces); there is one closest-hit shader per material class, which
only evaluates the surface, and two misses, the sky and the shadow ray. The shader binding table layout is
computed from the device's handle size and alignments (lavapipe's and RTX's differ).

- **Materials.** Physically based and authored in code: GGX metal and plastic (Smith height-correlated
  masking, visible-normal sampling, Schlick Fresnel) under an optional clear coat, smooth and rough dielectric
  glass with exact Fresnel, emitters (textured: the CRT samples the 1993 picture), and a procedural checker
  floor; wood planks and brushed metal are procedural patterns. Single-scattering GGX, so very rough metals
  lose some energy.
- **Lights.** Rectangular area lights of three kinds: an emitter, the retro screen (its radiance is the 1993
  picture, sampled through an sRGB view so it is decoded to linear light exactly once, at a mip level from a
  ray cone) and a portal (a window: its radiance is the procedural night sky behind it). Next-event
  estimation picks one light by fixed weights per sample and combines with the BSDF's own sampling by multiple
  importance sampling (power heuristic), portals included. The sky behind a portal is the scene's
  environment.
- **Environments.** Each scene's frame names its sky, a pure function of the direction: `NightSky` (the
  Studio's: a blue gradient, the moon's glow, stars, a town's lights under the horizon), `Space`, `Dusk` or
  `Day`, with an optional sun or moon disc. Rays from the camera and through mirrors see the full sky, its
  stars and town lights drawn as points of fixed flux filtered over the ray's footprint (sharp at 4K, no
  aliasing at 1 spp); next-event estimation, portals and rays after a rough bounce see a smooth lighting sky
  (gradient, glow, disc). Outdoor scenes sample the environment directly, the disc uniformly in its cone and
  the dome by cosine, weighed against the BSDF by multiple importance sampling.
- **Analytic spheres and particles** (`src/rt/spheres.*`, `src/rt/particles.*`, `shaders/rt/sphere.*`). A
  scene may hold exact spheres besides meshes: each sphere is an AABB in a bottom-level acceleration
  structure and an intersection shader solves the ray against it (a robust quadratic and a Newton step, so a
  2 mm sphere a kilometre away is still hit; a ray inside a glass sphere leaves through it). The closest-hit
  shaders are compiled a second time for spheres, so hit groups 4–7 shade spheres with the same four material
  classes; a sphere's colour tints its material and its own emission adds to it. Static spheres are built
  once per scene, one BLAS per range an instance places. The particle field is up to millions of spheres that
  a scene's compute shader makes every frame as a pure function of (particle, time), into a per-frame BLAS
  rebuilt per frame in flight: nothing carries over from one frame to the next, so seeking needs nothing.
- **Integrator.** Pixel jitter and a thin lens; Russian roulette from bounce 3. Noise is cut at its
  sources: after the first rough (non-delta) bounce every lobe is at least 0.25 rough (path regularisation:
  diffuse → mirror → small lamp becomes glossy, which next-event estimation reaches; sampling and MIS see
  the same BSDF), and only light arriving after a rough bounce is clamped, to a luminance of 16 (the Studio 0.5), so a lamp
  seen in a mirror or through glass keeps its full brightness. Emitters flagged camera-only (the PC's LEDs)
  are seen directly and in mirrors but light nothing. Glass with an Abbe number disperses: a path that meets
  it carries one hero wavelength (380–780 nm, Cauchy's IOR, an RGB weight of mean 1), so white light splits
  into a spectrum. A non-finite sample counts as 0 (checked from the float's bits, so a fast-math compiler
  cannot fold the check away).
  The window traces at most 4 bounces, exports 6, in dispatches of 4 samples per pixel, so no single
  dispatch traces more than 4 spp. A whole frame is still one GPU submission of ceil(spp / 4) dispatches,
  though: a 4K export frame at 64 spp is 16 dispatches in one submit, roughly 1–4 s on an RTX 4090 (an
  estimate, not measured), and whether Windows' GPU timeout (TDR, 2 s by default) tolerates that has not been
  measured yet (see the RTX checklist in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)). Every random number is an integer hash of (frame seed,
  pixel, sample, dimension).
- **Denoiser** (`src/post`, `shaders/post`). The tracer writes the lighting without the primary hit's
  emission, plus guides (albedo, normal, depth, motion; the motion of a surface point is where it was in the
  previous frame, carried back by its instance's previous transform or a sphere's `prevCenter`, so a moving
  object keeps its history; the window passes the previous frame's instances while its history continues). The
  lighting is divided by the albedo, reprojected
  into a temporal history (in the window only: rejected where depth or normal disagree, an exponential blend
  that starts as a plain copy after a reset), then filtered by three à-trous passes guided by depth, normal
  and an edge-robust estimate of the noise, and multiplied by the albedo again. The primary hit's emission,
  the CRT's own picture, is added after all that, so the screen is never smeared by the history or blurred by
  the filter. A short history (the first three frames after a reset) gets five à-trous passes; where its
  lighting is sparse (many samples exactly 0) the first pass has no luminance edge stop, so first frames come
  out clean rather than blotchy, at the price of a soft shadow edge there: about 4 pixels wide on a first
  frame (1 on the lit side, 3 on the dark side; energy-neutral, measured in `denoiser_test`), sharp again
  once the history has temporal moments (frame 4). On frames 2 and 3 the noise estimate also counts how
  much the history differs from the frame, so a bright sample of an earlier frame spreads out instead of
  staying a speck. Exports keep no history and filter only below 16 spp; from 16 spp an export firefly filter
  (on by default) clamps each pixel to 4× the second-brightest of its 3×3 neighbourhood instead, which keeps
  anything two pixels wide (CRT text, highlights) and pixels on a geometry edge (a highlight along an edge,
  such as the window sill's, is one pixel wide) but removes lone fireflies. A separate volume layer
  (in-scattered light and transmittance, from the participating media) has its own history and filter, which
  stops at transmittance and depth steps so silhouettes in fog stay sharp, and is composed as
  `transmittance × surface + volume`; a scene without a medium skips it.
- **Trace resolution and the temporal upscaler** (`src/post/upscale.*`, `shaders/post/upscale.comp`). The
  window path traces at `--trace-scale` × its size (0.67 by default, at least 16 pixels per axis) and the
  denoised picture is upscaled to the window size in time: from its second frame after a reset the window
  traces each pixel's samples in a window one output pixel wide, moved over the trace pixel by a 16-frame
  Halton (2, 3) sequence, and the upscaler accumulates them where they fell (each output pixel weights the
  samples near its centre), reprojects its history with the motion guide (the nearest depth's motion in
  3×3), clips it to the current neighbourhood's YCoCg mean ± 1σ and blends by weight (90 % history per
  frame). The primary hit's emission skips the upscaler's history, as it skips the denoiser's, so the CRT is
  always the current frame; below scale 1 it is a Catmull-Rom upsample of the trace and a little softer
  than native. A reset (a seek, a cut, a resize, a trace-scale change) traces whole pixels unjittered and
  shows the spatial Catmull-Rom upsample. At scale 1 there is no upscaler pass; the exports always trace at
  the output size.
- **Tonemap.** Khronos PBR Neutral per frame, then the sRGB encoding and a static per-pixel triangular dither
  into the RGBA8 image the compositor reads; a CPU reference pins it. PBR Neutral keeps the hue: it first
  subtracts a small offset set by the smallest channel (at most 0.04, a toe for dark and saturated colours);
  while the largest channel then stays below the knee at 0.76 that is all, and above it highlights compress
  and move towards white in the plane of the colour and white, so a bright saturated light turns paler but
  never changes hue (ACES turned the CRT's reds orange). White approaches 255 asymptotically: light 64
  rounds to 255 before the dither (about 2 % of its pixels show 254 after it), light 16384 everywhere.

### The Studio

A dark modern room at night (the Studio), all procedural, around a 1993 PC that is the only
1993 object in it: a polished concrete floor saw-cut into slabs and a board-formed concrete wall (the
`Concrete` pattern), plaster walls, an oak desk on brushed steel legs, a slim black window frame, an office
chair, a black desk lamp with its diffuser recessed in a matte shade, and beyond the window a skyline of dark
towers with lit windows under the night sky (481 instances, all counted from the scene). In it: the beige PC
and CRT monitor, a 101-key keyboard (one key-cap mesh, 101 instances), a mouse, floppy disks, a mug, and a
chrome and a glass sphere on small stands beside the monitor, where they reflect and refract the screen.
Five lights: the CRT's screen, the lamp, the window (a portal), the moon (an emitter outside the window,
whose beam falls past the desk onto the floor; gone at RETURN's dawn) and the town's sodium glow (a dim warm
panel below and left of the window, hidden from inside, that throws the window's shape onto the right wall).
The power LEDs and the skyline's windows are camera-only emitters: seen, but lighting nothing (no fireflies).
The surfaces carry their age: dust specks and a fingerprint on the CRT's glass (the `CrtGlass` pattern) and
yellowed key caps, pale in the middle and darker at the edges (`KeyCap`).

The CRT shows the live 1993 picture, barrel-warped, behind curved glass (the coat's shading normal bulges, the
quad stays flat), with a beam profile of 200 lines and, in a 4K close-up, an aperture-grille phosphor mask,
both fading into their mean with distance; in STUDIO NIGHT and RETURN it sits at the DOS prompt the demo
exited to (`C:\DEMO>` with a blinking cursor). It is the room's key light, through a light rig per chapter
(`studioRig`, carried by the frame's material table): in STUDIO NIGHT the lamp is off and the screen bright,
in FINALE and PORTAL the lamp glows at 20 % and the megamix lights the room, in RETURN and REBOOT the lamp
comes back on. The exposure per chapter keeps the colourful chapters' screen within a display gain of about
2.4, where PBR Neutral keeps 1993's colours saturated. The camera follows authored keys per chapter (a pull out
from the screen to a low, long wide shot in FINALE and PORTAL, a low dolly past the chair in the moonbeam and,
from the accent, an orbit round the monitor in STUDIO NIGHT, a push back in in RETURN, a close-up in REBOOT).
`studio_test --look <dir> [spp] [width height] [name]` renders a still of every shot.

### The city and the wormhole

The City (VECTOR CITY, ASCENT, CITY NIGHT, METROPOLIS) is one night city on the 1993 city's grid: towers
under procedural facades whose every window is a cell with its own lit or dark state and a room behind its
glass (3,896,219 cells, counted from the pattern's own bays and floors), wet streets that mirror 41,684
street lamps and 155 aviation beacons, all lights of the light tree, and shop signs lettered in a 5 x 7 dot
font. VECTOR CITY's neon takes the bright ends of the 1993 Gouraud ramps (sodium gold and white) and one red;
ASCENT starts on STUDIO NIGHT's last frame, the Studio's camera carried out of its window to this room's
window, under the same moon; CITY NIGHT lights only the 16,384 lamps nearest its street; METROPOLIS flies 58 m
over its elevated avenue at 200 km/h, high enough that nothing crosses more than 60 px of a 4K frame in a
frame. `scene_city_test --look <dir> [spp] [width height]` renders a contact sheet per chapter
(`EVO_LOOK_HUD=1` adds the cards) and `scene_city_test --cpu` runs its checks without the GPU.

WORMHOLE flies the ship through a gate over an ocean planet into a tube of luminous gas (the `WormholeWall`
pattern: a glacial-blue glow drawn out along the flow, gold filaments, dark dust lanes, its fine octaves
dropped where the ray cone is wider) with basalt rings 1 km apart inside it. Each ring carries 72 gold
lamps, lights of the light tree, so a ring sweeps the hull with light on every beat from D; the sun stands at
the tube's far end, and the last shot, where the ship pulls away into it, carries Act III's one anamorphic
streak. `scene_ship_test --look <dir> [spp] [width height]` renders a still of every shot.

### Bloom and the look

Everything after the trace that the direction sets is a pure function of the show time (`post::lookFor`): no
measured exposure anywhere.

- **Bloom and glare** (`post::Bloom`). The HDR picture is convolved with a lens's point spread function and
  mixed in before the tonemap as (1 − w) I + w (I ⊛ PSF): no threshold, so every pixel spreads in proportion to
  its light, and no energy gained or lost (a constant picture comes through unchanged, a point keeps its
  energy within 0.2 %). The PSF is a soft halo of six Gaussians (a downsample chain to 1/64 of the size and a
  weighted upsample; 97 % of the light within 64 pixels at 1080p) and a faint six-blade diffraction, three line
  blurs at 0°, 60° and 120° carrying 5 % of the bloom; no starburst. A scene's `SceneFrame::anamorphic` adds one
  long horizontal streak (WORMHOLE's white-out only). w is 0.020, 0.025 and 0.035 in Acts I, II and III; on a
  CRT crossing it gives way to the CRT's own glow at the glass and returns by D + 1 bar. Twelve compute passes,
  all but the first at half size or less: an estimated 0.3 ms at 4K on an RTX 4090 (lavapipe: 360 ms at 1080p).
- **The grade per act**, in linear display light after PBR Neutral: Act I a ±2 % warm-shadow/cool-highlight
  split tone and true blacks; Act II an S-curve of slope 1.05 at mid grey and blacks lifted to 0.004 in night
  blue (0.6, 0.8, 1.0); Act III shadows 8 % less saturated and highlights left to clip into the glare. Film
  grain on top: triangular, hashed from the pixel and the 60 fps frame, 0.4 %, 0.6 % and 0.3 % of the
  luminance. STUDIO NIGHT and REBOOT ease from the previous act over two bars (both joins are continuous
  shots); CREDITS and THE END are 1993 only and get none. `SceneFrame::vignette` (at most 0.15, wide shots)
  darkens the corners.
- **Authored exposure** (`post::exposureKeys`): EV offsets on top of each scene's own exposure, eased with zero
  slope at every key and at most 0.05 EV per frame except on a cut: RAYTRACE's last two bars down a stop,
  STUDIO NIGHT up 0.7 from D to the pane, SINGULARITY, WORMHOLE and ASCENDANT into white (+6), RETURN and
  CRYSTAL CITY back out of it, RETURN's rebalance to the screen, METROPOLIS's one bright beat.
- **The palette grade after a crossing.** From the glass of FLYBY, TUNNEL, PORTAL (inward) and REBOOT the 2026
  picture is mixed toward its nearest Oklab entry of the 1993 effect's palette at D: all palette at D, back to
  physically based colour by D + 1 bar.

The tonemap pins all of it against a CPU reference (`tonemapGraded`): the same bytes, except where float
rounding of `pow` decides a tie (about 1 channel in 10⁵).

The denoiser's first frame after a cut treats a neighbourhood whose bright samples stand alone (dim surfaces
lit by few bright paths, like the Studio's walls at night) as sparse, so its first à-trous pass does not weight
them down: STUDIO NIGHT's left wall on that frame is within 6 % of 256 spp (a heavy-tailed test frame was 20 %
dark before, 5 % now).

### Limits worth knowing

- **lavapipe.** The tests and the storyboard run the path tracer in software, which is far too slow for the
  window: below 2 fps every frame counts as a time jump, so the window's accumulation never builds up and it
  shows each frame's single sample, filtered. The real look needs a ray tracing GPU, which has not been tried yet (nor has
  Windows).
- **The first frames after a reset** (a seek, a chapter change or a camera cut) trace 4 and then 2 spp in the
  window and are filtered harder; in sparsely lit areas shadow edges are soft for those two frames. Below
  trace scale 1 the first frame is also the spatial upsample of the smaller trace; detail sharpens over the
  next 16 frames as the jittered samples accumulate.
- **Below trace scale 1 the CRT's picture is upsampled** from the trace (never accumulated, so it stays
  current), a little softer than at `--trace-scale 1`.
- **Exports at 16 spp and more** are not filtered spatially (the firefly filter only), so dark corners keep a
  fine grain at the 64 spp default; more samples (e.g. `--spp 256`) clean them up.
- **Long 4K exports on Windows are unmeasured:** a frame's whole trace is one submission (above), seconds
  long at 4K and 64 spp or more; measure it on the target GPU before a long export.
- PBR Neutral still desaturates the brightest colours towards white (a 1993 red at a display gain above
  about 2.4 comes out pink, hue intact), so the Studio keeps its colourful chapters below that. Its moonlit
  wide shots are lit mostly by light bounced off the moonbeam, which keeps a fine grain at export spp; the
  lights are picked by fixed weights (no spatial importance); the glass sphere's caustic is subtle:
  regularisation blurs it (sharp caustics await the photon maps of step 5's Task 13).

## Storyboard

`tools/storyboard.py` captures every chapter at 20 %, at its accent and at 80 % of its length, once forced
into 1993 and once into 2026, and composes one PNG grid per act (`act1.png`, `act2.png`, `act3.png`): a row
per chapter, labelled with its number, title, transition and times, and six columns (1993 at the three
times, then 2026). Each cell is the capture at half size. It needs numpy and runs the executable once per
cell; all 31 chapters at 640×360 and the default `--spp 8` take just under 2 minutes on lavapipe (1 min 49 s
on a 4-core machine), most of it in the five path-traced Studio chapters.

```sh
cd evolution
python3 tools/storyboard.py --app build/linux/bin/Evolution --out build/linux/build-out/storyboard
# options: --width 640 --height 360 (the capture size; the defaults), --chapters 0,3 (default: all),
# --no-crt (the 1993 picture without the CRT look), --spp 8 (path-traced samples per pixel; the default)
```

The path-traced chapters cost most of the time on lavapipe; `--spp` trades their noise against it (the
app's own export default, 64, is for real exports).

The forcing is the diagnostic environment variable `EVO_FORCE_ERA`: set to `0` or `1`, it renders
`--capture-dir` and `--render-stream` frames wholly in 1993 or 2026 (era 0 or 1, a wipe not started or
finished, no cut flash); a forced `--render-stream` says so once on stderr
(`render-stream: EVO_FORCE_ERA=1 (2026 only)`). Any other value is ignored, and the window never reads it.

```sh
EVO_FORCE_ERA=1 Evolution --capture-dir out --time 50   # PLASMA's chapter, its 2026 side
```

## Usage

Run `Evolution` without arguments for a 1280×720 window that plays the show from the start. Keys:
← / → previous / next chapter (wrapping around the show), 1–9 and 0 jump to chapters 1–10, C toggles
the CRT look of the 1993 picture, H shows or hides the chapter title and the epoch cards, M mutes, F11
toggles fullscreen, Esc quits. The window title shows the chapter and the show time.

```text
Usage: Evolution [options]

Options:
  --width N            Render width in pixels, 16..8192 (default 1280)
  --height N           Render height in pixels, 16..8192 (default 720)
  --fps N              Frames per second for headless output, 1..240 (default 60)
  --time S             Start time on the timeline in seconds, 0..86400 (default 0)
  --spp N              Path-traced samples per pixel per frame, 1..4096 (default 1 in
                       the window, which accumulates them over frames; 64 for
                       --capture-dir and --render-stream, where every frame stands alone)
  --detail F           Detail of the 2026 scenes, 0 < F <= 1 (default 1): particle,
                       splat, voxel and photon counts scale with it (for quick tests)
  --trace-scale F      Path trace at F x the window size, 0.25..1 (default 0.67), and
                       upscale in time; the window and --bench only (the exports always
                       trace at the output size)
  --duration S         Seconds to run; 0 runs until Esc (default 0). --render-stream
                       needs a duration greater than 0
  --headless           Render without opening a window
  --render-stream      Write raw RGB frames to stdout (implies --headless)
  --capture-dir PATH   Write one PNG frame, at --time, to PATH (implies --headless)
  --bench              Benchmark the window's rendering headless: per chapter, render
                       --bench-frames frames from 20 % into it (after 2 warm-up frames)
                       and print one JSON line with the GPU time of every pass
  --bench-frames N     Frames per chapter for --bench, 1..100000 (default 120)
  --chapters LIST      Chapters for --bench, comma-separated indices as --show-info
                       numbers them, e.g. 11,14 (default: all)
  --music PATH         Music WAV to play (default: assets/music/show.wav beside the
                       executable); a WAV of another length than the show is refused
  --no-audio           Run without audio, on the system clock (SILENT MODE)
  --mute               Start muted (M toggles); the music still keeps the time
  --audio-offset MS    Calibrate A/V sync: milliseconds added to the music time the
                       picture follows, -500..500 (default 0); raise it if the picture
                       lags the sound
  --no-crt             Show the 1993 picture without the CRT look (C toggles)
  --no-hud             Hide the epoch cards and the window's chapter title (H toggles)
  --show-info          Print the show's chapters and music as JSON and exit
  --validation         Enable the Vulkan validation layers
  --device-info        Print information about the selected GPU and exit
  --fullscreen         Start the window in fullscreen
  -h, --help           Show this help and exit

Keys (window): Left/Right previous/next chapter, keys 1-9 and 0 jump to chapters 1-10,
C CRT look, H HUD, M mute, F11 fullscreen, Esc quit

Environment (diagnostic): EVO_FORCE_ERA=0 or 1 renders --capture-dir and --render-stream
frames wholly in 1993 or 2026 (no wipe split, no cut flash); other values are ignored
```

Examples:

```sh
# One PNG of the frame at t = 1.5 s: writes and prints out/frame-001.500.png
Evolution --capture-dir out --time 1.5 --width 1920 --height 1080

# Two seconds at 30 fps as raw RGB, straight into ffmpeg
Evolution --render-stream --width 640 --height 360 --fps 30 --duration 2 |
    ffmpeg -f rawvideo -pixel_format rgb24 -video_size 640x360 -framerate 30 -i - out.mp4

# The selected GPU as JSON
Evolution --device-info

# A path-traced frame (STUDIO NIGHT) at 256 samples per pixel instead of the export default of 64
Evolution --capture-dir out --time 203 --spp 256

# The show as JSON: duration, music file, tracks, and each chapter's effect, transition, start, accent and
# 2026 scene ("Placeholder" or the path-traced "Studio")
Evolution --show-info
```

**Interactive and export.** The window is interactive: it path traces `--spp` samples per pixel per frame (1
by default; 4 on the first frame after a reset of its temporal history and 2 on the next, so a cut starts
clean) and accumulates them over the frames since that reset, which happens on a seek, a chapter change, a
camera cut, a resize or a change of settings. The exports (`--capture-dir`,
`--render-stream`) keep no history at all: every frame traces `--spp` samples (64 by default, in dispatches of
4) at up to 6 bounces (the window: 4), filtered spatially (à-trous) only below 16 spp (from 16, a firefly
filter clamps lone bright pixels instead; the window never uses it), and is a pure function
of the options and its show time, so a streamed frame equals a fresh capture of the same time byte for byte.
**Motion blur** is an export-only 180° shutter (`0.5 / --fps` seconds, centred on the frame's time): a frame of
more than 4 samples per pixel is traced in one time slice per dispatch of 4 samples, at most 8, each slice with
its own scene frame, particle field, TLAS and camera, and the picture is their mean (`src/modern/shutter.*`;
the slice times are a pure function of the time, fps and spp). A slice never samples another chapter: its time
is clamped into the frame's own chapter, so the first and last frames of a chapter blur only inwards (and a
scene shown before its chapter starts, REBOOT's look ahead at GALAXY, blurs forwards from its own time). With 4
samples or fewer, and always in the window, there is one slice and no blur. The guides (depth, normal, motion)
come from the middle slice.
The random seed of a frame comes from the show time, `pcg(llround(seconds × 48000))` (the audio sample clock);
the window adds `0x9E3779B9` per frame since its last reset, so its first frame after a reset uses the export
seed and, at equal bounces, equals the export frame at spp max(`--spp`, 4) byte for byte while that is below
16 (where both filter the same way). The bounces cannot be set from the command line (the window 4, exports 6), so a real window frame never
equals a real export frame; only the tests, which set both, see the equality. The window traces below its
size (`--trace-scale`, 0.67 by default) and the exports at the output size, so the equality holds at
`--trace-scale 1`; at a lower scale the window's first frame after a reset equals a frame rendered in Export
mode at the same scale (the tests pin both), and seeking stays deterministic: the jitter and the upscaler's
history follow only the frames since the last reset.

In the window the time comes from the music: the clock is the position the listener hears (what SDL still
has queued and the device's buffer are subtracted), and the show loops at its end. `--audio-offset` shifts
the picture against the sound for outputs with more latency than that. If the music cannot play (no WAV, a
WAV of the wrong length, no audio device, or `--no-audio`), EVOLUTION prints `SILENT MODE: <reason>` on
stderr and runs on the monotonic clock from `--time`; otherwise it prints `AUDIO: <file>`. If the audio
stream fails mid-show, it prints the new `SILENT MODE` status and carries on from the music's position.
`--show-info` needs neither a GPU nor SDL video. The headless modes never play audio and always render at
`t = time + i / fps`. A window that renders below 2 fps (lavapipe at a large size) steps the show more than
0.5 s per frame, so every frame counts as a time jump and resets the 2026 side's temporal history: the
accumulation never builds up there.

`--render-stream` writes frames `0 .. ceil(duration × fps) − 1` at `t = time + i / fps`, top-down,
3 bytes per pixel, with progress on stderr. `--capture-dir` creates missing directories and overwrites
an existing file of the same name. With `--validation`, validation messages go to stderr, so they never
mix with streamed frames.

**Benchmark (`--bench`).** Measures the window's rendering without a window: for each chapter (all, or
`--chapters`), two warm-up frames and then `--bench-frames` frames (120) from 20 % into the chapter,
Interactive at `--width` × `--height`, `--spp`, `--trace-scale` and `--detail` as the window would render
them, two frames in flight. Each chapter prints one JSON line on stdout (progress on stderr):

```json
{"chapter":11,"title":"STUDIO NIGHT","scene":"Studio","frames":120,"spp":1,"traceScale":0.6700,
 "width":2560,"height":1440,"traceWidth":1715,"traceHeight":965,"device":"NVIDIA GeForce RTX 4090",
 "timestamps":true,"gpuMs":{"total":…,"retro":…,"fill":…,"tlas":…,"trace":…,"denoise":…,"upscale":…,
 "tonemap":…,"compositor":…},"fps":…}
```

`gpuMs` are GPU timestamp times per pass, averaged over the timed frames (a pass a frame did not run counts
0): `tlas` the top-level acceleration structure build, `trace` the path tracing dispatches, `denoise`,
`upscale` (0 at scale 1), `tonemap`, `compositor` (CRT, era blend and HUD), `retro` the 1993 stage, `fill`
the placeholder and `total` the whole frame. `fps` counts the timed frames over their wall-clock time. A GPU
whose queue has no timestamps reports `"timestamps":false` and zero times. The per-scene budget (see ARCHITECTURE.md) is measured this way:

```sh
# Every chapter at 1440p as the window renders it (trace scale 0.67), then at scale 1
Evolution --bench --width 2560 --height 1440 > bench-1440p.jsonl
Evolution --bench --width 2560 --height 1440 --trace-scale 1 > bench-1440p-native.jsonl
# Two chapters, fewer frames
Evolution --bench --chapters 11,19 --bench-frames 60
```

Exit codes: 0 on success, 1 for bad usage or a runtime error, 2 when the machine cannot run EVOLUTION
(no Vulkan loader, no Vulkan 1.3 driver, no GPU, or a missing extension or feature; stderr names what is
missing), and 3 when `--validation` reported any warning or error during an otherwise successful run.

### Release and the 4K video

**The video.** `tools/render_video.py` pipes `--render-stream` into ffmpeg and muxes the show's music:
3840 × 2160, 60 fps, 64 samples per pixel, H.264 High at CRF 12 in BT.709 (TV range, tagged), AAC-LC
384 kb/s at 48 kHz, `+faststart`. It renders in 10-second chunks (`--chunk`) into `<out>.parts/`, each written
as `.part` and renamed when complete, and every chunk starts with `--first-frame` so its frames are exactly
those of one long stream. Interrupted, the same command resumes: chunks whose frame count checks out
(ffprobe) are kept. `--codec hevc_nvenc` encodes on an NVIDIA GPU, `--codec ffv1` losslessly into Matroska,
`--start`/`--end` render a range, and `--bench` times one export frame at each chapter's accent and prints the
seconds per frame and an estimate for the whole render. ffmpeg and ffprobe must be on the PATH.

One command configures, builds Release, checks for ffmpeg and renders into `dist/render/`; run it again to
resume:

```sh
tools/render_4k.sh                       # Linux
tools\render_4k.ps1                      # Windows (PowerShell)
tools/render_4k.sh --bench               # estimate first
```

At the export budget (2.5 s a frame at 4K and 64 spp on an RTX 4090) the 42,670 frames take about 30 hours. A
software Vulkan driver (lavapipe) is far too slow for the full video; use it for short ranges and checks.

**Chapters.** `tools/video_chapters.py --exe build/linux/bin/Evolution` prints one `M:SS TITLE` line per
chapter, from 0:00, joining a chapter shorter than 10 s to the next (`0:00 BOOT / FLYBY`).

**The release zip.** `tools/package_release.py --bin build/linux/bin` (or `--bin build/windows/bin --os
windows`) writes `dist/EVOLUTION-v1.0-<os>.zip`: the executable, its shaders, the music in
`assets/music/show.wav`, `EVOLUTION.NFO` (in `assets/`), the licence and the third-party notices, in one
`EVOLUTION/` folder, reproducibly (fixed order and dates).
