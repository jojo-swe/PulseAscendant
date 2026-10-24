"""Place editorial scene changes on tracked beats, preferring strong attacks, bar lines and phrase changes.

Ported from PulseAscendant tools/direct_music.py (MIT). direct() is the parent's algorithm with two
changes: the maximum chapter length is a parameter (default 18 s, the parent's value; EVOLUTION's
targets are ~24 s apart, so tools/show_targets.json sets it), and starts are reported as exact tracked
beat times rather than rounded to 1/400 s. The I/O is new: the chapter targets come from
tools/show_targets.json (show seconds, each with a track index), any number of tracks are directed in
show order, and the result is cue_data.hpp in evo::show::generated plus music/direction.json.

With a beat grid (tools/beat_grid.py) every chapter cut lands exactly on a tracked beat, preferring
strong attacks, bar lines and 4-bar phrase starts. Without one, the original attack-only direction is
used. Each track's first chapter is pinned to that track's start.

Usage: python3 tools/direct_music.py [--targets T] [--show S] [--header H] [--receipt R] <track1.wav> ...
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from analyze_music import analyze
import beat_grid

ROOT = Path(__file__).resolve().parents[1]
WINDOW_SECONDS = 2.5
MIN_CHAPTER_SECONDS = 5


def direct(data, spectrum, targets, duration, downbeats=None, phrases=None, beats=None, max_chapter=18.):
    spectrum = spectrum.astype(float)/255
    # Persistent spectral changes favor phrase entrances over a random loud note.
    change = np.zeros(len(data))
    for i in range(10, len(data)-10):
        change[i] = np.abs(spectrum[i:i+10].mean(axis=0)-spectrum[i-10:i].mean(axis=0)).mean()
    change /= max(.001, float(np.percentile(change, 95)))
    hit, bass = data[:, 2]/255., data[:, 1]/255.
    salience = hit*1.6 + np.minimum(change, 1.5)*.7 + bass*.15
    peaks = np.flatnonzero((hit[1:-1] >= hit[:-2]) & (hit[1:-1] > hit[2:]) & (hit[1:-1] > .15))+1
    states = [(0, 0., [])]
    for cue, target in enumerate(targets[1:], 1):
        low, high = max(0, int((target-2.5)*20)), min(len(data)-1, int((target+2.5)*20))
        grid = beats if beats is not None else downbeats
        on_beat = [] if grid is None else [d for d in grid if low <= d*20 <= high]
        if on_beat:
            # Cut exactly on a tracked beat. Times stay in seconds; salience is read at the nearest 20 Hz frame.
            candidates = on_beat
        else:
            candidates = list(peaks[(peaks >= low) & (peaks <= high)])
            # A phrase entrance may have no percussive attack. Include its strongest spectral change.
            candidates += [int(target*20), low+int(np.argmax(salience[low:high+1]))]
            candidates = [c/20 for c in candidates]
        next_states = []
        phrase_set, bar_set = set(phrases or []), set(downbeats or [])
        for time in sorted(set(candidates)):
            point = min(len(data)-1, int(round(time*20)))
            if duration-time < 5*(len(targets)-cue):
                continue
            if cue == len(targets)-1 and duration-time > max_chapter:
                continue
            eligible = [s for s in states if 5-1e-8 <= time-s[0] <= max_chapter]
            if not eligible:
                continue
            previous = max(eligible, key=lambda s:s[1])
            # Look slightly ahead of the bar line: an attack lands within ~50 ms of the tracked beat.
            local = float(salience[max(0,point-1):point+2].max())
            # Strong attacks first; bar lines and 4-bar phrase starts break ties.
            score = local-.09*abs(time-target)+(.15 if time in bar_set else 0.)+(.15 if time in phrase_set else 0.)
            next_states.append((time, previous[1]+score, previous[2]+[time]))
        if not next_states:
            raise ValueError(f'No valid musical edit for cue {cue}')
        states = next_states
    starts = [0.] + max(states, key=lambda s:s[1])[2]
    # Attack within +/-50 ms (one 20 Hz frame) of a time, applied equally to cuts and nominal targets.
    peak = lambda t: float(hit[max(0, round(t*20)-1):round(t*20)+2].max())
    report = [dict(nominal=target, start=round(time, 4), shift=round(time-target, 4), attack=round(peak(time),3),
                   on_beat=beats is not None and time in set(beats), on_downbeat=downbeats is not None and time in set(downbeats), phrase=time in set(phrases or []),
                   nominal_attack=round(peak(target),3)) for target,time in zip(targets, starts)]
    return starts, report


def load_targets(path):
    config = json.loads(Path(path).read_text(encoding='utf-8'))
    return config['chapters'], float(config.get('max_chapter_seconds', 18))


def direct_show(paths, analyses, grids, chapters, max_chapter):
    """Direct every track; returns show-second chapter starts and per-chapter / per-track reports.

    analyses: analyze_music.analyze() per track; grids: beat_grid.analyze() per track."""
    tracks = [c['track'] for c in chapters]
    if tracks != sorted(tracks) or set(tracks) != set(range(len(paths))):
        raise ValueError('Chapters must be in show order and every track needs at least one chapter')
    starts, chapter_reports, track_reports, track_start = [], [], [], 0.
    for index, ((frames, data, spectrum), grid) in enumerate(zip(analyses, grids)):
        own = [c for c in chapters if c['track'] == index]
        # The first chapter of a track is pinned to the track start; the rest are directed in track time.
        local_targets = [0] + [c['target'] - track_start for c in own[1:]]
        downbeats = [t for t, b in zip(grid['times'], grid['bar_position']) if b == 0]
        phrases = downbeats[::4]
        local, report = direct(data, spectrum, local_targets, frames/48000, downbeats, phrases, grid['times'],
                               max_chapter=max_chapter)
        for chapter, time, entry in zip(own, local, report):
            start = round(track_start + time, 4)
            starts.append(start)
            chapter_reports.append(dict(title=chapter['title'], track=index, target=chapter['target'],
                                        start=start, shift=round(start - chapter['target'], 4),
                                        **{k: entry[k] for k in ('attack', 'on_beat', 'on_downbeat', 'phrase',
                                                                 'nominal_attack')}))
        cuts = report[1:]  # the pinned first chapter is not a cut
        mean = lambda key: float(np.mean([c[key] for c in cuts])) if cuts else 0.
        track_reports.append(dict(track=index, source=Path(paths[index]).name, start_seconds=track_start,
                                  mean_attack=mean('attack'),
                                  on_beat=sum(c['on_beat'] for c in cuts),
                                  on_downbeat=sum(c['on_downbeat'] for c in cuts),
                                  phrase_starts=sum(c['phrase'] for c in cuts),
                                  nominal_mean_attack=mean('nominal_attack')))
        track_start += frames / 48000
    return starts, chapter_reports, track_reports


def write_outputs(chapters, starts, chapter_reports, track_reports, max_chapter, show_sha256, header_path,
                  receipt_path):
    count = len(chapters)
    header = ['// Generated by tools/direct_music.py; regenerate when any soundtrack changes.',
              '// Measured facts in music/direction.json.',
              '#pragma once', 'namespace evo::show::generated {',
              f'inline constexpr int chapterCount = {count};',
              '// Directed chapter starts in show seconds, each on a tracked beat; the first is 0.',
              'inline constexpr double chapterStarts[chapterCount] = {' + ','.join(f'{s:.4f}' for s in starts) + '};',
              '// The authored targets from tools/show_targets.json, in show seconds.',
              'inline constexpr double chapterTargets[chapterCount] = {'
              + ','.join(f'{float(c["target"]):.4f}' for c in chapters) + '};',
              '// SHA-256 of assets/music/show.wav, the audio this data was measured from.',
              f'inline constexpr const char* sourceSha256 = "{show_sha256}";',
              '}']
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text('\n'.join(header)+'\n', encoding='utf-8')
    method = ('Dynamic programming over tracked beats, scored by measured attacks and persistent spectral changes '
              f'with bar-line and 4-bar phrase bonuses; +/-{WINDOW_SECONDS} s; {MIN_CHAPTER_SECONDS}-{max_chapter:g} s '
              f'chapter bounds (max_chapter_seconds = {max_chapter:g} from tools/show_targets.json; '
              'the parent used 18); '
              'each track\'s first chapter pinned to the track start; starts are exact tracked beat times')
    report = dict(method=method, window_seconds=WINDOW_SECONDS, min_chapter_seconds=MIN_CHAPTER_SECONDS,
                  max_chapter_seconds=max_chapter, source_sha256=show_sha256, chapters=chapter_reports,
                  tracks=track_reports)
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser(description='Place chapter cuts on tracked beats near authored targets.')
    parser.add_argument('tracks', nargs='+', type=Path, help='stereo 48 kHz PCM16 WAV files, in show order')
    parser.add_argument('--targets', type=Path, default=ROOT / 'tools/show_targets.json')
    parser.add_argument('--show', type=Path, default=ROOT / 'assets/music/show.wav',
                        help='the concatenated show audio, hashed into cue_data.hpp')
    parser.add_argument('--header', type=Path, default=ROOT / 'src/show/generated/cue_data.hpp')
    parser.add_argument('--receipt', type=Path, default=ROOT / 'music/direction.json')
    args = parser.parse_args()
    chapters, max_chapter = load_targets(args.targets)
    analyses = [analyze(path) for path in args.tracks]
    grids = [beat_grid.analyze(path) for path in args.tracks]
    starts, chapter_reports, track_reports = direct_show(args.tracks, analyses, grids, chapters, max_chapter)
    write_outputs(chapters, starts, chapter_reports, track_reports, max_chapter,
                  hashlib.sha256(args.show.read_bytes()).hexdigest(), args.header, args.receipt)
    print(json.dumps(track_reports))


if __name__ == '__main__':
    main()
