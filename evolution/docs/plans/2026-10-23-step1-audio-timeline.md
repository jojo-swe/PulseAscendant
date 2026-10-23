# EVOLUTION Step 1: Audio Clock, Timeline and Placeholder Music Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The demo plays a 48 kHz WAV with a sample-exact clock, every frame samples a deterministic `TimelineFrame` (chapter, effect, era 1993/2026, transition state, beat phase, music curves) from that clock, `--show-info` reports the directed chapters, and a synthetic placeholder song plus the ported Python analysis pipeline make all of it testable before the real Suno tracks arrive.

**Architecture:** Python tools (ported from the parent project, generalised to N tracks) turn WAVs into generated C++ headers under `evolution/src/show/generated/`: 20 Hz envelopes and 24 spectrum bands, 100 Hz transients, a tracked beat grid, and chapter starts placed on beats near authored targets. `evo::show` samples those tables plus the authored cue sheet into a `TimelineFrame`; the era/transition state (portal, beat-swap, wipe) is a pure function of time and the beat grid. `evo::audio` reads the WAV, renders it through an SDL3 audio stream and exposes a queue-compensated position; the app uses that position as the only clock when audio is on, and a monotonic clock otherwise. The compute test pattern reacts to the frame so the result is visible and capturable.

**Tech Stack:** C++20, SDL3 audio streams, Python 3 + numpy (+ ffmpeg only when converting MP3), CTest + Python unittest.

**Spec:** `evolution/docs/plans/2026-10-21-evolution-design.md` (sections 2.2–2.3, 3.7, 3.9, 4, 5, 6; step 1 of section 7). Step 0 code is on the same branch; read `evolution/src/gfx/offscreen.hpp` and `evolution/src/app/options.hpp` for what exists.

## Amendment 2026-10-23: the real music arrived (binding; overrides the text below where they differ)

The owner supplied the two Suno tracks before step 1 finished. From Task 3 on, the show is **two acts**:

| Act | Track | Frames | Show start | Length |
|---|---|---|---|---|
| I | Adventurous Uplift (≈91.9 BPM) | 13 916 160 | 0 | 289.92 s |
| II | Rigid Arpeggios (≈92.2 BPM) | 14 340 480 | 289.92 s | 298.76 s |

Show length **588.68 s** = 28 256 640 frames; `assets/music/show.wav` (git-ignored; regenerate with `tools/prepare_show.py "music/source/Adventurous Uplift.mp3" "music/source/Rigid Arpeggios.mp3"`). The generated headers under `src/show/generated/` already hold this show (`chapterCount = 21`). The placeholder song is now only used by the Python tests (`tools/show_targets_placeholder.json`).

**21 chapters**, `Effect` enum in chapter order (one effect per chapter): `Boot, Flyby, Dots, Plasma, Tunnel, Glenz, Lens, Raytrace, City, Finale, Portal, StudioNight, Ascent, CityNight, Nebula, Cathedral, Ocean, SphereStorm, Singularity, Return, Credits`. Act I (chapters 0–10) carries the 1993/2026 pairs; Act II (11–20) is 2026 only, a deeper journey that starts in the Studio at night and returns to it.

Authored targets (show seconds, verbatim from `tools/show_targets.json`): Boot 0, Flyby 10, Dots 34, Plasma 58, Tunnel 82, Glenz 108, Lens 134, Raytrace 162, City 194, Finale 226, Portal 258, StudioNight 289.92, Ascent 313.92, CityNight 341.92, Nebula 373.92, Cathedral 405.92, Ocean 437.92, SphereStorm 469.92, Singularity 501.92, Return 531.92, Credits 559.92. The directed starts are in `cue_data.hpp` (`chapterStarts[21]`, `chapterTargets[21]`), each on a tracked beat within ±2.2 s of its target; chapter 11 starts exactly at 289.92.

