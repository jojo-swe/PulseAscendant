"""Find the form of a track: its bar and phrase lines, the section changes on them and its drops.

Dance tracks change section on phrase lines (usually every 8 bars), and a breakdown or a drop changes the
level of the whole mix, the bass most of all. The beat tracker (tools/beat_grid.py) follows the pulse but can
put a four-on-the-floor track's bar lines a beat or two off where every beat is equally loud; the form reads
them from the section changes instead.

Method: the level of each tracked beat is the mean of the 20 Hz energy and bass envelopes
(tools/analyze_music.py) over it, and the contrast at a beat the mean level of the bar after it minus that of
the bar before it. The bar phase is the beat phase (mod 4), and the phrase phase the bar phase (mod 8 bars),
whose beats carry the most squared contrast. A boundary is a bar line whose contrast is at least BOUNDARY in
size and the largest of its sign within a bar either side: a rise or a fall. A drop is a rise after which the
next 8 bars are louder than the 8 bars before by at least DROP; its strength is that difference plus half the
level of the 8 bars after, so the biggest drop is the strongest lift into the loudest section. The tail is the
last fall at least TAIL_SECONDS before the end, where the beat stops.

The cut points are the phrase starts and the boundaries: where a chapter of a form-directed act may start
(tools/direct_music.py). Every time is in track seconds and lies on a tracked beat.

Usage: python3 tools/song_form.py <track.wav> [...]   (prints each track's form)
"""
import argparse
from pathlib import Path

import numpy as np

METER = 4            # beats a bar
PHRASE_BARS = 8      # bars a phrase
BOUNDARY = .12       # the least contrast of a boundary (levels are 0..1)
DROP = .1            # the least 8-bar lift of a drop
TAIL_SECONDS = 5.    # the tail leaves at least this much of the track
METHOD = ('beat level = mean of the 20 Hz energy and bass envelopes over the tracked beat; contrast = mean level '
          'of the bar after minus the bar before; bar and phrase phase = the beat phase (mod 4) and bar phase '
          f'(mod {PHRASE_BARS} bars) with the most squared contrast; boundary = bar line with |contrast| >= '
          f'{BOUNDARY}, the largest of its sign within a bar; drop = rise whose next 8 bars are >= {DROP} louder '
          'than the 8 before, strength = that lift + half the level after; tail = last fall >= '
          f'{TAIL_SECONDS:g} s before the end')


def beat_levels(analysis, times):
    """Mean of the energy and bass envelopes over each tracked beat (the last one runs to the track end)."""
    frames, data, _ = analysis
    envelope = data[:, :2].astype(float).mean(axis=1) / 255
    rate = 20
    edges = [int(t * rate) for t in times] + [len(envelope)]
    return np.array([envelope[a:max(b, a + 1)].mean() if a < len(envelope) else 0.
                     for a, b in zip(edges[:-1], edges[1:])])


def window_mean(level, start, stop):
    start, stop = max(0, start), min(len(level), stop)
    return float(level[start:stop].mean()) if stop > start else float('nan')


def analyze(analysis, grid):
    """The form of one track from analyze_music.analyze() and beat_grid.analyze() of it."""
    times = list(grid['times'])
    count = len(times)
    duration = analysis[0] / 48000
    level = beat_levels(analysis, times) if count else np.zeros(0)
    bar = METER
    contrast = np.zeros(count)
    for k in range(bar, count - bar + 1):
        contrast[k] = window_mean(level, k, k + bar) - window_mean(level, k - bar, k)
    weight = contrast ** 2
    bar_phase = int(np.argmax([weight[m::bar].sum() for m in range(bar)])) if count else 0
    phrase = bar * PHRASE_BARS
    phrase_phase = bar_phase + bar * int(np.argmax([weight[bar_phase + bar * j::phrase].sum()
                                                    for j in range(PHRASE_BARS)])) if count else 0
    bar_lines = list(range(bar_phase, count, bar))

    def lift(k):
        before, after = window_mean(level, k - phrase, k), window_mean(level, k, k + phrase)
        return after - before, after

    boundaries = []
    for k in bar_lines:
        c = contrast[k]
        if abs(c) < BOUNDARY:
            continue
        neighbours = [contrast[j] for j in (k - bar, k + bar) if 0 <= j < count]
        if (c > 0 and all(c >= n for n in neighbours)) or (c < 0 and all(c <= n for n in neighbours)):
            boundaries.append(dict(beat=k, time=round(times[k], 4), kind='rise' if c > 0 else 'fall',
                                   contrast=round(float(c), 3)))
    drops = []
    for b in boundaries:
        if b['kind'] != 'rise':
            continue
        gain, after = lift(b['beat'])
        if gain >= DROP:
            drops.append(dict(beat=b['beat'], time=b['time'], lift=round(gain, 3),
                              strength=round(gain + .5 * after, 3)))
    phrase_starts = [k for k in bar_lines if (k - phrase_phase) % phrase == 0]
    cuts = sorted(set(phrase_starts) | {b['beat'] for b in boundaries})
    falls = [b for b in boundaries if b['kind'] == 'fall' and duration - b['time'] >= TAIL_SECONDS]
    events = {'track start': 0., 'track end': round(duration, 6)}
    if drops:
        events['first drop'] = drops[0]['time']
        events['biggest drop'] = max(drops, key=lambda d: (d['strength'], -d['time']))['time']
    if falls:
        events['tail'] = falls[-1]['time']
    return dict(source=grid.get('source', ''), beats=count, duration=round(duration, 6), bar_phase=bar_phase,
                phrase_phase=phrase_phase, phrase_bars=PHRASE_BARS,
                bar_position=[(k - bar_phase) % bar for k in range(count)],
                phrase_starts=[round(times[k], 4) for k in phrase_starts], cut_beats=cuts,
                boundaries=boundaries, drops=drops, events=events, times=times)


def receipt(form, directs):
    """What music/form.json records of a form: everything but the per-beat tables."""
    keep = ('source', 'beats', 'duration', 'bar_phase', 'phrase_phase', 'phrase_bars', 'phrase_starts',
            'boundaries', 'drops', 'events')
    return dict(directs=directs, **{k: form[k] for k in keep})


def main():
    import analyze_music
    import beat_grid
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('tracks', nargs='+', type=Path, help='stereo 48 kHz PCM16 WAV files')
    args = parser.parse_args()
    for path in args.tracks:
        form = analyze(analyze_music.analyze(path), beat_grid.analyze(path))
        print(f'{path.name}: {form["beats"]} beats, bar phase {form["bar_phase"]}, phrase phase '
              f'{form["phrase_phase"]}')
        for name, time in form['events'].items():
            print(f'  {name:12} {time:9.3f}')
        for b in form['boundaries']:
            drop = next((d for d in form['drops'] if d['beat'] == b['beat']), None)
            extra = f'  drop {drop["strength"]:.3f}' if drop else ''
            print(f'  {b["time"]:9.3f}  beat {b["beat"]:4d}  {b["kind"]:4}  {b["contrast"]:+.3f}{extra}')


if __name__ == '__main__':
    main()
