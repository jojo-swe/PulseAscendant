# Pulse Ascendant: Act III

The third act plays the complete **Rigid Arpeggios** soundtrack at normal speed: **14,340,480 stereo frames at 48 kHz, 298.76 seconds, 24 chapters and four worlds**. Its global start is frame 28,316,160, or 9:49.92. The complete show ends at 42,656,640 frames, or 14:48.68.

`src/act3.hpp` defines chapter metadata. `music_data_act3.hpp` supplies the decoded frame count, duration and analysis. `cue_data.hpp` supplies musical direction from the new song's measured attacks and persistent spectral changes. No second song is repeated or stretched to create this act.

## Four worlds, two implementations

| Scene ID | Key | World | Vulkan triangle scene | OpenGL fallback |
| --- | --- | --- | --- | --- |
| 12 | Q | Dune Colossus | A hero ceramic walker and six distant walkers stand on dune terrain. Moving joints, feet, luminous faces and drifting accents establish scale. | Signed-distance dunes, articulated ceramic walker, distant sentinels and warm sunlight. |
| 13 | W | Verdant Singularity | Seven branching trees carry moving leaves and reactive emissions; independently drifting seeds and an overhead ring fill the grove. | Repeated branching trees, layered leaves, bass-driven canopy growth and three-dimensional seed drift. |
| 14 | E | Gravity Rebellion | A dark core and luminous rings anchor moving floating stone and a larger debris field. | A rotating fractured core, nine independently moving stone platforms, smaller debris and a tilted luminous orbit. |
| 15 | R | Reality Ascendant | Glass, nested rings, crystal wings and an orbiting fleet combine into the final assembly. | Open arches, a central rotating crystal, sixteen elongated sails, a luminous cage, halo and floor grid. |

`src/act3_rt.inl` defines the actual triangle instances used by the Vulkan TLAS. `assets/shaders/act3.frag` defines the separate OpenGL 3.3 signed-distance worlds. They share scene identities and music curves while using different geometry. F3 seeks to this act; Q/W/E/R select its worlds with the corresponding music.

The final three chapters stay with the assembly for a sustained ending. Four camera variants and shorter intra-chapter events supply movement between world changes. Bass, midrange, treble and onsets drive different surface, geometry and light properties. Procedural movement continues during quieter passages.

## Tracing contract and limits

Act III uses the shared Vulkan KHR ray-query backend: five reusable triangle BLAS, a dynamic TLAS and a 2,048-instance ceiling. Shadows, reflections and refraction use bounded traversal with up to five surface encounters. Modern RT uses two deterministic subpixel samples plus an adaptive edge filter; retro uses one. The shader is not a full path tracer and does not provide general global illumination or temporal denoising.

Vulkan renders into device-local memory, then copies through host readback before OpenGL HDR upload and composition. This staging path is not zero-copy. The fallback uses SDF/analytic lighting and environment reflection; it must remain clearly identified. A 4K60 export is a deterministic video specification, not a guarantee of interactive 4K60.

## Sample-exact handoff

Let `N1`, `N2` and `N3` be the decoded frame counts. The acts occupy half-open intervals `[0,N1)`, `[N1,N1+N2)` and `[N1+N2,N1+N2+N3)`. Act II's final frame is 28,316,159; the next frame, 28,316,160, is local frame zero of Act III. The combined runtime WAV includes both frames with no padding, dropped sample or playback resampling.

Act-local FFT lookup and cue clocks reset at that boundary while the common audio transport continues. The ending and export bounds use the measured final sample rather than a nominal last chapter target. The separate runtime source asset is `music/rigid-arpeggios.wav`; playback uses the combined three-act asset.

The original planning reservation in `src/act3_plan.hpp` now resolves to the supplied track's integrated definition. It is a compatibility contract, not an independent or conflicting runtime availability source.

## Acceptance and provenance

[Act III receipt](../music/asset-receipt-act3.json) records the original MP3 and runtime WAV hashes, Suno ID and complete-song policy. The original MP3 is preserved; music rights are separate from the code's MIT license. See [MUSIC-NOTICE.txt](../licenses/MUSIC-NOTICE.txt).

Current checks and limitations are recorded in [VERIFICATION.md](VERIFICATION.md). The final review must distinguish sample continuity, chapter alignment, fallback rendering, actual Vulkan RT execution, platform support, motion and artistic quality. The requested visual acceptance threshold is at least 7/10 from an independent review. Native GPU captures and review findings must support that score; shader compilation alone cannot establish it.