`Cue` gains `float eraFixed` (0 or 1), used only by `Transition::Cut`: the chapter holds that era throughout. Cue sheet (transition, drive, eraFixed where Cut): Boot Cut/.2/0 · Flyby Portal/.6 · Dots BeatSwap/.8 · Plasma Wipe/.7 · Tunnel Portal/.8 · Glenz BeatSwap/.9 · Lens Wipe/.7 · Raytrace BeatSwap/1 · City Wipe/.9 · Finale BeatSwap/1 · Portal Portal/.7 (the pull-back into the Studio; era reaches 1 and Act I ends in 2026) · StudioNight Cut/.4/1 · Ascent Cut/.7/1 · CityNight Cut/.8/1 · Nebula Cut/.6/1 · Cathedral Cut/.8/1 · Ocean Cut/.9/1 · SphereStorm Cut/1/1 · Singularity Cut/.9/1 · Return Cut/.5/1 · Credits Cut/.4/0. Then/now cards for Act I as in Task 3; Act II chapters have an empty thenCard and a nowCard such as `2026 · THE STUDIO · PATH TRACED`, `2026 · ASCENT · VOLUMETRIC LIGHT`, `2026 · CITY NIGHT · WET STREETS`, `2026 · NEBULA · VOLUMETRIC`, `2026 · GLASS CATHEDRAL · REFRACTION`, `2026 · PARTICLE OCEAN · 2,097,152 PARTICLES`, `2026 · SPHERE STORM · PATH TRACED`, `2026 · SINGULARITY`, `2026 · RETURN · THE STUDIO`, `1993 → 2026 · CREDITS`.

Test values change accordingly: `showDuration() == 588.68`; `chapterCount() == 21`; `chapterStart(11) == 289.92` exactly; `sampleTimeline(289.92).effect == Effect::StudioNight` and `.chapter == 11`; `sampleTimeline(588.679).chapter == 20`; `sampleTimeline(588.68).chapter == 0`; era tests use Flyby (Portal), Plasma (Wipe), Raytrace (BeatSwap), Boot (Cut → 0), StudioNight (Cut → 1), Credits (Cut → 0); `--show-info` regex `"duration":588.68`; Task 6 captures at `--time 165` (Raytrace) and `--time 300` (StudioNight) must differ. The `BeatGridView` for a chapter is the view of that chapter's track, with times in track seconds; `sampleTimeline` converts show seconds to track seconds by subtracting the track start.

## Global Constraints

- Everything lives under `evolution/`; nothing outside it is modified.
- Audio format is fixed: stereo, 48 000 Hz, PCM16 WAV. Tracks are concatenated with no silence between them. Playback speed is never altered.
- Music data headers are generated, never hand-edited: `evolution/src/show/generated/{music_data,reactivity_data,beat_data,cue_data}.hpp`, namespace `evo::show::generated`. Each starts with a `// Generated by tools/<script>.py` line. They are committed (the WAVs are not: `assets/music/*.wav` is git-ignored).
- Analysis constants, verbatim from the parent tools: envelope rate 20 Hz, 24 log bands 35–18 000 Hz (8192-sample Hann at 48 kHz); transients 100 Hz in bands (35,180), (180,4000), (4000,18000) Hz with release (.16,.095,.055) s and refractory (.16,.11,.07) s; beat tracker 100 Hz log spectral flux, 70–180 BPM, log-normal prior around 120 BPM, DP tracking, ±6-beat linear smoothing, 4/4 bar position from ±24-beat windows; direction window ±2.5 s, chapter bounds 5–18 s, cuts on tracked beats with bar-line and 4-bar-phrase bonuses.
- Placeholder song: deterministic (fixed seed), 134 BPM, 4/4, exactly 270.0 s = 12 960 000 frames, written to `assets/music/placeholder.wav` by `tools/make_placeholder.py`; never committed.
- Determinism: every value in `TimelineFrame` is a pure function of show time (and generated data). Two different seek histories reaching the same time produce bit-identical frames.
- Namespaces: `evo::audio`, `evo::show`. Style and commit rules as in step 0 (`global-constraints.md` in the SDD workspace).
- Era vocabulary, verbatim: `era` 0 = 1993, 1 = 2026. Transition enum `Transition { Cut, Portal, BeatSwap, Wipe }`. Effect enum `Effect { Boot, Flyby, Dots, Plasma, Tunnel, Glenz, Lens, Raytrace, City, Finale, Credits }` (11 values, in show order).
- Chapter targets for the placeholder (seconds): Boot 0, Flyby 10, Dots 34, Plasma 58, Tunnel 82, Glenz 106, Lens 130, Raytrace 158, City 190, Finale 222, Credits 246; show length 270.

## Review Focus

