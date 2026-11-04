# Pulse Ascendant v0.5 — independent review

Read-only review, 2026-10-12. The reviewer did not author this revision. Historical evidence remains in [QUALITY_REVIEW-v0.4.0.md](QUALITY_REVIEW-v0.4.0.md).

**Approved within the inspected scope**, exceeding the 7/10 acceptance threshold.

| Area | Score |
|---|---:|
| Code and signal pipeline | 8.0/10 |
| Visual finish and composition | 7.2/10 |
| Sampled motion, pace and readability | 7.3/10 |

The 100 Hz frequency-band attacks reach both renderers, remain deterministic when seeking and vary independently. Removing callback RMS from visual energy fixes a plausible cause of flattened interactive response. Motion integration stays forward-moving; the 208-byte RT parameter layout matches its shader.

The reviewer found a real RT wrapping bug: negative `std::fmod` moved late-act traffic outside its intended volume. The positive-wrap replacement fixes it. Missing `--pulse-gain` help was also corrected. **No remaining blocking code finding identified.**

The first visual pass scored 6.5/10. Subsequent changes materially improved it:

- City retains dark buildings, distinct windows and a readable landmark.
- Armada has shaped hulls, engine pods and a clear pursuit subject. The final late-chapter sequence retains the flagship instead of leaving empty space.
- Cosmic scenes show bright stellar streams and asymmetric remnant filaments instead of large concentric discs.
- Garden, reactor, prism and ascent compositions remain the strongest.

The reviewer inspected all 16 initial world boards, revised affected boards, four OpenGL Act II boards and 14 ordered strips of 32 frames. The final three strips show continuous binary-stream development, growing supernova structure and active late Armada escorts. Fast traversal and secondary activity are visible without gross sampled camera jumps or freezes. Independent A/B measurements confirmed transient-driven pixel changes in every world.

Evidence: `build/energy-v05/boards-rt-v3`, `boards-gl-v2`, `sequence-v2`, `sequence-v3` and native tour logs. Published selected images and A/B evidence are under `docs/evidence/v0.5.0`. The reviewer independently ran the two synthetic reactivity tests successfully and read the final RT/OpenGL tour PASS logs.

This is source, sampled-image and ordered-frame review. It does **not** establish continuous listening acceptance, complete 14:49 audience retention, 60 fps freedom from fine shimmer, final 4K correctness or AAA quality. Remaining artistic weaknesses are simple architectural materials, soft cosmic detail, visibly stylized exhausts and a comparatively quiet carrier flyby. OpenGL fallback remains visibly flatter than the preferred RT presentation.
