"""Turn one or more soundtrack files into the show audio, the generated headers and the receipts.

Ported from PulseAscendant tools/prepare_show.py (MIT), generalised from three fixed, hash-checked acts
to any number of tracks given on the command line. Requires numpy; ffmpeg only for inputs that are not
already stereo 48 kHz PCM16 WAV (e.g. MP3).

    python3 tools/prepare_show.py [--root DIR] <track1.(wav|mp3)> [<track2> ...]

For each track (in show order): convert or copy to <root>/assets/music/<stem>.wav, then concatenate the
tracks with no gap into <root>/assets/music/show.wav. The analysis, reactivity, beat grid and direction
run over the per-track WAVs and write <root>/src/show/generated/{music_data,reactivity_data,beat_data,
cue_data}.hpp and <root>/music/{analysis,reactivity,beats,direction,show-audio}.json. <root> defaults to
evolution/; the chapter targets always come from tools/show_targets.json beside this script.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import wave
from pathlib import Path

import analyze_music
import analyze_reactivity
import beat_grid
import direct_music

ROOT = Path(__file__).resolve().parents[1]
FORMAT = (48000, 2, 2)  # sample rate, channels, bytes per sample


def is_show_format(path):
    try:
        with wave.open(str(path), 'rb') as audio:
            return (audio.getframerate(), audio.getnchannels(), audio.getsampwidth()) == FORMAT \
                and audio.getcomptype() == 'NONE'
    except (wave.Error, EOFError):
        return False


def runtime_track(source, music_dir):
    """Place source as stereo 48 kHz PCM16 at music_dir/<stem>.wav, converting with ffmpeg if needed."""
    destination = music_dir / f'{source.stem}.wav'
    music_dir.mkdir(parents=True, exist_ok=True)
    if is_show_format(source):
        if source.resolve() != destination.resolve():
            shutil.copyfile(source, destination)
    else:
        if source.resolve() == destination.resolve():
            raise ValueError(f'{source} is not stereo 48 kHz PCM16 and would be converted onto itself')
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(source), '-map', '0:a:0',
                        '-ar', '48000', '-ac', '2', '-c:a', 'pcm_s16le', str(destination)], check=True)
    return destination


def concatenate(tracks, destination):
    with wave.open(str(destination), 'wb') as output:
        output.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
        for track in tracks:
            with wave.open(str(track), 'rb') as audio:
                while data := audio.readframes(48000):
                    output.writeframesraw(data)


def main():
    parser = argparse.ArgumentParser(description='Prepare the show audio, generated headers and receipts.')
    parser.add_argument('tracks', nargs='+', type=Path, help='soundtrack files in show order (WAV or MP3)')
    parser.add_argument('--root', type=Path, default=ROOT, help='directory holding assets/, music/ and src/')
    parser.add_argument('--targets', type=Path, default=ROOT / 'tools/show_targets.json')
    args = parser.parse_args()
    root = args.root.resolve()
    stems = [t.stem for t in args.tracks]
    if 'show' in stems or len(set(stems)) != len(stems):
        raise ValueError('Track file names must be unique and must not be "show"')
    music_dir, generated, receipts = root / 'assets/music', root / 'src/show/generated', root / 'music'

    tracks = [runtime_track(source.resolve(), music_dir) for source in args.tracks]
    show = music_dir / 'show.wav'
    concatenate(tracks, show)
    show_sha256 = hashlib.sha256(show.read_bytes()).hexdigest()

    analyses = [analyze_music.analyze(track) for track in tracks]
    analysis = analyze_music.write_outputs(tracks, analyses, generated / 'music_data.hpp',
                                           receipts / 'analysis.json')
    analyze_reactivity.write_outputs(*analyze_reactivity.analyze_tracks(tracks),
                                     generated / 'reactivity_data.hpp', receipts / 'reactivity.json')
    grids = [beat_grid.analyze(track) for track in tracks]
    beat_grid.write_outputs(grids, generated / 'beat_data.hpp', receipts / 'beats.json')
    chapters, max_chapter = direct_music.load_targets(args.targets)
    starts, chapter_reports, track_reports = direct_music.direct_show(tracks, analyses, grids, chapters,
                                                                      max_chapter)
    direct_music.write_outputs(chapters, starts, chapter_reports, track_reports, max_chapter, show_sha256,
                               generated / 'cue_data.hpp', receipts / 'direction.json')

    frames = [a[0] for a in analyses]
    track_start_frames = [sum(frames[:i]) for i in range(len(frames))]
    receipts.mkdir(parents=True, exist_ok=True)
    (receipts / 'show-audio.json').write_text(json.dumps(dict(
        frames=sum(frames), duration=sum(frames) / 48000, sample_rate=48000, channels=2,
        encoding='PCM signed 16 bit', track_start_frames=track_start_frames,
        tracks=[dict(source=source.name, runtime_file=str(track.relative_to(root)), frames=report['frames'],
                     sha256=report['runtime_sha256'])
                for source, track, report in zip(args.tracks, tracks, analysis['tracks'])],
        sha256=show_sha256, runtime_file=str(show.relative_to(root))), indent=2) + '\n', encoding='utf-8')

    print(f'show.wav: {sum(frames)} frames, {sum(frames) / 48000:.3f} s, sha256 {show_sha256}')
    for grid in grids:
        print(f'{grid["source"]}: {grid["bpm_median"]} BPM, {grid["beats"]} beats, {grid["downbeats"]} downbeats')
    print(f'Direction (max chapter {max_chapter:g} s):')
    print(f'  {"chapter":10} {"target":>8} {"start":>10} {"shift":>8}  on_beat downbeat phrase attack')
    for c in chapter_reports:
        print(f'  {c["title"]:10} {c["target"]:8.3f} {c["start"]:10.4f} {c["shift"]:+8.4f}  '
              f'{c["on_beat"]!s:7} {c["on_downbeat"]!s:8} {c["phrase"]!s:6} {c["attack"]:.3f}')


if __name__ == '__main__':
    main()
