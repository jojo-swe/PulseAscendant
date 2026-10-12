# v0.6 local preview — let the music move the scene

The owner rejected v0.5's perceived pace and weak musical response. Publication
and its delivery automation remain paused. This revision is available locally
for listening and viewing before any new full-show export.

## Cause and change

The saved 100 Hz low/mid/high attacks reached the renderer, but numerous camera,
traffic and machinery animations still used elapsed chapter time. Several hero
responses changed radii by only 0.055 units or scale by 6.5%; much of the response
was confined to emissive detail. A nonzero pixel difference did not establish
that a viewer could see or feel the rhythm.

The existing PCM analysis is unchanged. The revision integrates sustained energy
and squared low/mid attacks into velocity. A new local `travelSeconds` clock
drives fly-throughs, machinery, trains, traffic and particles. It accelerates on
actual recorded attacks and decelerates between them without reversing or
teleporting. Seeking and exact-time exports reproduce the same state.

Finite camera paths use normalized integrated motion: they surge with the music
but retain their authored endpoints and chapter lengths. They do not acquire a
greater average path speed. Other travel and machinery clocks do run faster.
Longer-scale transformations and collisions retain their original musical
accent times, separately from the faster movement clock.

Reactor crystal scale now reaches +24% on bass attacks instead of +6.5%; the
sculpture's radius displacement increases from 0.055 to 0.32. Engines, pistons,
cosmic matter, leaf/wing articulation and RT geometry also receive larger local
responses. Both OpenGL and the loaded Vulkan SPIR-V apply a 14% bass and 4.5% mid
optical push, making silhouettes react before bloom and tone mapping.

All three songs retain their original speed, order and total duration, 888.68 s.
The 76 chapter boundaries and 16 worlds are preserved. Act II remains the
hardware RT act; Acts I and III remain OpenGL. `--pulse-gain 0` disables direct
transient effects, while integrated music-driven motion stays active.

## Review and evidence

An independent read-only reviewer scored the corrected code **8/10**. Their
three findings were corrected: the loaded RT shader binary must match its
source; the faster train/causeway cameras require continuous tracks; and the
UPLIFT camera must target the exact model height rather than a differently timed
camera progress value. No remaining demonstrated code fault was reported.

- Regression first failed on v0.5's insufficient speed range, then passed.
- Native core tests check forward/seek-stable motion, musical acceleration and
  all camera endpoints. Measured mean local travel speed on strong low attacks
  is 5.62 / 5.69 / 5.69 versus 2.01 / 2.06 / 1.90 during low-attack troughs in
  Acts I / II / III. These are animation-clock units per second.
- Windows: six CTest checks pass. Final RT with validation and OpenGL tours each
  render all 152 chapter samples without reported rendering errors.
- Linux under WSLg: native compilation and all 152 OpenGL tour samples pass.
  Five of six CTest checks pass; the lifecycle check fails with `Minimized state
  not observed`, including an isolated retry. Window minimization remains an
  unresolved platform test, and Linux must not be described as fully accepted.
- An exact-time OpenGL transient-on/off comparison reaches all 16 worlds with a
  substantial image footprint. This is evidence of coupling, not proof of taste.
- The 42 s native 1080p60 preview contains seven excerpts across all acts, with
  2,520 frames. It and the 1920x600 side-by-side comparison fully decode without
  errors. All seven source-audio checks correlate above 0.9998 with 0 ms measured
  offset. PCM is assembled once before final AAC encoding to avoid clip padding.
- Contact strips were inspected for all seven comparison excerpts and all 16
  worlds. They show stronger mechanism/exhaust motion and retained composition;
  they do not substitute for watching and listening in real time.

Short Windows playback samples on RTX 4080 reached 408 fps for the reactor at
1080p, 47 fps for the RT foundry at 720p (23 fps at 1080p), and 426 fps for the
seed fly-through at 720p. These are three-second uncapped samples, not full-show
performance guarantees. Offline preview remains a fixed 60 fps independently
of interactive render throughput.

Evidence is under `build/kinetic-v06/`; playable previews and packages are under
`dist/kinetic-v06/`. Reproduce the preview using
`tools/render_music_comparison.py --help`, supplying the immutable v0.5 movie.

This is a scoped pace/response improvement. It does not claim AAA finish,
increased audience retention or human acceptance. No new full 4K movie has been
rendered or published for v0.6.
