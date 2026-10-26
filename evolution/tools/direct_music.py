"""Place editorial scene changes on tracked beats, preferring strong attacks, bar lines and phrase changes.

The maximum chapter length is a parameter (default 18 s; EVOLUTION's targets are ~24 s apart, so
tools/show_targets.json sets it), and starts are reported as exact tracked beat times. The chapters come
from tools/show_targets.json (each with a track index), any number of tracks are directed in show order,
and the result is cue_data.hpp in evo::show::generated plus music/direction.json.

A chapter's "target" is a time in show seconds. With a beat grid (tools/beat_grid.py) every chapter cut
lands exactly on a tracked beat within +/-2.5 s of it, preferring strong attacks, bar lines and 4-bar phrase
starts. Without one, the original attack-only direction is used. Each track's first chapter is pinned to
that track's start.

A track whose chapters carry a "form" instead is directed by its form (tools/song_form.py), so the chapters
follow the track's own phrases and drops whatever its length. Each "form" has
  "start"    where the chapter starts: an expression (below), or a number f in [0, 1], the fraction of the
             way from the previous chapter with an expression start to the next one (or to the track end);
  "accent"   (optional) the tracked beat the chapter's accent lands on, an expression; the default "middle"
             is the strongest rise or phrase start in the middle half of the chapter;
  "min_bars" (optional) the chapter's least length in bars.
An expression is an event of the form ("track start", "first drop", "biggest drop", "tail", "track end"),
or for an accent "start" (the chapter's own start) or "middle", followed by any number of signed terms such
as "+8 bars", "-3 beats" or "-1 s". Bars and beats step along the tracked beats; an expression with a
seconds term, or from the track end, goes to the bar line at or before it. The starts are chosen among the
form's cut points (phrase starts and section boundaries) within 16 bars of their targets, nearest first, by
dynamic programming within the chapter bounds; every accent must fall inside its chapter. Such a track's
bar lines are the form's (beat_data.hpp, beats.json), and each chapter's accent beat goes to
chapterAccents in cue_data.hpp (-1 for chapters directed by target, whose accent the show finds itself).

Usage: python3 tools/direct_music.py [--targets T] [--show S] [--header H] [--receipt R] <track1.wav> ...
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from analyze_music import analyze
import beat_grid
import song_form

ROOT = Path(__file__).resolve().parents[1]
WINDOW_SECONDS = 2.5
MIN_CHAPTER_SECONDS = 5
FORM_WINDOW_BARS = 16  # a form-directed start lies within this many bars of its target


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


def form_tracks(chapters):
    """The indices of the tracks directed by their form: those whose chapters carry a "form"."""
    tracks = {c['track'] for c in chapters if 'form' in c}
    for c in chapters:
        if c['track'] in tracks and 'form' not in c:
            raise ValueError(f'{c["title"]}: every chapter of a form-directed track needs a "form"')
    return tracks


# --- Form direction (tools/song_form.py) --------------------------------------------------------------------

UNITS = {'bar': 'bars', 'bars': 'bars', 'beat': 'beats', 'beats': 'beats', 's': 'seconds'}


def parse_expression(text):
    """'biggest drop -8 bars +1 beat' -> ('biggest drop', {'bars': -8, 'beats': 1, 'seconds': 0})."""
    tokens = str(text).split()
    split = next((i for i, t in enumerate(tokens) if t[0] in '+-'), len(tokens))
    base, rest = ' '.join(tokens[:split]), tokens[split:]
    terms = dict(bars=0., beats=0., seconds=0.)
    if not base or len(rest) % 2:
        raise ValueError(f'"{text}": expected an event and terms such as "+8 bars", "-1 beat" or "-1 s"')
    for amount, unit in zip(rest[::2], rest[1::2]):
        if unit not in UNITS:
            raise ValueError(f'"{text}": unknown unit "{unit}" (bars, beats or s)')
        terms[UNITS[unit]] += float(amount)
    if terms['bars'] != int(terms['bars']) or terms['beats'] != int(terms['beats']):
        raise ValueError(f'"{text}": bars and beats must be whole')
    return base, terms


class FormGrid:
    """The tracked beats of a form with its bar lines; times beyond the beats step by the median beat."""

    def __init__(self, form):
        self.times = list(form['times'])
        self.bar = form['bar_position']
        self.beat = float(np.median(np.diff(self.times))) if len(self.times) > 1 else .5
        self.phrases = set(form['phrase_starts'])
        self.kinds = {b['beat']: b for b in form['boundaries']}

    def index(self, time):
        """The beat at `time` (within 0.1 ms), or None."""
        found = int(np.searchsorted(self.times, time - 1e-4))
        return found if found < len(self.times) and abs(self.times[found] - time) <= 1e-4 else None

    def time(self, index):
        if index < 0:
            return self.times[0] + index * self.beat
        if index >= len(self.times):
            return self.times[-1] + (index - len(self.times) + 1) * self.beat
        return self.times[index]

    def bar_at_or_before(self, time):
        found = int(np.searchsorted(self.times, time + 1e-6, side='right')) - 1
        while found > 0 and self.bar[found] != 0:
            found -= 1
        return max(found, 0)

    def kind(self, index):
        parts = (['phrase start'] if round(self.times[index], 4) in self.phrases else []) + \
                ([self.kinds[index]['kind']] if index in self.kinds else [])
        return ' + '.join(parts)


def middle_accent(grid, start, end):
    """The strongest rise or phrase start in the middle half of [start, end), else the bar line nearest its
    middle: a beat index."""
    lines = [k for k, t in enumerate(grid.times) if grid.bar[k] == 0 and start < t < end]
    if not lines:
        raise ValueError(f'No bar line between {start:.2f} and {end:.2f} s')
    middle, length = .5 * (start + end), end - start

    def score(k):
        rise = grid.kinds[k]['contrast'] if k in grid.kinds and grid.kinds[k]['kind'] == 'rise' else 0.
        phrase = .1 if round(grid.times[k], 4) in grid.phrases else 0.
        return rise + phrase, -abs(grid.times[k] - middle)

    marked = [k for k in lines if abs(grid.times[k] - middle) <= .25 * length and score(k)[0] > 0]
    if marked:
        return max(marked, key=score)
    return min(lines, key=lambda k: abs(grid.times[k] - middle))


def resolve(text, form, grid, chapter=None):
    """An expression's time in track seconds and the tracked beat it lies on (None for a start target off
    the beats). chapter: (start, end) in track seconds, for an accent's "start" and "middle"."""
    base, terms = parse_expression(text)
    if base in ('start', 'middle'):
        if chapter is None:
            raise ValueError(f'"{text}": "{base}" only names a chapter\'s accent')
        index = grid.index(chapter[0]) if base == 'start' else middle_accent(grid, *chapter)
    elif base in form['events']:
        index = grid.index(form['events'][base])
    else:
        raise ValueError(f'"{text}": the form has no event "{base}" (it has: {", ".join(form["events"])})')
    steps = int(4 * terms['bars'] + terms['beats'])
    if index is not None:
        index += steps
        time = grid.time(index)
    else:
        time = form['events'][base] + steps * grid.beat
    if terms['seconds'] or index is None or not 0 <= index < len(grid.times):
        index = grid.bar_at_or_before(time + terms['seconds'])
        time = grid.time(index)
    return time, index


