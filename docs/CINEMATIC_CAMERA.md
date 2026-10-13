# v0.7 — forward motion with a steadier camera

The v0.6 camera felt too sensitive: individual attacks changed focal length by
up to 18.5 percent and sharply changed travel velocity. Its strongest measured
60fps normalized speed step was 1.60561. One Act III stairwell also aimed at a
discrete `floor()` target. These are camera issues, distinct from the scene's
intentional musical geometry and lighting responses.

## Camera

- Fixed focal length within each authored shot in both OpenGL and Vulkan RT.
  No bass/snare zoom, no transient shake. Authored lens choices between shots
  remain different.
- A separate integrated camera clock follows energy and restrained attacks,
  with 650ms velocity damping. It stays forward-moving and seek-deterministic;
  normalized path progress still reaches its endpoint at the chapter cut. Machinery,
  light, exhaust and physical object pulses retain the v0.6 response.
- Camera travel drives both the tracking camera and Armada moving heroes,
  keeping their framing coherent. Other machinery uses its original clock.
- Wider hero arcs, physical dolly/truck/crane movement and a maximum 1.43-degree
  added bank provide restrained parallax. Each set has bounded clearance;
  narrow passages get much smaller offsets than open hero shots. This is world
  space camera motion, not a screen space depth simulation.
- The Act III stairwell look target now moves continuously between levels.

The new measured maximum camera speed step at 60fps is 0.0153713, against a
regression limit of 0.08. This measures the timeline clock, not every final
screen-space acceleration or a human comfort rating.

## Song boundaries

The complete soundtrack remains 888.68s, with Act II at 300s and Act III at 589.92s.
Original MP3s and individual runtime tracks are preserved. Only two 4s windows
in the combined playback WAV change: 298–302s and 587.92–591.92s.

Each window uses two-second equal-power fade-out/in curves and a quiet,
decaying echo tail from the outgoing song. Positive mixing weights are bounded
to prevent clipping. This retains the incoming song's original sample clock,
without stretching, removing its intro or shifting chapter sync. It is a tail
bridge, rather than an overlapping edit that shortens the show. Picture fades
now use 1.6s smoothstep curves across act boundaries.

Recreate only the combined soundtrack with:

```powershell
python tools/prepare_show.py --join-only
```

`music/show-audio.json` records the new WAV hash and exact affected frame ranges.
Tests verify the original source/per-track hashes, sample equality everywhere
outside the bridge windows, bounded continuous fixture transitions and the
unchanged total length. Silence remains silent.

## Delivery and acceptance

Native builds and visual preview evidence are recorded under
`build/cinematic-v07/`; packages and preview are under `dist/cinematic-v07/`.
The owner authorized the complete 4K export on October 13, 2026, then requested
immediate YouTube upload and publication. The verified full movie is now public;
the obsolete scheduled upload has been paused. Artistic acceptance remains a
separate human judgment. No new GitHub release was published during this delivery.

| Verification | Result |
| --- | --- |
| Windows native tests | 6/6 passed |
| OpenGL / Vulkan RT scene tours | 152/152 each; 42 hardware RT frames |
| Linux native tests under WSLg | 5/6 passed; window lifecycle test did not observe minimization |
| Linux OpenGL scene tour | 152/152 passed |
| Independent code review after corrections | 8.5/10; artistic acceptance remains pending |
| Review montage | 72s, 1280x720, 60fps, HEVC Main10; all 4320 frames decoded |
| Montage audio verification | 18/18 windows passed, measured offset 0ms |
| Complete native movie | 3840x2160, 60fps, HEVC Main10, 53321 frames, 888.68s |
| Full movie verification | Complete video/audio decode passed; 18/18 audio windows, offset 0ms |
| YouTube delivery | Public; 2160p60 available in the public player's quality menu |

The montage includes selected shots from all three acts and both song boundaries.
It is a short review preview, not a new export of the full show.
[Watch the camera preview on Google Drive](https://drive.google.com/file/d/1AtgxFt1vrdq-h1k2Ac2zxc7NA12V44n0/view).
The Windows executable is `dist/cinematic-v07/Windows-x64/PulseAscendant.exe`;
the Linux executable is `dist/cinematic-v07/Linux-x64/PulseAscendant`.

[Watch the complete v0.7 demo on YouTube](https://youtu.be/TmI1FHAdJao).
[Full 4K60 HEVC file on Google Drive](https://drive.google.com/file/d/1j7Gf5cSpOPfJArDOzx5OYohipGma1S9q/view).
The full file is 3143777229 bytes, SHA256
`ec8d89441c0055f224cfb2bcdab1805daea0400f8a4f36d6fa282e1519244fbe`.
The previous main video is unlisted and points to this version. The existing
Short's description also points to it; YouTube requires channel verification
before the separate related-video feature can be used.

YouTube reports a melody/lyrics copyright claim for "Watching the Waves
(Cedric Paul Remix)" with no effect on the video's reach and no copyright strike.
No dispute or soundtrack alteration was submitted.

## v0.7 Short

[Watch the new Short](https://youtube.com/shorts/cZMAWPHVVCQ): 28 seconds,
1080x1920 at 60fps, seven native portrait camera excerpts. The source shots start
at 48.5, 168.5, 396.5, 412.5, 605, 664 and 685 seconds. Cuts follow measured
attacks in the 110-138s excerpt of Rigid Arpeggios. The complete 1680-frame decode
passed; all four audio correlation windows passed with zero measured offset.
The full movie's URL is saved at the top of the Short's description and its
on-screen call to action points viewers to the full demo on the channel.
The separate clickable related-video feature remains blocked by YouTube's
channel-verification requirement. The Short is public and YouTube's initial
copyright check reports no problems.
