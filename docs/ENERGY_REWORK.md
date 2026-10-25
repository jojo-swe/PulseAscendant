# Pulse Ascendant v0.5 — music, momentum and finish

Goals of this revision: more pace, conspicuous music response, more simultaneous meaningful action, stronger visual finish and epic scale. The pending v0.4 full movie is superseded; its published packages and local movie remain preserved as a baseline.

## Direction

Keep all three supplied songs, their exact playback timing, the 16 worlds and 76 musical chapter boundaries. Layer immediate musical responses over the longer chapter transformations: working machinery, foreground traffic, travelling energy, leaf/wing articulation and moving light within the scene. Use dark solid forms around luminous accents so increased activity remains readable. Act II remains the hardware ray tracing act, with an OpenGL fallback; Acts I and III remain OpenGL.

## Music response

The original20Hz analysis still supplies sustained energy,24 frequency bands and musical direction. `tools/analyze_reactivity.py` adds100Hz positive spectral-flux analysis in35–180Hz,180–4000Hz and4000–18000Hz bands, using a centered2048-sample Hann FFT. Local-floor subtraction and a shared spectral floor suppress sustained textures and near-silent leakage. Independent attacks decay in160/95/55ms. These are frequency-band attacks, not separated kick/snare stems or a claimed beat transcription. No assumed BPM or generated metronome is used.

The same saved signals now drive both interactive playback and export. Interactive callback RMS no longer overrides the analyzed energy with a nearly constant high floor. Music-dependent velocity is integrated into a continuous scene-motion clock, so rising/falling energy never rewinds that motion and seeking remains deterministic. This clock drives waves, plasma, travelling packets and selected secondary layers; the separately authored camera paths are faster but most still use chapter time/progress. Playback audio stays at its original rate.

`--pulse-gain 0..2` adjusts transient sensitivity (default1). Gain0 is useful for visual A/B evidence: it removes the direct transient response while retaining spectrum, sustained energy and the integrated musical motion. It is not a music-off mode.

The generation receipt is `music/reactivity.json`; `src/music_reactivity.hpp` embeds the result. `tools/prepare_show.py` regenerates this layer after creating the original PCM and editorial analysis.

## Verification scope

Synthetic narrow-band attacks test measured timing, decay and frequency isolation; digital silence must produce zero triggers. Native tests cover bounded independent lanes, changing forward velocity, seek determinism and continuity at chapters/acts. A native exact-time A/B tour proves the signals affect pixels in every world. All of these are technical evidence, not a claim of audience retention, continuous listening acceptance or AAA quality.

The first visual pass scored 6.5/10 and prompted further City, Armada and Cosmic composition work. The final independent review scored code 8.0/10, visual finish 7.2/10 and sampled pace/readability 7.3/10. Final Windows RT and OpenGL tours each passed all 152 samples. Linux WSLg passed 152 samples and all six CTest checks. A 160.75-second native 1080p60 preview passed complete video decoding and 13 source-audio comparisons with zero measured offset. See [verification](VERIFICATION.md) and [review](QUALITY_REVIEW.md) for the bounded scope.

The full 4K60 export is complete and verified. See the verification report for exact evidence.