def direct_form(form, chapters, max_chapter):
    """Direct one track's chapters by its form: local starts and per-chapter reports, in track seconds."""
    grid = FormGrid(form)
    duration = form['duration']
    specs = [c['form'] for c in chapters]
    fraction = [isinstance(spec.get('start'), (int, float)) for spec in specs]
    if specs[0].get('start', 'track start') != 'track start':
        raise ValueError(f'{chapters[0]["title"]}: a track\'s first chapter starts at "track start"')
    # The targets: the expressions, then the fractions of the way between them.
    targets = [0.] + [None if fraction[j] else resolve(specs[j]['start'], form, grid)[0]
                      for j in range(1, len(chapters))]
    for j in range(1, len(chapters)):
        if fraction[j]:
            before = max(i for i in range(j) if not fraction[i])
            after = next((i for i in range(j + 1, len(chapters)) if not fraction[i]), None)
            end = duration if after is None else targets[after]
            targets[j] = targets[before] + float(specs[j]['start']) * (end - targets[before])
    # The accents an event names are known before the starts, which must keep each inside its chapter.
    named = [None if parse_expression(spec.get('accent', 'middle'))[0] in ('start', 'middle')
             else resolve(spec['accent'], form, grid)[0] for spec in specs]
    # The starts: cut points near the targets, chosen by dynamic programming within the chapter bounds.
    cuts = [grid.times[k] for k in form['cut_beats'] if grid.times[k] > 0]
    bar = 4 * grid.beat

    def keeps(j, time):
        return (named[j] is None or time < named[j] - 1e-6) and (named[j - 1] is None or time > named[j - 1] + 1e-6)

    def fits(j, start, end):
        least = max(MIN_CHAPTER_SECONDS, specs[j].get('min_bars', 0) * bar)
        return least - 1e-6 <= end - start <= max_chapter + 1e-6

    def cost(j, time):
        index = grid.index(time)
        return abs(time - targets[j]) / bar - (.5 if index in grid.kinds else 0.) - \
            (.25 if round(time, 4) in grid.phrases else 0.)

    layers = [{0.: (0., None)}]
    for j in range(1, len(chapters)):
        layer = {}
        for time in (c for c in cuts if abs(c - targets[j]) <= FORM_WINDOW_BARS * bar and keeps(j, c)):
            options = [(total + cost(j, time), previous) for previous, (total, _) in layers[-1].items()
                       if previous < time and fits(j - 1, previous, time)]
            if options:
                layer[time] = min(options)
        if not layer:
            raise ValueError(f'{chapters[j]["title"]}: no cut point within {FORM_WINDOW_BARS} bars of its target '
                             f'({targets[j]:.2f} s into the track) keeps the chapters between '
                             f'{MIN_CHAPTER_SECONDS} and {max_chapter:g} s; adjust its "form" or '
                             'max_chapter_seconds in tools/show_targets.json')
        layers.append(layer)
    finals = [(total, time) for time, (total, _) in layers[-1].items()
              if fits(len(chapters) - 1, time, duration)]
    if not finals:
        raise ValueError(f'{chapters[-1]["title"]}: no start leaves it between {MIN_CHAPTER_SECONDS} and '
                         f'{max_chapter:g} s before the track end; adjust its "form" in tools/show_targets.json')
    starts = [min(finals)[1]]
    for layer in reversed(layers[1:]):
        starts.append(layer[starts[-1]][1])
    starts.reverse()
    # The accents, on the chosen chapters.
    reports = []
    for j, (chapter, spec, start) in enumerate(zip(chapters, specs, starts)):
        end = starts[j + 1] if j + 1 < len(starts) else duration
        text = spec.get('accent', 'middle')
        accent, index = resolve(text, form, grid, (start, end))
        if not start < accent < end:
            raise ValueError(f'{chapter["title"]}: its accent "{text}" ({accent:.2f} s into the track) is outside '
                             f'the chapter ({start:.2f} to {end:.2f} s); adjust its "form" in '
                             'tools/show_targets.json')
        cut = 'track start' if j == 0 else grid.kind(grid.index(start))
        reports.append(dict(target=targets[j], start=start, accent=accent, cut=cut,
                            spec={k: spec[k] for k in ('start', 'accent', 'min_bars') if k in spec}))
    return starts, reports