1. **Seeking backwards then forwards** (user presses ← then →): every field, including `eraCut` and `flowSeconds`, must equal the value of a straight play-through at the same time. Pinned in Task 4 (determinism test over 200 random seek pairs).
2. **A WAV whose length differs from the generated data** (user drops a new song without regenerating): the app must refuse with a message naming both lengths, not drift. Pinned in Task 5.
3. **Audio device absent** (CI, lavapipe container, user with no output device): the demo runs on the monotonic clock and shows `SILENT MODE`, exit code 0. Pinned in Task 5 with `SDL_AUDIODRIVER=dummy` and with an invalid driver.
4. **Chapters at the show boundary** (t = 0, t = 269.999, t = 270 wraps to 0): `sampleTimeline` must never index past the last chapter or beat. Pinned in Task 3.
5. **A silent or beatless stretch** (placeholder breakdown before the drop, future real song intros): beat phase must not jump to garbage; `beatPhase` and `barPhase` hold the last tracked beat's grid and `swap` schedules never divide by zero. Pinned in Task 4 with a chapter whose window has fewer than 4 bars.

---

### Task 1: Python pipeline port and placeholder song

**Suggested model:** opus.

**Files:**
- Create: `evolution/tools/analyze_music.py`, `evolution/tools/analyze_reactivity.py`, `evolution/tools/beat_grid.py`, `evolution/tools/direct_music.py` (ported from `/home/user/PulseAscendant/tools/*.py`; keep the algorithms and constants verbatim, change only I/O: single or multiple WAV inputs given on the command line, outputs under `evolution/`, namespace `evo::show::generated`).
- Create: `evolution/tools/make_placeholder.py`
- Create: `evolution/tools/prepare_show.py`
- Create: `evolution/tools/show_targets.json` (the chapter targets and titles from Global Constraints, read by `direct_music.py`; the C++ cue sheet in Task 3 mirrors it and a test cross-checks them)
- Create: `evolution/music/README.md` (what each JSON receipt is), `evolution/music/.gitkeep`
- Test: `evolution/tests/test_beat_grid.py` (port of the parent's synthetic drifting click-track test), `evolution/tests/test_placeholder.py`, `evolution/tests/test_pipeline.py`

**Interfaces:**
- Produces: `python3 tools/make_placeholder.py <out.wav>` writes the placeholder. Structure in bars (134 BPM, bar = 1.7910 s): bars 0–7 chip intro (square-wave lead melody, noise hats, no kick); 8–23 kick on every beat + snare on 2 and 4 + saw bass on chord roots; 24–39 adds pad chords changing every 4 bars and an arpeggio; 40–55 brighter lead, kick continues; 56–59 breakdown (no kick, pad only, rising noise sweep); bar 60 (= 107.46 s)… wait — the drop must land near the Raytrace target 158 s = bar 88.2: so sections are: 0–7 intro, 8–31 groove, 32–55 arpeggio/pad, 56–79 brighter, 80–87 breakdown (no kick, sweep), **88–119 drop** (kick, sub bass, wide saw chords, loudest), 120–135 outro reprising the intro lead over a kick, 136–150.8 (to 270 s) fade of pad only. Every drum is a synthesised transient (kick: 55 Hz sine with pitch drop and 80 ms decay; snare: filtered noise 120 ms; hat: high noise 30 ms). Peak-normalise to −1 dBFS. Seed 1993. Print the SHA-256 and frame count.
- Produces: `python3 tools/prepare_show.py <track1.(wav|mp3)> [<track2> …]` → for each input: convert with ffmpeg to stereo 48 kHz PCM16 under `assets/music/<stem>.wav` if not already that format, then concatenate into `assets/music/show.wav`; run analysis, reactivity, beat grid and direction over the per-track WAVs; write the four generated headers and the receipts `music/analysis.json`, `music/reactivity.json`, `music/beats.json`, `music/direction.json`, `music/show-audio.json` (frames, duration, per-track start frame, sha256, runtime_file).
- Produces, generated header contracts (Task 3 consumes them):
  ```cpp
  namespace evo::show::generated {
  // music_data.hpp
  inline constexpr int trackCount = N;
  inline constexpr std::uint64_t trackFrames[N] = {...};          // per track
  inline constexpr int envelopeRate = 20, bands = 24;
  inline constexpr int envelopeOffsets[N+1] = {...};              // 20 Hz sample offsets per track
  inline constexpr std::uint8_t envelope[] = {...};               // 3 per sample: energy, bass, hit
  inline constexpr std::uint8_t spectrum[] = {...};               // 24 per sample
  // reactivity_data.hpp
  inline constexpr int reactivityRate = 100; inline constexpr int reactivityOffsets[N+1]; inline constexpr std::uint8_t reactivity[]; // 4 per sample: low, mid, high, lowOnsetPhase
  // beat_data.hpp
  inline constexpr int beatOffsets[N+1]; inline constexpr double beatTimes[]; /* track seconds */ inline constexpr std::uint8_t beatBar[];
  // cue_data.hpp
  inline constexpr int chapterCount = 11; inline constexpr double chapterStarts[chapterCount]; /* show seconds, first = 0 */
  inline constexpr const char* sourceSha256 = "...";              // of show.wav
  }
  ```
- Produces: `direct_music.py` reads `tools/show_targets.json` (`[{"title":"BOOT","target":0,"track":0}, …]`), places each target on a tracked beat within ±2.5 s with 5–18 s bounds exactly like the parent, and writes `cue_data.hpp` + `music/direction.json`. Chapter 0 is always 0.

- [ ] **Step 1: Write the failing tests**
  - `test_placeholder.py`: generates into a temp dir; asserts frames == 12 960 000, stereo 48 kHz PCM16, peak ≤ −1 dBFS + 0.1 dB, RMS of bars 88–119 (drop) > RMS of bars 0–7 (intro) by ≥ 6 dB, RMS of bars 80–87 (breakdown) < RMS of bars 56–79; and that two runs give identical SHA-256.
  - `test_beat_grid.py`: the parent's synthetic test (124→126 BPM drifting clicks): median beat error < 15 ms, ≥ 90 % of downbeats correct.
  - `test_pipeline.py`: runs `prepare_show.py` on the placeholder (generated into a temp `assets/music`), then asserts the four headers exist, `chapterStarts` has 11 ascending values with first 0 and last < 270−5, every non-zero start is within 2.5 s of its target and coincides (±1 ms) with a `beatTimes` entry, `beats.json` `bpm_median` within 134 ± 1, and `show-audio.json` frames == 12 960 000.

- [ ] **Step 2: Run them to verify they fail** (`python3 -m unittest discover -s evolution/tests -p 'test_*.py'`). Expected: ImportError / FileNotFoundError.

- [ ] **Step 3: Port the tools, write `make_placeholder.py`, `prepare_show.py`, `show_targets.json`, music README.** Path handling: every script takes explicit paths or derives them from `Path(__file__).resolve().parents[1]` (= `evolution/`). Output directory for headers: `src/show/generated/`. No `remix` names anywhere.

- [ ] **Step 4: Run the tests** Expected: all pass; `prepare_show.py` on the placeholder prints the direction summary; `beats.json` shows ~134 BPM.

- [ ] **Step 5: Generate and commit the headers** for the placeholder (`python3 tools/make_placeholder.py assets/music/placeholder.wav && python3 tools/prepare_show.py assets/music/placeholder.wav`), add `tests/test_*.py` to CTest via `add_test(NAME python_tools COMMAND python3 -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests -p "test_*.py")`, and commit: `Port the music pipeline and synthesise a placeholder song`.

---

### Task 2: WAV reader and audio transport

**Suggested model:** opus.

**Files:**
- Create: `evolution/src/audio/wav.hpp`, `evolution/src/audio/wav.cpp`
- Create: `evolution/src/audio/transport.hpp`, `evolution/src/audio/transport.cpp`
- Test: `evolution/tests/audio_transport_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace evo::audio {
  struct Wav { std::vector<float> stereo; /* interleaved L R, -1..1 */ std::uint64_t frames() const; };
  Wav readWav(const std::filesystem::path&);   // throws std::runtime_error naming the path and the problem; only stereo 48 kHz PCM16 accepted; files up to 512 MiB
  class Transport {                             // position in frames; wraps at the end
  public:
      explicit Transport(Wav);
      void seek(double seconds);                // floor(fmod(s, duration)*48000 + 1e-7); negative/NaN → 0
      void setMuted(bool);
      double positionSeconds() const; double durationSeconds() const; std::uint64_t frame() const;
      void render(float* out, std::size_t frames);  // interleaved stereo; muted writes zeros but advances
  };
  }
  ```
- [ ] **Step 1: Write the failing test**: builds a 48 000-frame WAV in memory (write a tiny WAV with Python-free C++ helper in the test: RIFF/fmt/data), writes it to the build-out dir, `readWav` → `frames()==48000`; `Transport::seek(0.5)` → `frame()==24000`; `seek(1.5)` wraps to 24000; `seek(-1)` → 0; `render(out, 10)` after `seek(0.99999)` wraps across the end without reading out of range; muted render gives zeros and advances by 10; `readWav` on a 44.1 kHz file throws with message containing "48000".
- [ ] **Step 2: Run to verify it fails.** **Step 3: Implement** (library `evo_audio` with `wav.cpp`, `transport.cpp`, no SDL dependency yet). **Step 4: Run** `ctest -R audio_transport`. **Step 5: Commit** `Read WAV music and drive a sample-exact transport`.

---

### Task 3: Cue sheet and timeline core

**Suggested model:** default.

**Files:**
- Create: `evolution/src/show/cues.hpp` (authored cue sheet, constexpr)
- Create: `evolution/src/show/timeline.hpp`, `evolution/src/show/timeline.cpp`
- Create: `evolution/src/show/music_curves.hpp`, `evolution/src/show/music_curves.cpp` (sampling of generated tables)
- Test: `evolution/tests/timeline_test.cpp`

**Interfaces:**
- Consumes: generated headers (Task 1).
- Produces:
  ```cpp
  namespace evo::show {
  enum class Effect { Boot, Flyby, Dots, Plasma, Tunnel, Glenz, Lens, Raytrace, City, Finale, Credits };
  enum class Transition { Cut, Portal, BeatSwap, Wipe };
  struct Cue { double target; Effect effect; Transition transition; float drive; const char* title; const char* thenCard; const char* nowCard; };
  inline constexpr std::array<Cue, 11> cues = {{ {0, Effect::Boot, Transition::Cut, .2f, "BOOT", "1993 · MS-DOS · 320×200", ""},
      {10, Effect::Flyby, Transition::Portal, .6f, "FLYBY", "1993 · 320×200 · 256 COLORS · FLAT SHADING", "2026 · PATH TRACED · VOLUMETRIC TRAILS"},
      {34, Effect::Dots, Transition::BeatSwap, .8f, "DOTS", "1993 · 512 POINTS", "2026 · 2,097,152 PARTICLES"},
      {58, Effect::Plasma, Transition::Wipe, .7f, "PLASMA", "1993 · PALETTE CYCLING", "2026 · VOLUMETRIC NEBULA"},
      {82, Effect::Tunnel, Transition::Portal, .8f, "TUNNEL", "1993 · 320×200 · 35 FPS", "2026 · PATH TRACED REFLECTIONS"},
      {106, Effect::Glenz, Transition::BeatSwap, .9f, "GLENZ", "1993 · 24 TRIANGLES · TRANSPARENT VECTORS", "2026 · REFRACTION · DISPERSION · CAUSTICS"},
      {130, Effect::Lens, Transition::Wipe, .7f, "LENS", "1993 · ROTOZOOM · LENS", "2026 · GLASS SPHERE · RAY TRACED FLOOR"},
      {158, Effect::Raytrace, Transition::BeatSwap, 1.f, "RAYTRACE", "1993 · PRECALCULATED · 8 FPS", "2026 · REAL-TIME PATH TRACING · 60 FPS"},
      {190, Effect::City, Transition::Wipe, .9f, "VECTOR CITY", "1993 · 200 POLYGONS · GOURAUD", "2026 · MILLIONS OF WINDOWS · WET STREETS"},
      {222, Effect::Finale, Transition::Portal, 1.f, "FINALE", "1993 · ALL EFFECTS", "2026 · THE STUDIO"},
      {246, Effect::Credits, Transition::Cut, .4f, "CREDITS", "1993 · SINE SCROLLER", "2026"} }};
  struct TimelineFrame {
      double showSeconds = 0, localSeconds = 0, chapterDuration = 0, accentSeconds = 0;  // accent: seconds into the chapter
      int chapter = 0; Effect effect = Effect::Boot; Transition transition = Transition::Cut;
      float chapterProgress = 0, drive = 0;
      // music
      float energy = 0, bass = 0, hit = 0, kick = 0, snare = 0, treble = 0, lowOnsetPhase = 0;
      std::array<float, 24> spectrum{};
      // tracked grid
      int beatIndex = 0; float beatPhase = 0, barPhase = 0, beatSeconds = .5f; bool onDownbeatBar = false;
      // camera-safe smoothed signals (see parent timeline.cpp): drive smoothed ~0.7 s zero-phase, and an integrated flow clock
      float cameraDrive = 0; double flowSeconds = 0;
      // era (filled by Task 4; defaults here)
      float era = 0, wipe = 0, eraCut = 0; int swapCount = 0;
      const char* title = ""; const char* thenCard = ""; const char* nowCard = "";
  };
  double showDuration(); int chapterCount(); double chapterStart(int chapter); double wrapShowTime(double);
  TimelineFrame sampleTimeline(double showSeconds);
  std::string showInfoJson();   // {"title":"EVOLUTION","version":"0.1.0","duration":270,"music":"music/show.wav","tracks":[{"frames":...,"start":...}],"chapters":[{"index":0,"title":"BOOT","effect":"Boot","transition":"Cut","start":0,"duration":...,"accent":...}]}
  }
  ```
  Decisions: accent = time of the strongest `hit` envelope sample between 28 % and 76 % of the chapter (parent's rule, with the same onset bonus); `cameraDrive`/`flowSeconds` computed exactly as the parent's `cameraSeries` (two forward + two backward exponential passes, k = 1−exp(−1/(.35·rate))), precomputed once per process in a function-local static; `beatSeconds` = next−previous tracked beat (≥ .2); before the first beat `beatPhase = 0`. `static_assert(cues.size() == generated::chapterCount)` and a test that every `cues[i].target` equals `show_targets.json` (the test reads the JSON with a tiny parser or compares against a generated `targets[]` array that `direct_music.py` also emits into `cue_data.hpp` — choose the latter: emit `inline constexpr double chapterTargets[11]` and `static_assert` equality in `timeline.cpp`).

- [ ] **Step 1: Write the failing test** `timeline_test.cpp`: `showDuration()==270`; `chapterCount()==11`; `chapterStart(0)==0`; starts strictly ascending; `sampleTimeline(0).effect==Effect::Boot`; `sampleTimeline(chapterStart(7)+.5).effect==Effect::Raytrace`; `sampleTimeline(269.999).chapter==10`; `sampleTimeline(270).chapter==0` and `sampleTimeline(-5).chapter==0` (Review Focus 4); all spectrum values in [0,1] and finite at 0.25 s steps across the show; `beatPhase` in [0,1] and monotone within a beat (sample 1 ms apart); `flowSeconds` non-decreasing across the show at 10 ms steps; `showInfoJson()` contains `"chapters":[` and `"title":"RAYTRACE"`; accent for every chapter lies in [.28,.76]·duration.
- [ ] **Step 2: Run to verify it fails.** **Step 3: Implement** library `evo_show` (`timeline.cpp`, `music_curves.cpp`), `cues.hpp`. **Step 4: Run** `ctest -R timeline`. **Step 5: Commit** `Sample the directed timeline and music curves from the audio clock`.

---

### Task 4: Era and transition state

**Suggested model:** default.

**Files:**
- Create: `evolution/src/show/era.hpp`, `evolution/src/show/era.cpp`
- Modify: `evolution/src/show/timeline.cpp` (call `applyEra` at the end of `sampleTimeline`)
- Test: `evolution/tests/era_test.cpp`

**Interfaces:**
- Produces: `void applyEra(TimelineFrame&, const BeatGridView&)` filling `era`, `wipe`, `eraCut`, `swapCount`, where `BeatGridView` (defined in `timeline.hpp`) exposes `int beatAt(double trackSeconds)`, `double beatTime(int)`, `int bar(int)`, `int count()` for the chapter's track.
  Rules (all times in show seconds; A = chapter start + accent):
  - `Cut`: `era = 0` for Boot and Credits-then-only? No: `Cut` chapters use `era = cues[i].nowCard[0] ? 1 : 0`... keep it explicit: add `float eraFixed` to `Cue`? Simpler and explicit: `Cut` → `era = 0` for the whole chapter (Boot and Credits are 1993-styled with modern overlay drawn by the renderer, see spec 2.3). `wipe = 0`, `eraCut = 0`.
  - `Portal`: `era = smoothstep(A−1, A+1, t)`; `eraCut = exp(−8·max(0,t−A))` once `t ≥ A`, else 0.
  - `Wipe`: the wipe runs over the two bars ending at the downbeat at or before A (call it D; if the chapter has fewer than 2 bars before D, use the chapter start as the wipe start): `wipe = clamp((t − (D − 2 bars))/(2 bars), 0, 1)` where a bar = 4·beatSeconds measured at D; `era = wipe`; `eraCut = 0`.
  - `BeatSwap`: swap schedule from S = D − 4 bars (clamped to chapter start): between S and D−2 bars toggle at every bar line; between D−2 bars and D toggle at every beat; the state starts at 0 and is recomputed by counting toggles ≤ t (`swapCount`); `era = swapCount % 2` before D, `era = 1` from D on; `eraCut = exp(−10·(t − lastToggle))` capped to 1. If the chapter has fewer than 4 bars before D, start at the chapter start with per-beat toggles only (Review Focus 5).
- [ ] **Step 1: Write the failing test** `era_test.cpp`: (a) **determinism**: 200 pseudo-random times in [0,270) sampled directly vs after sampling an unrelated time first → every field bit-identical (`memcmp` on a packed copy is fine; compare fields explicitly) (Review Focus 1); (b) Portal chapter (Flyby): `era==0` at A−2, `≈.5` at A, `==1` at A+2; (c) Wipe chapter (Plasma): `wipe` increases monotonically over the two bars before D and `era==wipe`; (d) BeatSwap (Raytrace): `swapCount` at D ≥ 8 (4 bars of bar toggles + 2 bars of beat toggles ≥ 4 + 8 = 12 actually; assert ≥ 10), `era==1` at A+0.01 and at chapter end − 0.01, `era` alternates between consecutive beats inside the last two bars before D; (e) `Cut` (Boot): `era==0`, `wipe==0`; (f) no NaN anywhere at 5 ms steps across the show.
- [ ] **Step 2: Run to verify it fails.** **Step 3: Implement.** **Step 4: Run** `ctest -R "era|timeline"`. **Step 5: Commit** `Schedule the 1993/2026 transitions on the tracked beat grid`.

---

### Task 5: SDL3 audio player and the app clock

**Suggested model:** default.

**Files:**
- Create: `evolution/src/audio/player.hpp`, `evolution/src/audio/player.cpp`
- Create: `evolution/src/app/clock.hpp`, `evolution/src/app/clock.cpp`
- Modify: `evolution/src/app/options.hpp/.cpp` (add `--no-audio`, `--show-info`, `--music PATH` default `assets/music/show.wav` relative to the executable's `assets/` dir, `--mute`), `evolution/src/app/app.cpp` (use `ShowClock`), `evolution/src/main.cpp` (`--show-info` prints `showInfoJson()` and exits 0 without Vulkan or SDL video)
- Test: `evolution/tests/clock_test.cpp`, extend `evolution/tests/options_test.cpp`, CTest `show_info` (`Evolution --show-info`, `PASS_REGULAR_EXPRESSION "\"duration\":270"`), CTest `silent_mode` (runs `Evolution --render-stream --duration 0.2 --fps 10 --width 32 --height 18` with env `SDL_AUDIODRIVER=nonexistent`, expects exit 0 and stderr containing `SILENT MODE`).

**Interfaces:**
- Produces:
  ```cpp
  namespace evo::audio {
  class Player {   // SDL3: SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, {F32, 2, 48000}, callback)
  public:
      bool start(const std::filesystem::path& wav, double expectedSeconds); // false + status() on any failure; |duration − expected| > 0.5 frame → "Music length <x> s differs from the show <y> s" (Review Focus 2)
      void stop(); void seek(double); void setMuted(bool);
      double positionSeconds() const;   // transport position minus queued frames (SDL_GetAudioStreamQueued/8), wrapped
      bool active() const; std::string status() const;
  };
  }
  namespace evo {
  class ShowClock {    // the single time source for rendering
  public:
      ShowClock(bool audioWanted, const std::filesystem::path& wav, double showSeconds, double startAt);
      double now();                  // audio position when active, else start + monotonic elapsed (SDL_GetTicksNS), wrapped to the show
      void seek(double); void setMuted(bool);
      bool silent() const; std::string status() const;   // "SILENT MODE: <reason>" or "AUDIO: <file>"
  };
  }
  ```
  Keys in `runWindowed`: `←/→` previous/next chapter (`chapterStart`), `1–9,0` chapters 0–9, `M` mute, `Space` pause? No (YAGNI). The window title shows `EVOLUTION  <chapter title>  <mm:ss>`; the render-stream path uses `showSeconds = time + i/fps` directly (never the clock).
- [ ] **Step 1: Write the failing tests** (`clock_test.cpp`: a `ShowClock` constructed with `audioWanted=false` reports `silent()`, `now()` is non-decreasing over 3 calls and `seek(100)` makes the next `now()` ≥ 100 and < 100.5; options: `--no-audio`, `--mute`, `--music x.wav`, `--show-info`; CTest `show_info` and `silent_mode` as above). **Step 2: Fail.** **Step 3: Implement** (`evo_audio` gains `player.cpp` and links `SDL3::SDL3-static`; `evo_app` links `evo_show`, `evo_audio`). **Step 4: Run the full suite.** **Step 5: Commit** `Play the show audio and drive rendering from its clock`.

---

### Task 6: Make the frame visible

**Suggested model:** opus.

**Files:**
- Modify: `evolution/shaders/fill.comp` (push constants grow to 32 bytes: `{uint width; uint height; float time; float era; float kick; float beatPhase; float wipe; uint chapter;}`), `evolution/src/gfx/offscreen.hpp/.cpp` (`render(const show::TimelineFrame&)` overload; keep `render(double)` delegating with a frame sampled at that time), `evolution/src/app/app.cpp`, `evolution/src/app/capture.cpp` (sample the timeline at the clock/time and render the frame).
- Test: extend `evolution/tests/offscreen_test.cpp` (frame at Raytrace chapter start + 0.5 s differs from Boot at 0.5 s; two renders of the same frame identical), `evolution/tests/capture_test.py` unchanged but now the PNG at `--time 160` shows the 2026 palette.

**Interfaces:** the pattern: left of the wipe/era boundary (x < `wipe`·width when transition is Wipe, otherwise whole frame by `era`) the 1993 look = 4-colour banded checker (indexed feel), the 2026 look = smooth gradient; the circle radius pulses with `kick` (0.2 → 0.26·height) and its brightness with `1−beatPhase`; a thin vertical line marks the wipe edge. The `chapter` index selects the hue (`chapter/11`).
- [ ] Steps: failing test → fail → implement → `ctest --preset linux` all green → capture `--time 160 --capture-dir build-out/cap` and look at the PNG → commit `Show the timeline state in the test pattern`.

---

### Task 7: README and handoff

**Suggested model:** opus.

- Update `evolution/README.md`: music pipeline section (`make_placeholder.py`, `prepare_show.py <your-suno-track.mp3>`, what gets generated, that WAVs are not committed), new flags, keys, `SILENT MODE`. Update `evolution/docs/plans/HANDOFF.md` task table for step 1. Commit `Document the music pipeline and step 1 controls`.

---

## Self-review notes

- Spec 3.7 (sample-exact clock, generated analysis, deterministic frame) → Tasks 1–5; 2.2 transitions → Task 4; 3.9 CLI/keys → Task 5; 4 (ported tools) → Task 1; 5 (song requirements) → placeholder mirrors them (134 BPM, drop near 158 s); 6 (tests) → every task.
- Interface names used consistently: `readWav`, `Wav`, `Transport`, `Player`, `ShowClock`, `Effect`, `Transition`, `Cue`, `cues`, `TimelineFrame`, `sampleTimeline`, `showInfoJson`, `applyEra`, `BeatGridView`, generated arrays as listed in Task 1.
- Review Focus 1 → T4 (a); 2 → T5 `start()` length check; 3 → T5 `silent_mode`; 4 → T3 boundary asserts; 5 → T4 short-window rule.
- Task 4's rule text for `Cut` was tightened while writing: `Cut` chapters are `era = 0`; the renderer draws the modern overlay for Credits itself (spec 2.3). Task 1's section layout was corrected inline to put the drop at bar 88 (≈158 s).
