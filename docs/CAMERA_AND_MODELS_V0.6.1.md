# Pulse Ascendant v0.6.1 — a calmer camera and rebuilt hero models

In v0.6 the camera looked stuttery even though performance was fine, and the ship, walker and insect models needed to be better.

## Why the camera stuttered

The v0.6 rig let single drum hits move the camera:

- **Angles multiplied by a fluctuating level.** The orbit used `shotSeconds × (0.16 + 0.2·drive)`. `drive` included the kick and snare, so every kick changed the angle by an amount that grew with shot time (several degrees a few seconds into a shot). Tunnel barrel rolls used the same pattern and could jump by more than a radian.
- **Speed dust on the musical motion clock.** Its phase was `motionTime × velocity(drive)`, so each kick shifted the whole dust field.
- **Kick shake and lens punches.** High-frequency noise shake on kicks and hits, a 4% lens punch on every beat, a 3.5% punch on low attacks, and a 20% crash-zoom on every shot cut.
- **Cuts on every downbeat** (about 1.75 s at 137 BPM) in high-drive chapters.

## What changed

The timeline now provides camera-only signals that contain no transients (`TimelineFrame::cameraDrive`, `shotDrive`, `flowSeconds`):

- `cameraDrive` is the drive smoothed with zero-phase exponential passes (about 0.7 s effective width).
- `shotDrive` is its mean over the chapter, so it stays constant inside a chapter.
- `flowSeconds` is a motion clock integrated from smoothed speed. It drives speed dust, tunnel particles and flight streaks, so they glide instead of surging on each kick.

`cinematicRig()` (GLSL) and the Vulkan RT camera now apply the same rules:

- Each shot orbits at a constant rate of 5–13°/s, chosen once per shot.
- Bank is slow, and tunnel barrel rolls turn at one constant rate.
- A slow handheld drift replaces the kick shake.
- One 1% lens swell per beat, eased in over 60 ms.
- Cuts settle with a 5% zoom instead of a 20% crash-zoom.
- Beat-synced cuts happen every **two bars** in high-drive chapters and every **four bars** in mid-drive chapters. The show now has 196 shots instead of 346.
- Act III no longer shakes the camera on hits.
- The composite's radial blur and chromatic aberration respond much less to kicks.

Music still drives light, geometry and machinery as before.

Measured on 8-second, 60 fps segments in six chapters across all three acts (480×270 global phase correlation, shot cuts excluded):

| Segment | v0.6 jerk | v0.6.1 jerk | v0.6 change jitter | v0.6.1 change jitter |
|:---|---:|---:|---:|---:|
| Reactor Overdrive 64 s | 0.656 px | 0.004 px | 0.260 | 0.162 |
| Orbital Armada 104 s | 1.553 px | 0.000 px | 0.215 | 0.152 |
| Full Throttle 241 s | 0.931 px | 0.034 px | 0.135 | 0.099 |
| Liquid Chrome (RT) 382 s | 0.968 px | 0.338 px | 0.259 | 0.178 |
| Dune Colossus 600 s | 2.235 px | 0.021 px | 0.212 | 0.116 |
| Crystal Wings 684 s | 1.809 px | 0.000 px | 0.266 | 0.134 |

`core_tests` now checks that camera drive changes by less than 0.004 per frame, that `shotDrive` stays constant inside a chapter, and that the flow clock's frame-to-frame speed change is under 10% of the transient motion clock's.

## Rebuilt models

All models remain signed-distance fields in the existing shaders. Each uses a bounding test, so the extra detail costs little away from the model.

- **Interceptors (Orbital Armada):**
  - blended fuselage with a needle nose and dorsal spine, side intakes
  - swept anhedral wings with wingtip cannons, canted twin fins
  - twin nacelles with recessed nozzles and tapered exhaust plumes
  - a tinted glass canopy (new material 4)
- **Carrier:**
  - split ram prow, ventral keel, armour belts and flank trenches
  - a second deck tier, rows of turrets with twin barrels
  - a layered command tower with masts and a sensor dish
  - four engine housings with nozzle bells
- **Dune Colossus walker:**
  - armoured quadruped chassis with dorsal plates
  - a sloped command head with a light slit and chin cannons
  - side vents that recoil on the kick, and a sensor mast
  - four two-bone IK legs: hip housings, pistons, knee joints, tapered shins with power lines, clawed foot pads
  - a four-beat walk whose stance feet move with the ground
- **Crystal moth:**
  - head with compound eyes, thorax and a six-segment abdomen
  - six folded legs and glowing feathered antennae
  - moth-shaped forewings and swallowtail hindwings with an iridescent membrane (material 2.4)
  - light veins, rims and eyespots inlaid in each wing
- **Titan Overdrive launch field:** the stretched octahedra are now rockets, each with a tapered hull and needle nose, three swept fins, a nozzle bell and a light band.

Act III adds a smooth machined-armour / chitin material (1.4) without the monument tile grid.

## Verification

- Windows build with an RTX 4080. `core_tests` passes every timeline and camera check; it then stops at the per-act WAV check, because a Git checkout has no audio.
- `act3_plan_test` and `audio_tests` pass.
- Tour mode passes: 152/152 exact-time captures with Vulkan RT in Act II.
- The full 4K60 film was rendered with `tools/render_video.py --require-rt`.
