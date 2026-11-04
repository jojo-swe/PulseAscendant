# EVOLUTION — music credits and provenance

Three tracks by the artist **joyolo**, used with permission in this free, non-commercial demo. The music is
**not** covered by the code's MIT licence and is not relicensed by this repository. All rights in the music
remain with joyolo.

| Act | Title | Source MP3 SHA-256 | Runtime frames (48 kHz) |
|---|---|---|---|
| I | Amiga Trance Demo | ab0202db20241cd1bf2e79b981e6ac541fb9881fb7eda0b7128ccc9c89fd3a77 | 9 312 000 (194.00 s) |
| II | Guitar Trance | 766fc0b437b1ee252849d8ad8828799de9a680fc1ca7f931e862bcd4afc45d90 | 10 483 200 (218.40 s) |
| III | Cyberpunk Assembly (extended) | a9fbe04d340d4196a246a84037efd7f9995d32eb66c260743c5ee6be5b4949b8 | 15 610 560 (325.22 s) |

All three are used as complete songs at original speed; a 3 ms raised-cosine
fade at each act join (inaudible), otherwise no fades. Act III is an extended edit of
Cyberpunk Assembly (269.56 s): its four phrases from 0:42.17 to 1:51.83 play a second time
at 3:29.14, cut on the downbeats with 90 ms crossfades, which makes it 325.22 s. They are concatenated
without silence into `assets/music/show.wav` (35 405 760 frames, 737.62 s). Exact frame counts, per-track start frames and SHA-256 digests are
recorded in `music/show-audio.json`; the analysis receipts are the other JSON files
in `music/`.

Audio files are not committed: source MP3s live under `music/source/` and runtime
WAVs under `assets/music/`, both git-ignored. Regenerate with
`python3 tools/prepare_show.py "music/source/Amiga Trance Demo.mp3" "music/source/Guitar Trance.mp3" "music/source/Cyberpunk Assembly Extended.mp3"`.

Future Crew is credited for the original 1993 Second Reality demo, selected effect
ideas and the 14-vertex / 24-triangle glenz geometry (Unlicense). These tracks are
not Future Crew music.
