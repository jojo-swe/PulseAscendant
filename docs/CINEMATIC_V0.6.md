# Pulse Ascendant v0.6 — on the beat, around the action

Goals of this release: a much stronger image, a camera that flies around the action with a blockbuster feel, a sense of speed for retention, and scene changes on the correct beat.

## Beat-synced editing

`tools/beat_grid.py` measures every beat from the PCM: a 100 Hz log spectral-flux onset envelope, autocorrelation and comb tempo estimation (70–180 BPM, log-normal prior around 120 BPM), dynamic-programming beat tracking and ±6-beat local linear smoothing. The supplied generated tracks drift slowly in tempo (up to roughly ±0.12 s over a few minutes), so a single constant grid would slide off the music. Tracked beats deviate 2–5 ms (90th percentile) from the smoothed result. The bar position of each beat comes from bass onsets and harmonic change in ±24-beat windows; in this four-on-the-floor material, downbeat evidence is weaker than beat evidence.

| Act | Median tempo | Beats | Downbeats |
|:---|---:|---:|---:|
| I · Luminous Flow | 137.6 BPM | 685 | 170 |
| II · Adventurous Uplift | 91.9 BPM | 441 | 113 |
| III · Rigid Arpeggios | 92.2 BPM | 458 | 116 |

`tools/direct_music.py` now chooses every chapter cut from the tracked beats within ±2.5 s of the editorial target. Each candidate is scored by measured attack and persistent spectral change, with small bar-line and 4-bar phrase bonuses, under the existing 5–18 s chapter bounds. Cut times are quantized to 2.5 ms, which is exactly 120 audio samples. All 73 cuts land on tracked beats. The measured attack (±50 ms) at cuts averages 0.64 / 0.62 / 0.67 per act, against 0.25 / 0.38 / 0.29 at the nominal targets. The previous attack-only placement scored higher on this metric because it chased attack peaks between beats.

Inside a chapter, `sampleTimeline` exposes beat phase, bar phase and a **shot**:

- High-drive chapters cut to a new camera setup on every downbeat.
- Mid-drive chapters cut every two bars.
- Calm chapters hold one continuous shot.
- Cuts never fall in a chapter's first bar or its last 0.75 s.

The show has 346 shots in total, about 2.6 s each. `--show-info` reports shots per chapter.

## Camera rig

`cinematicRig()` in `common.glsl` runs after each world's authored camera, so each authored move remains the base path:

- **Orbit.** Each beat-synced shot rotates the eye around the target to a new side (±26°–75°) and height, changes distance and pushes in, and keeps circling at up to ~18°/s with drive. The eye is kept near or above its authored height so floors are respected.
- **Roll.** Shots open on a Dutch angle and bank with the music. High-drive tunnel flights barrel-roll.
- **Lens.** Each shot opens with a 20% crash-zoom that settles in ~0.15 s. The lens punches 4% on beats and 3.5% on low-frequency attacks.
- **Shake.** Kicks and hits add a small noise-driven shake.

Orbit is 0.85–1.0 for object-centric set pieces (engine, sculpture, cosmic, armada, Act II/III set pieces). It is 0–0.3 for tunnels, street flights and causeways, which keep their lane and use roll instead. `speedDust()` streams four camera-space depth layers past the lens on flights. Near layers sweep outward faster (parallax) and stretch radially with velocity. The Vulkan RT path applies the same orbit, roll and lens on the CPU in `hardware_rt.cpp`.

## Image pipeline

- **Letterbox.** 2.39:1 scope, cropped from the 16:9 frame. The scene pass is scissored to the visible band, so the bars cost nothing. `--open-matte` restores 16:9.
- **Render scale.** `--render-scale 0.25..1` renders the 3D scene at reduced resolution. Bloom, streaks, grading and text stay at output size. Contrast-adaptive sharpening restores edges.
- **Lens effects.** Anamorphic streaks (four widening horizontal passes over hot highlights), radial speed blur on cuts and kicks, edge-weighted chromatic aberration.
- **Grade.** Teal/orange split tone, lowered black point, +22% saturation and a post-tonemap S-curve.
- **Beat response.** Exposure punches on beats, a short white flash on cuts, animated film grain.
- Exports no longer show the interactive keyboard hint.

## Worlds

- **Act II fallback.** Pale clay surfaces became dark glossy dielectrics: a wet near-black floor, darker chrome and tinted glass. Also added: two-tap SDF ambient occlusion, a cool rim light, a moody gradient sky with a hot horizon band, and height fog. Raymarching uses a 0.78 step factor with distance-relaxed precision: 128 instead of 144 steps, about 20% faster.
- **City.** SDF ambient occlusion.
- **Armada.** Hull occlusion and a cool rim light.

## Export

`tools/render_chunked.py` renders 30 s chunks with exact frame times to x264 (CRF 15, `veryfast`). Verified chunks are skipped on restart. It concatenates without re-encoding and muxes the combined soundtrack once, then verifies resolution, frame rate, total frame count and audio duration. It needs no GPU encoder. The v0.6 film is 3840×2160 at 60 fps, rendered on CPU (Mesa llvmpipe) at render scale 0.5 using the OpenGL Act II fallback.

## Verification scope

- CTest 4/4 passes on Linux.
- `test_show.py`, `test_music_direction.py` (with a new beat-snap case), `test_music_analysis.py`, `test_reactivity.py` and the new `test_beat_grid.py` (drifting synthetic click track with accented downbeats) pass.
- All 32 scenes/styles render in the smoke test.
- Windows and hardware-RT paths were not run for this revision: the build machine had no GPU.
- Beat placement is measured, not listened to. Subjective synchronization and viewer retention are not claimed.
