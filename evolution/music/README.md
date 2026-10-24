# Music receipts

`tools/prepare_show.py` writes these JSON files next to the generated headers in `src/show/generated/`.
They record what the generated data was measured from and how, so a change of soundtrack is visible in a
diff. They are committed; the WAVs they describe (`assets/music/*.wav`) are not.

Regenerate everything for the placeholder with:

    python3 tools/make_placeholder.py assets/music/placeholder.wav
    python3 tools/prepare_show.py assets/music/placeholder.wav

For real tracks, pass them in show order: `python3 tools/prepare_show.py first.mp3 second.mp3`.

| File | Written by | Contents |
| --- | --- | --- |
| `show-audio.json` | `prepare_show.py` | The concatenated runtime file `assets/music/show.wav`: total frames and duration, the start frame of each track, each track's runtime file, frames and SHA-256, and the SHA-256 of `show.wav` (also in `cue_data.hpp` as `sourceSha256`). |
| `analysis.json` | `analyze_music.py` | The 20 Hz envelopes (energy, bass, hit) and the 24 log-spaced spectrum bands behind `music_data.hpp`: method, band edges, per-track frames, sample counts, transient peak count and SHA-256. |
| `reactivity.json` | `analyze_reactivity.py` | The 100 Hz low/mid/high attack curves and low-band onset phase behind `reactivity_data.hpp`: method, release and refractory times, per-track peak counts and how often each channel is active or saturated. |
| `beats.json` | `beat_grid.py` | The tracked beat grid behind `beat_data.hpp`: method, median BPM over the show and per track, beat and downbeat counts, the comb tempo estimate with the scored tempo candidates and which method won, and tracker jitter. |
| `direction.json` | `direct_music.py` | The chapter cuts behind `cue_data.hpp`: method and bounds, and for each chapter its title, authored target (`tools/show_targets.json`), directed start, shift, measured attack and whether it sits on a beat, a downbeat and a 4-bar phrase start. |

All times are in seconds. Chapter targets and starts are show seconds; beat times in `beats.json` and
`beat_data.hpp` are track seconds.
