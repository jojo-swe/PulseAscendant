"""Measure each track's beat grid (tempo, beat phase and downbeat) from the actual PCM.

Ported from PulseAscendant tools/beat_grid.py (MIT). The tracker, smoothing and bar-position code and their
constants are unchanged; the I/O differs (any number of tracks on the command line, output in the
evo::show::generated namespace) and the tempo estimate gains a disambiguation step.

Songs drift slowly in tempo (tens of milliseconds over a minute), so a single constant grid would slide
off the music. Every beat is tracked and then smoothed locally, which keeps cuts on the actual pulse.

Method: 100 Hz log spectral-flux onset envelope -> autocorrelation tempo
(70-180 BPM, log-normal prior around 120) -> disambiguation against 2/3, 3/2, 1/2, 2x and the strongest
autocorrelation peaks by the bass onsets on each candidate's grid -> dynamic-programming beat tracking
(Ellis 2007) -> local linear smoothing -> per-region downbeat phase chosen from
bass onsets and harmonic change at each candidate bar line.

Usage: python3 tools/beat_grid.py [--header H] [--receipt R] <track1.wav> [<track2.wav> ...]
"""
import argparse
import hashlib
import json
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
RATE = 100
METHOD = ('100 Hz log spectral flux; autocorrelation + comb tempo with 120 BPM log-normal prior; '
          'tempo disambiguated by phase-optimised bass onset mean on 90-180 BPM candidate grids (switch at '
          '>= 5 % better); DP beat tracking; +/-6 beat local linear smoothing (follows tempo drift); '
          '4/4 bar position from bass onset and harmonic change in +/-24 beat windows')


def read_mono(path):
    with wave.open(str(path), 'rb') as audio:
        if (audio.getframerate(), audio.getnchannels(), audio.getsampwidth()) != (48000, 2, 2):
            raise ValueError('Expected stereo 48 kHz PCM16')
        pcm = np.frombuffer(audio.readframes(audio.getnframes()), '<i2').reshape(-1, 2)
    return pcm.astype(np.float32).mean(axis=1) / 32768, len(pcm)