def direct_show(paths, analyses, grids, chapters, max_chapter, forms=None):
    """Direct every track; returns show-second chapter starts and per-chapter / per-track reports.

    analyses: analyze_music.analyze() per track; grids: beat_grid.analyze() per track; forms:
    song_form.analyze() per track (computed where needed when not given). A form-directed track's grid must
    carry the form's bar positions already (prepare_show.py puts them there)."""
    tracks = [c['track'] for c in chapters]
    if tracks != sorted(tracks) or set(tracks) != set(range(len(paths))):
        raise ValueError('Chapters must be in show order and every track needs at least one chapter')
    by_form = form_tracks(chapters)
    starts, chapter_reports, track_reports, track_start = [], [], [], 0.
    for index, ((frames, data, spectrum), grid) in enumerate(zip(analyses, grids)):
        own = [c for c in chapters if c['track'] == index]
        if index in by_form:
            form = forms[index] if forms is not None else song_form.analyze((frames, data, spectrum), grid)
            local, form_reports = direct_form(form, own, max_chapter)
            hit = data[:, 2] / 255.
            # The attack within +/-50 ms (one 20 Hz frame) of a time, as direct() measures it.
            peak = lambda t: round(float(hit[max(0, round(t * 20) - 1):round(t * 20) + 2].max()), 3)
            downbeats = {round(t, 4) for t, b in zip(grid['times'], grid['bar_position']) if b == 0}
            report = [dict(attack=peak(r['start']), on_beat=r['cut'] != 'track start',
                           on_downbeat=round(r['start'], 4) in downbeats,
                           phrase='phrase start' in r['cut'], nominal_attack=peak(r['target']))
                      for r in form_reports]
            for chapter, time, entry, extra in zip(own, local, report, form_reports):
                start = round(track_start + time, 4)
                target = round(track_start + extra['target'], 4)
                starts.append(start)
                chapter_reports.append(dict(title=chapter['title'], track=index, target=target, start=start,
                                            shift=round(start - target, 4), **entry, form=extra['spec'],
                                            cut=extra['cut'], accent_beat=round(track_start + extra['accent'], 4)))
        else:
            # The first chapter of a track is pinned to the track start; the rest are directed in track time.
            local_targets = [0] + [c['target'] - track_start for c in own[1:]]
            downbeats = [t for t, b in zip(grid['times'], grid['bar_position']) if b == 0]
            phrases = downbeats[::4]
            local, report = direct(data, spectrum, local_targets, frames/48000, downbeats, phrases,
                                   grid['times'], max_chapter=max_chapter)
            for chapter, time, entry in zip(own, local, report):
                start = round(track_start + time, 4)
                starts.append(start)
                chapter_reports.append(dict(title=chapter['title'], track=index, target=chapter['target'],
                                            start=start, shift=round(start - chapter['target'], 4),
                                            **{k: entry[k] for k in ('attack', 'on_beat', 'on_downbeat',
                                                                     'phrase', 'nominal_attack')}))
        cuts = report[1:]  # the pinned first chapter is not a cut
        mean = lambda key: float(np.mean([c[key] for c in cuts])) if cuts else 0.
        track_reports.append(dict(track=index, source=Path(paths[index]).name, start_seconds=track_start,
                                  mean_attack=mean('attack'),
                                  on_beat=sum(c['on_beat'] for c in cuts),
                                  on_downbeat=sum(c['on_downbeat'] for c in cuts),
                                  phrase_starts=sum(c['phrase'] for c in cuts),
                                  nominal_mean_attack=mean('nominal_attack'),
                                  **({'directed_by': 'form'} if index in by_form else {})))
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
              '// The targets from tools/show_targets.json in show seconds: as written, or found in the form of a',
              '// form-directed track (tools/song_form.py).',
              'inline constexpr double chapterTargets[chapterCount] = {'
              + ','.join(f'{float(c["target"]):.4f}' for c in chapter_reports) + '};',
              '// The tracked beat each chapter\'s accent lands on, in show seconds, from the form of a form-directed',
              '// track; -1 where the show finds the accent itself (the strongest hit in the middle of the chapter).',
              'inline constexpr double chapterAccents[chapterCount] = {'
              + ','.join(f'{c["accent_beat"]:.4f}' if 'accent_beat' in c else '-1' for c in chapter_reports)
              + '};',
              '// SHA-256 of assets/music/show.wav, the audio this data was measured from.',
              f'inline constexpr const char* sourceSha256 = "{show_sha256}";',
              '}']
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text('\n'.join(header)+'\n', encoding='utf-8')
    method = ('Dynamic programming over tracked beats, scored by measured attacks and persistent spectral changes '
              f'with bar-line and 4-bar phrase bonuses; +/-{WINDOW_SECONDS} s; {MIN_CHAPTER_SECONDS}-{max_chapter:g} s '
              f'chapter bounds (max_chapter_seconds = {max_chapter:g} from tools/show_targets.json; '
              'the default is 18); '
              'each track\'s first chapter pinned to the track start; starts are exact tracked beat times')
    form_method = ('A form-directed track: targets from the "form" expressions over tools/song_form.py events, '
                   f'starts chosen among its phrase starts and boundaries within {FORM_WINDOW_BARS} bars of them '
                   'by dynamic programming (cost: distance in bars, -0.5 on a boundary, -0.25 on a phrase start) '
                   'within the same bounds; accents on the beats their expressions name')
    report = dict(method=method, form_method=form_method, window_seconds=WINDOW_SECONDS,
                  min_chapter_seconds=MIN_CHAPTER_SECONDS, max_chapter_seconds=max_chapter,
                  source_sha256=show_sha256, chapters=chapter_reports, tracks=track_reports)
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
    forms = [song_form.analyze(a, g) for a, g in zip(analyses, grids)]
    for index in form_tracks(chapters):
        grids[index]['bar_position'] = forms[index]['bar_position']
    starts, chapter_reports, track_reports = direct_show(args.tracks, analyses, grids, chapters, max_chapter,
                                                         forms)
    write_outputs(chapters, starts, chapter_reports, track_reports, max_chapter,
                  hashlib.sha256(args.show.read_bytes()).hexdigest(), args.header, args.receipt)
    print(json.dumps(track_reports))


if __name__ == '__main__':
    main()
