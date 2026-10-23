# EVOLUTION — music credits and provenance

Two tracks, generated with Suno by the project owner (artist metadata **joyolo**),
supplied for this free, non-commercial demo. The music is **not** covered by the
code's MIT licence and is not relicensed by this repository. Applicable creator
rights and Suno's terms remain in effect: https://suno.com/terms-of-service

| Act | Title | Suno ID | Source MP3 SHA-256 | Runtime frames (48 kHz) |
|---|---|---|---|---|
| I | Adventurous Uplift | 29b80971-2988-41e1-b658-00291a06b4ae | f82793aa129acfd5d9d8efbf15e67b08647666c09b615a38b99d4063855028d0 | 13 916 160 (289.92 s) |
| II | Rigid Arpeggios | 2a49b46a-28b5-40f9-b6e8-195ba877380a | eab637ad1cd987632b1f76a6209c901a1fb2dcda683bcb6d4e4c00761349255d | 14 340 480 (298.76 s) |

Both complete songs are used at their original speed, with no fades, concatenated
without silence into `assets/music/show.wav` (28 256 640 frames, 588.68 s). Exact
frame counts, per-track start frames and SHA-256 digests are recorded in
`music/show-audio.json`; the analysis receipts are the other JSON files in `music/`.

Audio files are not committed: source MP3s live under `music/source/` and runtime
WAVs under `assets/music/`, both git-ignored. Regenerate with
`python3 tools/prepare_show.py "music/source/Adventurous Uplift.mp3" "music/source/Rigid Arpeggios.mp3"`.

Future Crew is credited for the original 1993 Second Reality demo, selected effect
ideas and the 14-vertex / 24-triangle glenz geometry (Unlicense). These tracks are
not Future Crew music.
