# Music receipts

`tools/prepare_show.py` writes these JSON files next to the generated headers in `src/show/generated/`.
They record what the generated data was measured from and how, so a change of soundtrack is visible in a
diff. They are committed; the WAVs they describe (`assets/music/*.wav`) are not.

Regenerate everything for the placeholder with:

    python3 tools/make_placeholder.py assets/music/placeholder.wav
    python3 tools/prepare_show.py --targets tools/show_targets_placeholder.json assets/music/placeholder.wav

For real tracks, pass them in show order: `python3 tools/prepare_show.py first.mp3 second.mp3`.

| File | Written by | Contents |
| --- | --- | --- |
| `show-audio.json` | `prepare_show.py` | The concatenated runtime file `assets/music/show.wav`: total frames and duration, the start frame of each track, each track's runtime file, frames and SHA-256, and the SHA-256 of `show.wav` (also in `cue_data.hpp` as `sourceSha256`). |
| `analysis.json` | `analyze_music.py` | The 20 Hz envelopes (energy, bass, hit) and the 24 log-spaced spectrum bands behind `music_data.hpp`: method, band edges, per-track frames, sample counts, transient peak count and SHA-256. |
| `reactivity.json` | `analyze_reactivity.py` | The 100 Hz low/mid/high attack curves and low-band onset phase behind `reactivity_data.hpp`: method, release and refractory times, per-track peak counts and how often each channel is active or saturated. |
| `beats.json` | `beat_grid.py` | The tracked beat grid behind `beat_data.hpp`: method, median BPM over the show and per track, beat and downbeat counts, the comb tempo estimate with the scored tempo candidates and which method won, and tracker jitter. A form-directed track's bar lines come from its form (`bar_lines_from`). |
| `form.json` | `song_form.py` | Each track's form: method, bar and phrase phase, phrase starts, section boundaries (rises and falls with their contrast), drops with their strength, and the events Act III's chapters are placed on (`first drop`, `biggest drop`, `tail`, `track start`, `track end`); `directs` marks the track whose chapters follow it. |
| `direction.json` | `direct_music.py` | The chapter cuts behind `cue_data.hpp`: method and bounds, and for each chapter its title, target, directed start, shift, measured attack and whether it sits on a beat, a downbeat and a phrase start; a form-directed chapter also has its `form` spec, the kind of cut point it starts on and its `accent_beat` (the beat `chapterAccents` holds). |

All times are in seconds. Chapter targets, starts and accent beats are show seconds; beat times and the form's
times in `beats.json`, `beat_data.hpp` and `form.json` are track seconds.

## How Act III follows its track

Acts I and II give their chapter targets in seconds. Act III's chapters (those with a `"form"` in
`tools/show_targets.json`) are placed on the form of its track instead, so they follow whatever track is put
there: REBOOT runs over the intro and crosses the glass a beat after the phrase in its middle, GALAXY starts on
the first drop, PLASMA PLANET to METROPOLIS fall at fixed fractions of the way from GALAXY to ASCENDANT, each on
the nearest phrase start or section boundary, ASCENDANT starts a phrase before the biggest drop and lands its
logo on it, and CREDITS and THE END take the outro (THE END from the tail, where the beat stops). Each accent is
a tracked beat: the strongest rise or phrase start in the middle of the chapter unless the chapter names one.
`tools/direct_music.py` explains the expressions; the code and the tests read every Act III time from the
generated data.

## Replacing a track

Act III's track (Cyberpunk Assembly, extended) stands in until the final one. To replace it, from `evolution/`:

1. Put the new file in `music/source/`, for example `music/source/Final Track.mp3`.
2. Update the list: the third name in `"tracks"` in `tools/show_targets.json`, and the track in the command
   below (also in `README.md` and `licenses/MUSIC-NOTICE.md`, with the new SHA-256 and frame count).
3. Run the one command that rebuilds the show audio, the generated headers and these receipts:

       python3 tools/prepare_show.py "music/source/Amiga Trance Demo.mp3" "music/source/Guitar Trance.mp3" "music/source/Final Track.mp3"

   It prints the track's form (its first drop, biggest drop and tail) and every chapter's start and accent.
4. Run the tests:

       cmake --build --preset linux && xvfb-run -a ctest --preset linux -j2 --timeout 900

   and regenerate the byte baseline, which any new audio changes:

       python3 tests/baseline/hash_outputs.py build/linux/bin/Evolution build/linux/build-out/baseline \
           --check tests/baseline/SUMS --luminance tests/baseline/LUMINANCE --write
       cp build/linux/build-out/baseline/SUMS build/linux/build-out/baseline/LUMINANCE tests/baseline/

No code changes. If `prepare_show.py` stops because a chapter cannot fit (no cut point keeps it between 5 s
and `max_chapter_seconds`, or an accent falls outside its chapter), adjust the Act III block in
`tools/show_targets.json` (the chapters with a `"form"`; its `"act_iii"` note says the same):

- `max_chapter_seconds`: raise it when a long track makes the chapters longer than 40 s;
- the events and bar offsets of GALAXY (`first drop`), ASCENDANT (`biggest drop -8 bars`: the phrase of
  lift-off before the landing) and THE END (`tail`);
- the fractions of PLASMA PLANET to METROPOLIS (between GALAXY and ASCENDANT) and of CREDITS (between
  ASCENDANT and THE END);
- the accents: REBOOT `middle +1 beat` (the crossing on the downbeat a beat before it), ASCENDANT
  `biggest drop`, CREDITS `start +11 bars +3 beats` (the greetings scroll for 11 bars before THE END? enters;
  `min_bars` keeps 13) and THE END `track end -2 bars -1 s` (the tube's two bars of afterglow end before the
  music does).
