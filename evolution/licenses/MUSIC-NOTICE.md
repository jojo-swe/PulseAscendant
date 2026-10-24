# EVOLUTION — music credits and provenance

Three tracks, generated with Suno by the project owner (Jojo, artist metadata
**joyolo**), supplied for this free, non-commercial demo. The music is **not**
covered by the code's MIT licence and is not relicensed by this repository.
Applicable creator rights and Suno's terms remain in effect:
https://suno.com/terms-of-service

| Act | Title | Suno ID | Source MP3 SHA-256 | Runtime frames (48 kHz) |
|---|---|---|---|---|
| I | Amiga Trance Demo | [2762f558-2512-4544-8955-3d2bfc14c861](https://suno.com/song/2762f558-2512-4544-8955-3d2bfc14c861) | ab0202db20241cd1bf2e79b981e6ac541fb9881fb7eda0b7128ccc9c89fd3a77 | 9 312 000 (194.00 s) |
| II | Guitar Trance | [96b24189-bb02-4dac-a607-4309a5ef0a7e](https://suno.com/song/96b24189-bb02-4dac-a607-4309a5ef0a7e) | 766fc0b437b1ee252849d8ad8828799de9a680fc1ca7f931e862bcd4afc45d90 | 10 483 200 (218.40 s) |
| III | Rigid Arpeggios | [2a49b46a-28b5-40f9-b6e8-195ba877380a](https://suno.com/song/2a49b46a-28b5-40f9-b6e8-195ba877380a) | eab637ad1cd987632b1f76a6209c901a1fb2dcda683bcb6d4e4c00761349255d | 14 340 480 (298.76 s) |

Amiga Trance Demo (3:14; styles techno, trance, chiptune) and Guitar Trance (3:38;
techno, trance, demo-scene) were created on 11 October 2026.

All three complete songs are used at their original speed, with no fades,
concatenated without silence into `assets/music/show.wav` (34 135 680 frames,
711.16 s). Exact frame counts, per-track start frames and SHA-256 digests are
recorded in `music/show-audio.json`; the analysis receipts are the other JSON files
in `music/`.

Audio files are not committed: source MP3s live under `music/source/` and runtime
WAVs under `assets/music/`, both git-ignored. Regenerate with
`python3 tools/prepare_show.py "music/source/Amiga Trance Demo.mp3" "music/source/Guitar Trance.mp3" "music/source/Rigid Arpeggios.mp3"`.

Future Crew is credited for the original 1993 Second Reality demo, selected effect
ideas and the 14-vertex / 24-triangle glenz geometry (Unlicense). These tracks are
not Future Crew music.