def onset_envelope(mono, sample_rate=48000):
    """Return full-band and bass onset strength plus a coarse spectrum per 10 ms frame."""
    hop, size = sample_rate // RATE, 2048
    window = np.hanning(size)
    freqs = np.fft.rfftfreq(size, 1 / sample_rate)
    bass = (freqs >= 35) & (freqs < 160)
    edges = np.geomspace(60, 8000, 13)
    bands = [(freqs >= lo) & (freqs < hi) for lo, hi in zip(edges[:-1], edges[1:])]
    padded = np.pad(mono, (size // 2, size))
    count = (len(mono) + hop - 1) // hop
    flux, low, coarse = np.zeros(count), np.zeros(count), np.zeros((count, len(bands)))
    previous = None
    for i, start in enumerate(range(0, len(mono), hop)):
        mag = np.log1p(1000 * np.abs(np.fft.rfft(padded[start:start + size] * window)))
        if previous is not None:
            rise = np.maximum(mag - previous, 0)
            flux[i], low[i] = rise.mean(), rise[bass].mean()
        coarse[i] = [mag[b].mean() for b in bands]
        previous = mag

    def normalize(x):
        x = x - np.convolve(x, np.ones(31) / 31, 'same')
        x = np.maximum(x, 0)
        return x / max(1e-9, float(np.percentile(x, 99)))
    return normalize(flux), normalize(low), coarse


# Tempo range a disambiguated beat may take, the range autocorrelation peaks are drawn from, and the score
# margin a candidate needs over the comb estimate before it replaces it.
CANDIDATE_BPM = (90, 180)
PEAK_BPM = (60, 200)
SWITCH_MARGIN = 1.05


def autocorrelation(onset):
    x = onset - onset.mean()
    return np.correlate(x, x, 'full')[len(x) - 1:]


def tempo_prior(bpm):
    """Log-normal prior around 120 BPM (0.9 octave width)."""
    return np.exp(-.5 * (np.log2(bpm / 120) / .9) ** 2)


def estimate_period(onset):
    """Autocorrelation tempo in frames, with octave-error protection via a tempo prior."""
    corr = autocorrelation(onset)
    lags = np.arange(len(corr))
    valid = (lags >= RATE * 60 / 180) & (lags <= RATE * 60 / 70)
    prior = tempo_prior(60 * RATE / np.maximum(lags, 1))
    score = np.where(valid, corr * prior, -np.inf)
    # Comb-sum over multiples sharpens the true beat period.
    comb = np.full(len(corr), -np.inf)
    for lag in np.flatnonzero(valid):
        multiples = [m for m in (1, 2, 3, 4) if lag * m < len(corr)]
        comb[lag] = sum(corr[lag * m] for m in multiples) / len(multiples) * prior[lag]
    best = int(np.argmax(comb if np.isfinite(comb).any() else score))
    # Parabolic interpolation for sub-frame precision.
    if 0 < best < len(comb) - 1 and np.isfinite(comb[best - 1]) and np.isfinite(comb[best + 1]):
        a, b, c = comb[best - 1], comb[best], comb[best + 1]
        denom = a - 2 * b + c
        if denom != 0:
            return best + .5 * (a - c) / denom
    return float(best)


def autocorrelation_peaks(corr, count=6):
    """Lags (frames) of the strongest local maxima of the autocorrelation between PEAK_BPM."""
    lags = np.arange(int(np.ceil(60 * RATE / PEAK_BPM[1])), int(60 * RATE / PEAK_BPM[0]) + 1)
    lags = lags[(lags >= 1) & (lags < len(corr) - 1)]
    peaks = lags[(corr[lags] > corr[lags - 1]) & (corr[lags] >= corr[lags + 1])]
    return peaks[np.argsort(-corr[peaks], kind='stable')][:count]


def bass_grid(low, period, start, phases=32, span=1., step=.01):
    """Best mean bass onset on a constant beat grid from start to the end: (mean, period).

    The grid phase is optimised over `phases` steps of one period. Over a whole song a constant grid only
    stays on the beat if its period is right to about 1e-4, far finer than an autocorrelation lag, so the
    period is refined too, within +/-span frames in `step` frame steps."""
    n = len(low)
    best = (-1., period)
    for p in np.arange(period - span, period + span + step / 2, step):
        positions = start + np.arange(phases)[:, None] * p / phases + np.arange(int((n - start) / p) + 1) * p
        index = np.round(positions).astype(int)
        inside = index < n
        sums = np.where(inside, low[np.minimum(index, n - 1)], 0).sum(axis=1)
        means = sums / np.maximum(inside.sum(axis=1), 1)
        if means.max() > best[0]:
            best = (float(means.max()), float(p))
    return best


def disambiguate_period(onset, low, period):
    """Correct the comb estimate when it locked onto a related period instead of the beat.

    Trance delays at 3/16 put strong onsets 1.5 beats apart, so the comb can settle on dotted quarters
    (2/3 tempo) or other sub-harmonics. Each candidate (the comb period P, P/1.5, P*1.5, P/2, 2P and the six
    strongest autocorrelation peaks; all but P limited to CANDIDATE_BPM) is scored by its bass-onset grid
    mean times the tempo prior. The best replaces P only if it scores SWITCH_MARGIN better and is a
    different tempo (more than 3 % away). Returns (period, {bpm: score}, method)."""
    start = int(np.argmax(onset >= .3))  # first beat estimate: the first clear onset
    candidates = [period, period / 1.5, period * 1.5, period / 2, period * 2]
    candidates += [float(lag) for lag in autocorrelation_peaks(autocorrelation(onset))]
    seen, scored = [], []
    for k, candidate in enumerate(candidates):
        bpm = 60 * RATE / candidate
        if k and not CANDIDATE_BPM[0] <= bpm <= CANDIDATE_BPM[1]:
            continue
        if any(abs(candidate - other) < .5 for other in seen):
            continue  # an autocorrelation peak at a period already scored
        seen.append(candidate)
        mean, refined = bass_grid(low, candidate, start)
        scored.append((mean * float(tempo_prior(60 * RATE / refined)), refined))
    report = {f'{60 * RATE / refined:.2f}': round(score, 4)
              for score, refined in sorted(scored, key=lambda s: s[1])}
    score, refined = max(scored)
    # Only the best candidate is considered. If it is P's own tempo (within 3 %), P stays even when a distinct
    # second-best candidate beats P by the margin: that is deliberate, the best evidence already backs P.
    if score > 0 and score >= SWITCH_MARGIN * scored[0][0] and abs(np.log(refined / period)) > np.log(1.03):
        return refined, report, 'bass-grid'
    return period, report, 'comb'


def track_beats(onset, period, tightness=120.):
    """Dynamic-programming beat tracker: maximize onset strength with a tempo-consistency penalty."""
    n = len(onset)
    score = onset.copy()
    back = np.full(n, -1)
    lo, hi = int(round(period * .5)), int(round(period * 2))
    for i in range(hi, n):
        prev = np.arange(i - hi, i - lo + 1)
        penalty = -tightness * np.log((i - prev) / period) ** 2
        candidates = score[prev] + penalty
        j = int(np.argmax(candidates))
        score[i] = onset[i] + candidates[j]
        back[i] = prev[j]
    # Start from the strongest score in the final beat period.
    i = int(n - hi + np.argmax(score[n - hi:]))
    beats = []
    while i >= 0:
        beats.append(i)
        i = back[i]
    return np.array(beats[::-1], dtype=float)


def smooth_beats(beats, radius=6):
    """Local linear refit over +/-radius beats removes tracker jitter but follows slow tempo drift."""
    out = beats.copy()
    idx = np.arange(len(beats))
    for k in range(len(beats)):
        lo, hi = max(0, k - radius), min(len(beats), k + radius + 1)
        slope, offset = np.polyfit(idx[lo:hi], beats[lo:hi], 1)
        out[k] = slope * k + offset
    assert np.all(np.diff(out) > 0)
    return out


def bar_positions(onset, low, coarse, beats, meter=4, radius=24):
    """Per-beat position within a 4/4 bar (0 = downbeat).

    Each beat's phase is chosen from a +/-radius-beat window: bass weight plus
    harmonic change across the candidate bar line. Windows overlap heavily, so
    the phase only changes where the evidence really moves (e.g. after a
    tracker-inserted beat in a sparse intro)."""
    frames = np.clip(np.round(beats).astype(int), 0, len(onset) - 1)
    period = float(np.median(np.diff(beats)))
    span = max(2, int(period * .6))
    change = np.zeros(len(frames))
    for k, f in enumerate(frames):
        before, after = coarse[max(0, f - span):f], coarse[f:f + span]
        if len(before) and len(after):
            change[k] = np.abs(after.mean(axis=0) - before.mean(axis=0)).mean()
    change /= max(1e-9, float(np.percentile(change, 95)))
    strength = 1.2 * low[frames] + .4 * onset[frames] + change
    phases = np.zeros(len(frames), dtype=int)
    for k in range(len(frames)):
        lo, hi = max(0, k - radius), min(len(frames), k + radius + 1)
        window = np.arange(lo, hi)
        scores = [strength[window[(window - m) % meter == 0]].mean() for m in range(meter)]
        phases[k] = int(np.argmax(scores))
    # Majority filter: a bar line must persist, not flicker beat to beat.
    smoothed = phases.copy()
    for k in range(len(phases)):
        lo, hi = max(0, k - 8), min(len(phases), k + 9)
        smoothed[k] = np.bincount(phases[lo:hi], minlength=meter).argmax()
    return (np.arange(len(frames)) - smoothed) % meter


def analyze(path):
    mono, frames = read_mono(path)
    onset, low, coarse = onset_envelope(mono)
    comb = estimate_period(onset)
    period, candidates, tempo_method = disambiguate_period(onset, low, comb)
    tracked = track_beats(onset, period)
    beats = smooth_beats(tracked)
    position = bar_positions(onset, low, coarse, beats)
    jitter = np.abs(tracked - beats) * 1000 / RATE
    seconds = beats / RATE
    return dict(source=Path(path).name, sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(),
                frames=frames, bpm_median=round(60 * RATE / float(np.median(np.diff(beats))), 3),
                beats=int(len(beats)), downbeats=int((position == 0).sum()),
                tempo_comb_bpm=round(60 * RATE / comb, 3), tempo_method=tempo_method,
                tempo_candidates=candidates,
                tracker_jitter_ms_median=round(float(np.median(jitter)), 2),
                tracker_jitter_ms_p90=round(float(np.percentile(jitter, 90)), 2),
                times=[round(float(t), 4) for t in seconds], bar_position=[int(p) for p in position])


def write_outputs(reports, header_path, receipt_path):
    """Write beat_data.hpp (all tracks, concatenated, with per-track offsets) and the beats.json receipt."""
    if any(r['beats'] == 0 for r in reports):
        raise ValueError('A track has no tracked beats')
    offsets = [0]
    for r in reports:
        offsets.append(offsets[-1] + r['beats'])
    times = [t for r in reports for t in r['times']]
    bars = [b for r in reports for b in r['bar_position']]
    lines = ['// Generated by tools/beat_grid.py; measured facts in music/beats.json.', '#pragma once',
             '#include <cstdint>', 'namespace evo::show::generated {',
             '// Beat times in track seconds; beatOffsets[i] is track i\'s first beat. Bar position 0 marks a '
             'downbeat.',
             f'inline constexpr int beatOffsets[{len(offsets)}] = {{' + ','.join(map(str, offsets)) + '};',
             'inline constexpr double beatTimes[] = {' + ','.join(f'{t:.4f}' for t in times) + '};',
             'inline constexpr std::uint8_t beatBar[] = {' + ','.join(map(str, bars)) + '};',
             '}']
    header_path.parent.mkdir(parents=True, exist_ok=True)
    header_path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    summary = [{k: v for k, v in r.items() if k not in ('times', 'bar_position')} for r in reports]
    intervals = np.concatenate([np.diff(r['times']) for r in reports])
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_text(json.dumps(dict(
        method=METHOD, meter=4, bpm_median=round(60 / float(np.median(intervals)), 3), tracks=summary),
        indent=2) + '\n', encoding='utf-8')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('tracks', nargs='+', type=Path, help='stereo 48 kHz PCM16 WAV files, in show order')
    parser.add_argument('--header', type=Path, default=ROOT / 'src/show/generated/beat_data.hpp')
    parser.add_argument('--receipt', type=Path, default=ROOT / 'music/beats.json')
    args = parser.parse_args()
    reports = [analyze(path) for path in args.tracks]
    print(json.dumps(write_outputs(reports, args.header, args.receipt), indent=1))


if __name__ == '__main__':
    main()
