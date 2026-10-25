# Pulse Ascendant: Act III

The third act uses the complete **Rigid Arpeggios** soundtrack at normal speed:
14,340,480 stereo frames at 48 kHz, 298.76 seconds, 24 chapters and four worlds.
It begins at global frame 28,316,160 (9:49.92); the complete show ends at
42,656,640 frames (14:48.68). Music and chapter boundaries are unchanged in v0.4.

## Different staging at every return

| Scene | Key | Successive set pieces |
| --- | --- | --- |
| 12: Dune Colossus | Q | Rising monuments, walking titan, canyon chase, mechanical unfolding, solar train, vertical fleet launch. |
| 13: Verdant Singularity | W | Germination, DNA tower, seed tunnel, quiet lotus, terraced cascade, enormous bloom. |
| 14: Gravity Rebellion | E | Fractured floor, falling staircase, separated slab, vertical uprising, opening walls. |
| 15: Reality Ascendant | R | Gate flight, unfolded wings, branching crystal, large assembly, monument landscape, vertical ascent, final split into an open solar horizon. |

`assets/shaders/act3.frag` implements these OpenGL 3.3 signed-distance and
analytic worlds. The renderer selects the chronological occurrence directly,
without wrapping it to four camera variants. Each chapter has one camera path,
continuous progress and a one-time transformation driven by a measured music
accent. Smaller details retain interpolated FFT animation.

The final three chapters use different structures and camera paths. The last
chapter dissolves the central form and leaves a simple horizon for the ending.
F3 seeks to Act III; Q/W/E/R select its worlds with the corresponding music.

## Rendering contract

Act III uses OpenGL in v0.4, including when `--require-rt` initializes the Vulkan
backend for Act II. SDF lighting, bounded shadow marches and environment
reflections are approximate. They are not hardware ray tracing or a general
path tracer. HDR, bloom, antialiasing and composition remain shared.

The previous triangle-based implementation is preserved in `src/act3_rt.inl`
for historical source context. The main renderer no longer routes Act III to
it. v0.3 videos and performance measurements describe that older implementation.

## Music and evidence

The combined WAV joins Act II's final frame and Act III's first frame without
padding, dropped samples or playback resampling. The act-local FFT lookup
resets at the boundary; the audio transport and integrated motion clock remain
continuous. Export bounds use the measured final sample count.

[The source receipt](../music/asset-receipt-act3.json) records the original MP3,
runtime WAV and provenance. Music rights are separate from the code's MIT
license; see [the music notice](../licenses/MUSIC-NOTICE.txt).

See [the variation rework](VARIATION_REWORK.md) and [verification](VERIFICATION.md)
for current checks. Image differences, compilation and technical reviews do not
establish full audiovisual or human artistic acceptance.
