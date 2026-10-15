<div align="center">

<img src="assets/branding/pulse-ascendant.svg" alt="Pulse Ascendant — three acts, sixteen worlds, one pulse" width="100%">

### Follow the pulse. Fly through sixteen worlds.

A standalone music-driven 3D demo for **Windows & Linux**.<br>
Procedural worlds. Moving machinery. Stellar storms. Hardware ray tracing.

[![Release v0.5.0](https://img.shields.io/badge/release-v0.5.0-25d9ff?style=for-the-badge&labelColor=101827)](https://github.com/jojo-swe/PulseAscendant/releases/tag/v0.5.0)
[![Windows and Linux](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux-86ebff?style=for-the-badge&labelColor=101827)](#launch-the-demo)
[![Vulkan ray queries](https://img.shields.io/badge/Act_II-Vulkan_Ray_Queries-f7b84e?style=for-the-badge&labelColor=101827)](#inside-the-renderer)
[![Code license MIT](https://img.shields.io/badge/code_license-MIT-a6b4d0?style=for-the-badge&labelColor=101827)](LICENSE)

**[▶ Watch the 1080p60 preview](https://github.com/jojo-swe/PulseAscendant/releases/download/v0.5.0/PulseAscendant-Preview-1080p60.mp4)**
&nbsp; · &nbsp; **[↓ Download the demo](https://github.com/jojo-swe/PulseAscendant/releases/tag/v0.5.0)**
&nbsp; · &nbsp; **[Svenska](docs/README.sv.md)**

</div>

---

| Three acts | Sixteen worlds | Seventy-six chapters | One continuous soundtrack |
|:---:|:---:|:---:|:---:|
| Three distinct visual identities | From neon cities to living architecture | Authored camera paths and transformations | **14:48.68** of music at its original speed |

## See the worlds

[![Six actual v0.5 captures: Neon City, Orbital Armada, Supernova, Prism Chamber, Seed Velocity and Final Ascent](docs/images/v0.5.0-energy.jpg)](https://github.com/jojo-swe/PulseAscendant/releases/download/v0.5.0/PulseAscendant-Preview-1080p60.mp4)

<p align="center"><sub>Actual v0.5 native captures. Click the gallery for a 2:40.75 preview with the original music.</sub></p>

## The journey

| **I · Full Throttle** | **II · Adventurous Uplift** | **III · Reality Ascendant** |
|:---|:---|:---|
| Neon flight, folded machines and orbital fleets. | Rain, chrome, moving foundries and refractive glass. | Desert colossi, luminous vegetation and a crystal-wing finale. |
| **8 worlds · 31 chapters** | **4 worlds · 21 chapters** | **4 worlds · 24 chapters** |
| `00:00 → 05:00` | `05:00 → 09:49.92` | `09:49.92 → 14:48.68` |
| OpenGL procedural rendering | Vulkan hardware RT + OpenGL fallback | OpenGL procedural rendering |
| ♫ *Luminous Flow* — five-minute edit | ♫ *Adventurous Uplift* — full track | ♫ *Rigid Arpeggios* — full track |

<details>
<summary><strong>Explore all 16 worlds</strong></summary>

**Act I** — Photon Corridor · Plasma / Gyroid · Crystal Engine · Neon City · Fracture Machine · Cosmic Reactor · Quantum Ocean · Orbital Armada

**Act II** — Chrome Cathedral · Rain District · Orbital Foundry · Prism Chamber

**Act III** — Dune Colossus · Verdant Singularity · Gravity Rebellion · Reality Ascendant

Each return to a world has its own staging and transformation. Read the [Act II notes](docs/ACT_II.md) and [Act III notes](docs/ACT_III.md) for the implementation.

</details>

## Feel the music

The music drives geometry, light, surfaces and moving mechanisms. **24 frequency bands** provide sustained motion and color; a separate **100 Hz attack layer** responds to low, mid and high frequency transients. Both interactive playback and export follow the same saved analysis and audio clock.

**New in v0.6:** every scene change lands on a tracked beat, and high-energy chapters cut to a new camera angle on each downbeat (346 shots). A flying camera rig circles the action, crash-zooms on cuts, barrel-rolls through tunnels and punches on the beat, with parallax speed dust. The image gains a 2.39:1 scope frame, anamorphic streaks, speed blur, a blockbuster grade and darker, glossier Act II materials. [Read the v0.6 notes →](docs/CINEMATIC_V0.6.md)

**v0.5:** faster authored cameras, travelling energy, working machinery, fleet escorts, articulated vegetation and stronger city/fleet/stellar compositions. The transient response now stays distinct from sustained loudness. [Read the rework notes →](docs/ENERGY_REWORK.md)

Turn up the response when launching the executable:

```sh
PulseAscendant --pulse-gain 1.5
```

Use `./PulseAscendant` on Linux or `.\PulseAscendant.exe` in Windows PowerShell. Sensitivity defaults to **1**, with a range of **0–2**. Setting it to zero removes direct transients; spectrum, sustained energy and integrated musical motion remain active. These are frequency-band attacks, not separated instrument stems or a fixed metronome.

## Launch the demo

| | Download | Start |
|:---|:---|:---|
| **Windows x64** | [Windows ZIP](https://github.com/jojo-swe/PulseAscendant/releases/download/v0.5.0/PulseAscendant-Windows-x64.zip) | Extract everything, then run `PulseAscendant.exe`. |
| **Linux x64** | [Linux tar.gz](https://github.com/jojo-swe/PulseAscendant/releases/download/v0.5.0/PulseAscendant-Linux-x64.tar.gz) | Extract everything, then run `./PulseAscendant`. |
| **Source + music** | [Complete source ZIP](https://github.com/jojo-swe/PulseAscendant/releases/download/v0.5.0/PulseAscendant-source.zip) | Includes all three original MP3s and four runtime WAVs. |

Keep the complete `assets/` directory beside the executable. The demo starts at **1280×720**, loops the full show and needs no account or network connection.

- **Windows:** Windows 10/11 x64 and an OpenGL 3.3 driver. SDL and the MSVC runtime are statically linked.
- **Linux:** built for Ubuntu 24.04 x64, GCC 13 and glibc 2.39, with a working X11/Wayland display and OpenGL driver.
- **Hardware RT:** a compatible Vulkan driver with KHR acceleration structures and ray queries. Unsupported devices use the separate OpenGL fallback; an Act II badge identifies the active renderer.

<details>
<summary><strong>Keyboard controls</strong></summary>

| Key | Action |
|:---|:---|
| **F1 / F2 / F3** | Jump to Act I / II / III, including its music |
| **1–8** | Act I worlds |
| **9 / 0 / - / =** | Act II worlds |
| **Q / W / E / R** | Act III worlds |
| **← / →** | Previous / next chapter |
| **Tab** | Modern / 320×200 retro rendering |
| **M** | Mute; music and animation clocks keep advancing |
| **F11** | Fullscreen / window |
| **Esc** | Exit |

</details>

## Inside the renderer

| Layer | What it does |
|:---|:---|
| **OpenGL 3.3** | Procedural worlds in Acts I and III, plus a separate SDF/analytic fallback for Act II. |
| **Vulkan KHR ray queries** | Triangle BLAS/TLAS, animated instances, traced shadows, reflections and refraction in Act II. |
| **Music analysis** | 24 logarithmic FFT bands from 35 Hz–18 kHz at 20 Hz, plus independent low/mid/high attacks at 100 Hz. |
| **Direction** | 76 authored chapters cut on tracked beats (measured tempo, drift-following beat tracker), plus beat-synced camera shots on downbeats. |
| **Motion** | A deterministic, forward-moving musical clock for selected secondary animation; separately authored camera paths. |
| **Camera** | Shared cinematic rig: beat-synced orbit setups, crash-zooms, banking and barrel rolls, lens punches, parallax speed dust. |
| **Presentation** | HDR composition, 2.39:1 scope frame, bloom, anamorphic streaks, speed blur, split-tone grading, film grain, captions and an optional retro rendering mode. |

Use `--no-rt` to select the fallback, `--require-rt` to require hardware tracing, or `--rt-validation` for diagnostics when Vulkan validation layers are installed.

<details>
<summary><strong>Rendering and soundtrack details</strong></summary>

Five reusable meshes form the RT scenes within a 2,048-instance budget. Ray traversal allows up to five surface encounters. Modern RT uses two deterministic subpixel samples and an adaptive edge filter; retro uses one sample per pixel. RT output passes through host readback into an OpenGL HDR texture each frame.

The renderer is not a full path tracer and does not provide general global illumination or temporal denoising. Resolution and scene complexity affect performance: **an exported 4K60 video does not imply interactive 4K60 performance**. [Rendering and provenance →](docs/SOURCE_MAP.md)

All music plays at its original speed. Act I preserves the first 299 decoded seconds sample for sample, then fades for one second to its exact 300-second boundary. Acts II and III retain every decoded sample. A combined stereo 48 kHz PCM16 WAV supplies a continuous audio clock with sample-exact joins.

Chapter cuts land on tracked beats within ±2.5 seconds of editorial targets, preferring strong measured attacks, bar lines and phrase starts, with 5–18 second chapter bounds. Subjective musical synchronization still needs listening.

</details>

## Checked, measured, reviewed

| v0.5 evidence | Result |
|:---|:---|
| Windows / Linux CTest | **6/6 passed on each platform** |
| Windows RT / Windows fallback / Linux WSLg tours | **152/152 chapter samples on each path** |
| Exact-time transient A/B comparison | Visible pixel changes in **all 16 worlds** |
| 1080p60 preview | **9,645 frames**, complete decode passed; **13 audio windows** measured zero offset |
| Independent review | Code **8.0/10** · Visual finish **7.2/10** · Sampled pace **7.3/10** |

These checks cover the documented builds and samples; physical Linux hardware RT and full-show audience retention remain unverified. See the [verification report](docs/VERIFICATION.md), [independent review](docs/QUALITY_REVIEW.md) and [release checksums](https://github.com/jojo-swe/PulseAscendant/releases/download/v0.5.0/SHA256SUMS.txt).

> **Film status:** the v0.5 1080p60 preview is available now. The full v0.5 4K60 export is complete and verified; YouTube publication is pending the earlier upload limit. Earlier YouTube videos show earlier versions: [v0.4 Short](https://youtube.com/shorts/uOhjmoLY6DI) · [v0.3 full film](https://youtu.be/b5ShsKNC7KM).

## Make it your own

The code, shaders, analysis tools and capture pipeline are included. Start from the **complete source ZIP** if you want the supplied soundtrack: a plain Git checkout intentionally omits audio.

<details>
<summary><strong>Build on Windows or Linux · dependencies · regenerate music analysis</strong></summary>

### Build from source

The release source bundle, `PulseAscendant-source.zip`, includes all three original MP3s, three per-act runtime WAVs, the combined WAV and generated analysis. A Git checkout intentionally omits audio. Restore **all four WAVs** before running the full checks: CTest reads Act I and the combined show, while the standalone audio checks also compare Acts II and III. Runtime binary packages include only `assets/music/second-reality-three-acts.wav`; copying that file alone is insufficient for full source validation.

SDL 3.4.18 is fetched from a pinned commit with a verified SHA-256. Vulkan headers are located from the SDK or fetched from a pinned release. The normal executable loads Vulkan dynamically and can start in fallback mode without a Vulkan loader. A runtime shader compiler is not required because compiled SPIR-V ships with the assets.

Windows, Visual Studio 2026 and CMake 3.25+:

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
./tools/package.ps1
```

Ubuntu 24.04:

```sh
sudo apt install build-essential cmake pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev libasound2-dev libpulse-dev
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
bash tools/package.sh build/linux
```

For offline dependency reuse, configure with `-DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL` and a local Vulkan header include path via `REMIX_VULKAN_INCLUDE_DIR`. Add `-DREMIX_RUNTIME_TESTS=ON` to register tests that require a working display. The `remix_assets` build target synchronizes shader changes beside the executable, including shader-only builds.

To recreate audio, FFT data and directed cues, install ffmpeg, Python and numpy, then run:

```sh
python tools/prepare_show.py "Luminous Flow.mp3" "Adventurous Uplift.mp3" "Rigid Arpeggios.mp3"
```

The script checks source hashes, prepares the three runtime tracks, combines them without gaps and regenerates per-act analysis, transient response and cue direction. With the original files already under `music/source/`, it also works without arguments. The PowerShell entry point is `tools/prepare_music.ps1`.

After editing the Vulkan compute shader, rebuild and validate its shipped SPIR-V with Vulkan SDK tools:

```sh
glslangValidator -V -Os --target-env vulkan1.2 assets/shaders/rt_scene.comp -o assets/shaders/rt_scene.comp.spv
spirv-val --target-env vulkan1.2 assets/shaders/rt_scene.comp.spv
```


</details>

<details>
<summary><strong>Capture, export and verification commands</strong></summary>

### Capture and export

Interactive playback prepares the graphics pipelines before starting the music. The window title shows progress; driver-dependent preparation can take over a minute on WSLg. Escape cancels preparation. This prevents deferred first-draw compilation from consuming the opening music.

`PulseAscendant --show-info` prints the three acts, measured durations, audio asset and all 76 directed chapter starts as JSON without initializing a display.

```powershell
build/windows/Release/PulseAscendant.exe --smoke-test --no-audio --capture-dir build/captures
build/windows/Release/PulseAscendant.exe --tour-test --no-audio --capture-dir build/tour
python tools/storyboard.py --help
python tools/render_video.py --app build/windows/Release/PulseAscendant.exe --require-rt --output dist/three-acts/PulseAscendant-4K60.mp4
```

Smoke mode requests all 16 worlds in both styles; tour mode captures 18% and 82% through each chapter. Use `storyboard.py` to group these images for inspection. The historical `verify-motion.py` thresholds were designed for earlier capture timings and are not a current acceptance gate. Capture success and pixel differences do not establish artistic acceptance. See [current evidence](docs/VERIFICATION.md).

Additional audio and music-direction checks need the complete source audio plus Python and numpy:

```powershell
python tests/test_show.py --app build/windows/Release/PulseAscendant.exe
python tests/test_music_analysis.py
python tests/test_music_direction.py
```

Without NVENC, `tools/render_chunked.py --app <executable> --work <dir> --output <new.mp4>` renders resumable 30-second x264 chunks (default 3840×2160/60, `--render-scale 0.5`) and verifies the concatenated film. Add `--open-matte` to the executable for 16:9 instead of 2.39:1.

The exporter reads `--show-info`, checks the combined music's format and sample count, rejects ranges outside 888.68 seconds, and validates the encoded resolution, frame count, frame rate and audio duration. It needs Python, ffmpeg/ffprobe and NVIDIA H.264 NVENC. A short preview can add `--start 589.92 --duration 5`. Existing outputs are protected; choose a new filename for another render. Packages are written under `dist/three-acts/`.

The native `--render-stream --fps N` option writes bottom-up RGB24 to stdout and diagnostics to stderr. In this mode, `--duration` is timeline length, not wall-clock runtime.

`tools/render_short.py --app <executable> --font <bold.ttf> --output <new.mp4>` creates a 32-second 1080×1920/60 trailer from seven native camera excerpts. It places montage cuts on measured Act III music attacks, constrains every segment to its five-second source, and identifies cached renders by SHA-256 of the executable and its assets. Trailer revision 2 removes a weak city-camera excerpt following visual review. It uses a 70–102 s excerpt of Rigid Arpeggios; the full demo retains the complete three-act audio. The original v0.3.0 source archive preserves trailer revision 1; current Git source and the separate trailer-v2 source supplement contain the updated generator.


</details>

---

## Credits and licensing

**Pulse Ascendant** has its own identity and new choreography. Its history includes a selective reimagining of Future Crew's *Second Reality* (1993). Future Crew receives credit for the original demo, selected effect ideas and the 14-vertex / 24-triangle glenz geometry. [Original source](https://github.com/mtuomi/SecondReality), reference commit `071a82eddb7a7390dbaa491a8a473e0e923a55c2`, Unlicense/public domain. This is not an official Future Crew release or a complete port. The original source and prior releases remain preserved.

Khronos Vulkan headers carry Apache-2.0 and MIT notices; the applicable notices and Apache license text are bundled under `licenses/`.

New code uses the [MIT license](LICENSE); SDL uses its zlib license and bundled notices. **Music is not MIT-licensed.** Luminous Flow, Adventurous Uplift and Rigid Arpeggios were generated with Suno, carry artist metadata **joyolo**, and were supplied and requested for publication by the project owner. Their applicable rights remain separate. See [music credits and source hashes](licenses/MUSIC-NOTICE.txt), the receipts in `music/`, and [Suno's terms](https://suno.com/terms-of-service).

<div align="center">

**Three acts. Sixteen worlds. One pulse.**

[Download](https://github.com/jojo-swe/PulseAscendant/releases/tag/v0.5.0) · [Watch the preview](https://github.com/jojo-swe/PulseAscendant/releases/download/v0.5.0/PulseAscendant-Preview-1080p60.mp4) · [Svenska](docs/README.sv.md)

</div>
