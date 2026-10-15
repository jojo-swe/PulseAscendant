"""Measure each act's beat grid (tempo, beat phase and downbeat) from the actual PCM.

These generated tracks drift slowly in tempo (tens of milliseconds over a
minute), so a single constant grid would slide off the music. Every beat is
tracked and then smoothed locally, which keeps cuts on the actual pulse.

Method: 100 Hz log spectral-flux onset envelope -> autocorrelation tempo
(70-180 BPM, log-normal prior around 120) -> dynamic-programming beat tracking
(Ellis 2007) -> local linear smoothing -> per-region downbeat phase chosen from
bass onsets and harmonic change at each candidate bar line.
"""
import hashlib
import json
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
RATE = 100
STEMS = ('luminous-flow-act1', 'adventurous-uplift', 'rigid-arpeggios')


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


def estimate_period(onset):
    """Autocorrelation tempo in frames, with octave-error protection via a tempo prior."""
    x = onset - onset.mean()
    corr = np.correlate(x, x, 'full')[len(x) - 1:]
    lags = np.arange(len(corr))
    valid = (lags >= RATE * 60 / 180) & (lags <= RATE * 60 / 70)
    bpm = 60 * RATE / np.maximum(lags, 1)
    prior = np.exp(-.5 * (np.log2(bpm / 120) / .9) ** 2)
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
    period = estimate_period(onset)
    tracked = track_beats(onset, period)
    beats = smooth_beats(tracked)
    position = bar_positions(onset, low, coarse, beats)
    jitter = np.abs(tracked - beats) * 1000 / RATE
    seconds = beats / RATE
    return dict(source=Path(path).name, sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(),
                frames=frames, bpm_median=round(60 * RATE / float(np.median(np.diff(beats))), 3),
                beats=int(len(beats)), downbeats=int((position == 0).sum()),
                tracker_jitter_ms_median=round(float(np.median(jitter)), 2),
                tracker_jitter_ms_p90=round(float(np.percentile(jitter, 90)), 2),
                times=[round(float(t), 4) for t in seconds], bar_position=[int(p) for p in position])


def main():
    reports = [analyze(ROOT / f'assets/music/{stem}.wav') for stem in STEMS]
    lines = ['// Generated by tools/beat_grid.py; measured facts in music/beats.json.', '#pragma once',
             '#include <array>', 'namespace remix::beat_data {',
             '// Beat times in act seconds; bar position 0 marks a downbeat.']
    for i, r in enumerate(reports):
        lines.append(f'inline constexpr std::array<double,{r["beats"]}> act{i+1}_times={{' + ','.join(f'{t:.4f}' for t in r['times']) + '};')
        lines.append(f'inline constexpr std::array<unsigned char,{r["beats"]}> act{i+1}_bar={{' + ','.join(map(str, r['bar_position'])) + '};')
    lines.append('}')
    (ROOT / 'src/beat_data.hpp').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    summary = [{k: v for k, v in r.items() if k not in ('times', 'bar_position')} for r in reports]
    (ROOT / 'music/beats.json').write_text(json.dumps(dict(
        method='100 Hz log spectral flux; autocorrelation + comb tempo with 120 BPM log-normal prior; '
               'DP beat tracking; +/-6 beat local linear smoothing (follows tempo drift); '
               '4/4 bar position from bass onset and harmonic change in +/-24 beat windows',
        meter=4, acts=summary), indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summary, indent=1))
    return reports


if __name__ == '__main__':
    main()
