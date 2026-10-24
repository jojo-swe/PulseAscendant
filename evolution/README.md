# EVOLUTION

EVOLUTION is a real-time demo that puts 1993 and 2026 in the same frame: every effect first appears as
it looked on a 486 and then as modern hardware can render it, with Vulkan 1.3 and hardware ray tracing
as a hard requirement. It is a standalone project inside PulseAscendant; the design spec and the
implementation plans live in [`docs/plans/`](docs/plans/), starting with
[`2026-10-21-evolution-design.md`](docs/plans/2026-10-21-evolution-design.md).

> **Status:** steps 0–3 of the plan are done. The music drives a timeline of 31 chapters in three acts, and
> every chapter with a 1993 side (act I, REBOOT, CREDITS and THE END) shows its 1993 effect through its own
> pipeline (below). The act II and act III planetary chapters are 2026 only (forced into 1993 they show a grey
> test pattern). On the 2026 side, step 4 path traces the Studio in FINALE, PORTAL, STUDIO NIGHT, RETURN and
> REBOOT; the other 2026 chapters show a placeholder gradient until step 5.

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

The 49 tests run headless (they also pass on lavapipe) and need Python 3 (`python_tools` and `storyboard`
also numpy). Every GPU test runs with the validation layer on and fails on any message it reports, except the
one `context` provokes on purpose to check that messages are counted.
- The show and its music: `options`, `timeline`, `era`, `clock`, `audio_transport` and `show_info`.
- The render foundation: `png`, `png_decode`, `context`, `gfx_foundation`, `renderer`, `swapchain`, `font`,
  `text_pass` and `no_wait_idle`.
- The 1993 side: one test per effect (`boot`, `plasma`, `scroller`, `flyby`, `dots`, `tunnel`, `glenz`,
  `rotozoom`, `raytrace`, `city`, `finale`, and `closing` for PORTAL, REBOOT and THE END), `raster` (the polygon
  and point rasterisers against a CPU reference) and `retro_registry` (every effect a pure function of its retro
  frame, every scene within the rasterisers' limits).
- The Studio: `studio` (STUDIO NIGHT lit but not blown out, the CRT lighting the room, the screen following
  the 1993 picture on the very next window frame, the cameras and their cuts, the scene, the DOS prompt).
- The executable end to end, its exports at `--spp 2`: `render_stream`, `capture`, `stream_capture` and
  `stream_capture_studio` (a frame of a stream equals a fresh capture at the same time, for every chapter; the
  Studio chapters, path traced, in a part of their own that runs beside the rest), `export_mode` (the seeds,
  export purity and the window's first frame after a reset equal to an export frame), `storyboard` (into
  `build-out/storyboard_test`, so it never touches the storyboards below), `exit_codes`, and `python_tools`
  (the music analysis pipeline).

The `linux` test preset runs 8 tests at a time; `ctest --preset linux -j 1` runs them one by one.

The three that open a window, `windowed`, `windowed_unsupported` and `silent_mode`, are off by default;
configure with `-DEVO_RUNTIME_TESTS=ON` to run them under `xvfb-run`.

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

The soundtrack is three tracks generated with Suno by joyolo, one per act, played back to back without
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

## The 1993 pipeline

Every frame is rendered by compute passes into one RGBA8 image at the output size, which the window shows and
the exports read back:

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
3. **Modern stage.** The 2026 side, in HDR light tone mapped once (ACES). FINALE, PORTAL, STUDIO NIGHT,
   RETURN and REBOOT path trace the Studio: a dark room at night around a 1993 PC on a wooden desk, whose CRT
   shows the live 1993 picture (barrel-warped, with scanlines) and lights the room, with a warm desk lamp, a
   moonlit window onto a procedural night sky, a 101-key keyboard, a chrome and a glass sphere, all built in
   code. The path tracer's output goes through the denoiser (temporal in the window, spatial à-trous) and the
   CRT's picture bypasses both, so it stays sharp and current. In STUDIO NIGHT and RETURN the CRT sits at the
   DOS prompt the demo exited to. Every other 2026 chapter shows a placeholder gradient in the chapter's hue
   with a circle on kick and beat until step 5.
4. **Era compositor.** Blends the two by the timeline's era: a Cut or BeatSwap chapter shows one era whole,
   a Wipe sweeps 2026 in from the left behind a white seam, a Portal cross-fades for now, and a cut flashes
   white.
5. **HUD.** The epoch card bottom-left (both cards during a wipe), in the demo's own 8×8 font with a drop
   shadow, scaled with the output in whole steps (one step per 360 lines: 2× at 640×360, 4× at 1280×720,
   8× at 2560×1440, at 1× of the step when a card would not fit). The window also shows the chapter title
   top-left as a development aid; captures and `--render-stream` never do. Key H or `--no-hud` hides it.

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

## Storyboard

`tools/storyboard.py` captures every chapter at 20 %, at its accent and at 80 % of its length, once forced
into 1993 and once into 2026, and composes one PNG grid per act (`act1.png`, `act2.png`, `act3.png`): a row
per chapter, labelled with its number, title, transition and times, and six columns (1993 at the three
times, then 2026). Each cell is the capture at half size. It needs numpy and runs the executable once per
cell; all 31 chapters at 640×360 and the default `--spp 8` take about 2 minutes on lavapipe, most of it in
the five path-traced Studio chapters.

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
  --duration S         Seconds to run; 0 runs until Esc (default 0). --render-stream
                       needs a duration greater than 0
  --headless           Render without opening a window
  --render-stream      Write raw RGB frames to stdout (implies --headless)
  --capture-dir PATH   Write one PNG frame, at --time, to PATH (implies --headless)
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
by default) and accumulates them over the frames since the last reset of its temporal history, which resets on
a seek, a chapter change, a camera cut, a resize or a change of settings. The exports (`--capture-dir`,
`--render-stream`) keep no history at all: every frame traces `--spp` samples (64 by default, in dispatches of
4) at up to 6 bounces (the window: 4), filtered spatially (à-trous) only below 16 spp, and is a pure function
of the options and its show time, so a streamed frame equals a fresh capture of the same time byte for byte.
The random seed of a frame comes from the show time, `pcg(llround(seconds × 48000))` (the audio sample clock);
the window adds `0x9E3779B9` per frame since its last reset, so its first frame after a reset uses the export
seed and, at equal spp and bounces, equals the export frame byte for byte.

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

Exit codes: 0 on success, 1 for bad usage or a runtime error, 2 when the machine cannot run EVOLUTION
(no Vulkan loader, no Vulkan 1.3 driver, no GPU, or a missing extension or feature; stderr names what is
missing), and 3 when `--validation` reported any warning or error during an otherwise successful run.
