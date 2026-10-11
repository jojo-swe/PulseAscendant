# Pulse Ascendant: Act II

Act II plays the complete **Adventurous Uplift** soundtrack at normal speed: **13,916,160 stereo frames at 48 kHz, 289.92 seconds, 21 chapters and four worlds**. It starts immediately after Act I at global frame 14,400,000, or 5:00. Its final frame is followed by Act III at global frame 28,316,160.

`src/act2.hpp` owns editorial chapter metadata. `music_data_act2.hpp` owns the measured length and FFT curves; `cue_data.hpp` contains directed starts chosen against this song. The continuous runtime transport reads the combined three-act WAV, while the separate `adventurous-uplift.wav` remains available in the source bundle for regeneration and tests.

## Four worlds

| Scene ID | Key | World | Vulkan geometry and action |
| --- | --- | --- | --- |
| 8 | 9 | Chrome Cathedral | Repeated reflective arches and emissive ribs frame moving polished spheres. Lights and the central form follow the music. |
| 9 | 0 | Rain District | Layered skyline and foreground buildings surround a moving avenue. Traffic, rain instances, reactive windows and wet surfaces move independently. |
| 10 | - | Orbital Foundry | Nested metal rings, rotating arrays and tall machinery surround an animated central solid. |
| 11 | = | Prism Chamber | Moving glass prisms surround a larger refractive crystal, with emissive structures and spectrum geometry. |

F2 seeks to the act's first sample. Scene keys and chapter arrows seek the audio and graphics together. Four camera variants run within chapters; the scene changes use measured attacks and persistent spectral changes rather than a fixed BPM. The generated musical-direction report is `music/direction.json`. Its numerical choices still require listening for subjective synchronization.

## Hardware rendering and fallback

The Vulkan backend traces triangle BLAS/TLAS using KHR ray queries. Five reusable mesh types and a 2,048-instance ceiling support animated scenes, shadows, reflected geometry and bounded glass transmission. The compute shader allows up to five surface encounters per sample. Modern RT output uses two deterministic subpixel samples and an adaptive edge filter; retro uses one sample. It does not implement a full path tracer, general GI or temporal denoising.

A staging transfer copies the Vulkan HDR output through host memory into OpenGL each frame. OpenGL then presents bloom, effects and captions. There is no zero-copy interoperability claim. Performance depends on the scene and resolution; the 4K60 video export setting is not an interactive frame-rate guarantee.

`assets/shaders/act2.frag` implements the separate OpenGL 3.3 SDF fallback. It preserves four distinct themes and music-driven movement, with approximate environment reflections. It is not hardware RT and does not reproduce the Vulkan scene mesh for mesh. The on-screen backend indicator identifies which path is active.

Use `--no-rt` for fallback, `--require-rt` for strict RT initialization, and `--rt-validation` where Vulkan validation layers are installed. A capability probe is separate from rendering evidence:

```powershell
cmake --preset windows -DREMIX_BUILD_RT_PROBE=ON
cmake --build --preset windows --target remix_rt_probe
build/windows/Release/remix_rt_probe.exe
```

The probe needs Vulkan development libraries. The main program dynamically loads Vulkan and can fall back when its loader or required device features are unavailable.

## Music provenance and acceptance

The original MP3 is retained unchanged. [Act II receipt](../music/asset-receipt-act2.json) records its source hash, Suno ID and runtime WAV hash. The complete song has no added fade or speed change. [Music notice](../licenses/MUSIC-NOTICE.txt) contains separate licensing and credits.

Current RT, fallback, Windows, Linux, motion and quality results belong to [VERIFICATION.md](VERIFICATION.md). Shader compilation and successful native startup alone do not establish every camera view, physical Linux RT support or artistic acceptance.
