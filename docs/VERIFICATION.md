# Pulse Ascendant v0.5 — verification

This revision increases pace, separates musical attacks from sustained loudness and improves scene composition. Historical v0.4 tests/video remain in [VERIFICATION-v0.4.0.md](VERIFICATION-v0.4.0.md); they are not new v0.5 results.

## Audio and deterministic motion

All three songs, 76 chapter boundaries, 16 worlds and 888.68 seconds remain intact. Act I preserves 299 seconds of the original decoded track followed by its existing 1 second fade; Acts II and III use their complete supplied songs at normal speed. The runtime WAV remains SHA256 `58ca253cb4c427c0aa89f96e9cd4e12efb02ee8852ea75fd2cac0159321b2ea6`.

`tests/test_show.py` passes source hashes, sample-exact joins, lengths and bounded export. Five existing music-analysis/direction tests pass. Two new synthetic tests cover silence plus 80/1000/9000 Hz smooth attacks, peak timing within 30 ms, frequency isolation and decay. Native core tests cover independent bounded transient lanes, changing forward velocity, seek determinism and continuity at every chapter/act boundary.

The generated 100 Hz low/mid/high spectral-attack receipt is [reactivity.json](../music/reactivity.json). These lanes are not separated musical instruments. Interactive rendering no longer mixes callback RMS into visual energy; export and playback use the same saved signals. Authored camera paths and the music-dependent integrated scene-motion clock are distinct.

## Native evidence

- Windows x64 Release: CTest 6/6 PASS, 8.67 seconds, MSVC/SDL3. Later graphical pass3 changes only Armada/Cosmic shader assets; these passed the final native tours below.
- Linux Release: 6/6 CTest PASS in a private X11 namespace (36.74 seconds); WSLg OpenGL tour 152/152 at 960×540 with no reported GL errors. Three hidden starts at 42/342/620 seconds passed. All 28 copied shaders match final sources. This does not establish physical Linux hardware RT support.
- Vulkan compute compiled for Vulkan 1.2 and checked with spirv-val. The RT 208-byte UBO and shader layout match.
- Windows strict Vulkan RT+validation: 152/152 chapter images, 42 RT frames, all in Act II; no reported GL/Vulkan errors.
- Windows forced OpenGL fallback: 152/152 chapter images; no reported GL errors.
- Same-time native captures at pulse gain1 and0 show transient-driven changes in every world. Spectrum, energy and integrated musical motion stay active in this comparison; it is not a music-off test.

[Windows tests](evidence/v0.5.0/windows-tests.log), [RT tour](evidence/v0.5.0/tour-rt.log), [OpenGL tour](evidence/v0.5.0/tour-fallback.log), [show information](evidence/v0.5.0/show-info.json), [A/B measurements](evidence/v0.5.0/pulse-comparison.json), [A/B images](evidence/v0.5.0/pulse-comparison.jpg).

## Bounded performance probes

NVIDIA RTX 4080, Windows driver 617.42, 1280×720, three-second hidden runs after pipeline preparation, audio disabled, vsync requested off. These are short scene probes, not a full-show benchmark or a 4K60 guarantee.

| Start / scene | Mean FPS | Mean frame ms |
|---|---:|---:|
| 58s / City | 248.8 | 4.00 |
| 77s / Cosmic | 181.0 | 5.50 |
| 110s / Armada | 168.6 | 5.91 |
| 411s / hardware-RT Prism | 34.3 | 29.09 |
| 651s / Canyon | 229.4 | 4.34 |

Maximum observed frames ranged 31–82 ms. Logs are `evidence/v0.5.0/perf-*.log`. Vulkan RT is substantially more expensive than the procedural OpenGL scenes. Actual interactive FPS depends on scene, resolution and device. WSLg/physical Linux RT remain separate scopes.

## Visual review and preview

Independent review: code 8.0/10, visual finish 7.2/10, sampled pace/readability 7.3/10. The first 6.5/10 pass was corrected before these final scores. [Full report](QUALITY_REVIEW.md) records scope and remaining limitations. This does not establish full continuous listening, audience retention or AAA quality.

A 160.75-second preview contains 13 native clips with their original source music, 1920×1080 at 60 fps and 9645 frames. Full video decode to EOF passed. All 13 decoded audio windows match their source excerpts with at most 2 ms allowed offset; [actual results](evidence/v0.5.0/preview-audio.json) show zero measured offset. Concatenating individually encoded AAC clips initially introduced 21.375 ms; the final preview rebuilds one sample-exact PCM montage and encodes its audio once. Native video packets are retained and their starting timestamp normalized.

## Full film

The v0.5 full 4K60 render is complete and verified. The verified v0.4 movie is not reused as v0.5. Existing older public trailers are labeled as older versions.

## Completed full 4K60 export

The v0.5 final file contains **53,321 frames at 3840×2160 / 60 fps**, with 888.683 seconds of video and 888.68 seconds of audio. File size: **6,712,158,215 bytes**. The native producer reports 17,396 hardware-RT frames, confined to Act II. The producer and runtime shader/audio hashes matched the verified v0.5 release before rendering.

Full CUDA-assisted ffmpeg decoding reached EOF with all 53,321 frames, exit 0 and no error output. Eighteen two-second decoded audio windows across all acts, both joins and the ending passed comparison against the runtime WAV: minimum correlation **0.999618**, maximum measured offset **0 ms** at 8 kHz analysis resolution. Five full-aspect final-video images at 58, 110, 411, 651 and 865 seconds were inspected without gross corruption or orientation errors. This is technical and sampled visual evidence, not full uninterrupted listening or audience-retention acceptance.

[Video receipt](evidence/v0.5.0/video-receipt.json) · [Metadata](evidence/v0.5.0/video-probe.json) · [Full decode result](evidence/v0.5.0/video-decode-result.json) · [Decoded frame count](evidence/v0.5.0/video-decode-progress.log) · [Audio windows](evidence/v0.5.0/video-audio.json). Final encoded images: [City](evidence/v0.5.0/video-frame-58.jpg), [Fleet](evidence/v0.5.0/video-frame-110.jpg), [Prism](evidence/v0.5.0/video-frame-411.jpg), [Canyon](evidence/v0.5.0/video-frame-651.jpg), [Ascent](evidence/v0.5.0/video-frame-865.jpg).

SHA-256: `a37e6645dcacbaf9b032ea517b7b2f23b1e60b1fd0ed5e6b9e9d5edfe8ef494e`. The immutable v0.5.0 binary/source archives remain the already verified release; these later video receipts are supplemental evidence.
