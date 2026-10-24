"""Synthetic tests for beat tracking: a drifting click track and its downbeats, and the tempo
disambiguation against dotted-eighth delays (without over-correcting a plain slow track).

Ported from PulseAscendant tests/test_beat_grid.py (MIT)."""
import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import beat_grid  # noqa: E402


def clicks(times, accents, seconds, rate=48000):
    rng = np.random.default_rng(1)
    signal = rng.normal(0, .002, int(seconds * rate)).astype(np.float32)
    burst = np.arange(int(.03 * rate))
    for t, accent in zip(times, accents):
        start = int(t * rate)
        tone = np.sin(2 * np.pi * (60 if accent else 1800) * burst / rate) * np.exp(-burst / (.008 * rate))
        signal[start:start + len(burst)] += (.9 if accent else .4) * tone[:len(signal) - start]
    return signal


def add_bursts(signal, times, frequency, gain, decay, rate=48000):
    """Add a decaying sine burst at each time (seconds)."""
    burst = np.arange(int(6 * decay * rate))
    tone = np.sin(2 * np.pi * frequency * burst / rate) * np.exp(-burst / (decay * rate))
    for t in times:
        start = int(round(t * rate))
        if start < len(signal):
            signal[start:start + len(burst)] += gain * tone[:len(signal) - start]
    return signal


def add_hats(signal, times, gain, decay=.02, rate=48000):
    """Add a differentiated (bright) noise burst at each time (seconds): a broadband hi-hat."""
    n = np.arange(int(6 * decay * rate))
    burst = np.diff(np.random.default_rng(5).normal(0, 1, len(n)) * np.exp(-n / (decay * rate)), prepend=0)
    for t in times:
        start = int(round(t * rate))
        if start < len(signal):
            signal[start:start + len(n)] += gain * burst[:len(signal) - start]
    return signal


def tracked_tempo(signal):
    """Run the tempo estimate, disambiguation, tracker and smoothing as analyze() does."""
    onset, low, _ = beat_grid.onset_envelope(signal)
    comb = beat_grid.estimate_period(onset)
    period, candidates, method = beat_grid.disambiguate_period(onset, low, comb)
    beats = beat_grid.smooth_beats(beat_grid.track_beats(onset, period)) / 100
    return 60 / float(np.median(np.diff(beats))), beats, 6000 / comb, method, candidates


def on_beat_fraction(beats, truth, tolerance=.025):
    error = np.abs(beats[:, None] - truth[None, :]).min(axis=1)
    return float(np.mean(error <= tolerance))


class BeatGridTests(unittest.TestCase):
    def test_tracks_drifting_tempo_and_downbeats(self):
        # 124 BPM drifting to 126 BPM over 60 s, kick on beat one of every bar.
        times, t = [], .5
        while t < 59:
            times.append(t)
            t += 60 / (124 + 2 * t / 60)
        accents = [i % 4 == 0 for i in range(len(times))]
        onset, low, coarse = beat_grid.onset_envelope(clicks(times, accents, 60))
        period = beat_grid.estimate_period(onset)
        self.assertAlmostEqual(6000 / period, 125, delta=2.5)
        beats = beat_grid.smooth_beats(beat_grid.track_beats(onset, period)) / 100
        truth = np.array(times)
        inner = beats[(beats > 3) & (beats < 56)]
        error = np.abs(inner[:, None] - truth[None, :]).min(axis=1)
        self.assertLess(float(np.median(error)), .015)
        position = beat_grid.bar_positions(onset, low, coarse, beats * 100)
        downbeats = beats[position == 0]
        accented = truth[::4]
        hits = [np.abs(accented - d).min() < .03 for d in downbeats[(downbeats > 5) & (downbeats < 54)]]
        self.assertGreaterEqual(np.mean(hits), .9)
        # The disambiguation leaves the drifting click track at its tempo.
        self.assertAlmostEqual(6000 / beat_grid.disambiguate_period(onset, low, period)[0], 125, delta=2.5)

    def test_dotted_eighth_delay_does_not_halve_the_tempo(self):
        # 140 BPM trance figure: strong kick on every beat, a bass pluck on the off-beat eighths and a hi-hat
        # through a dotted-eighth (3/16) delay: a hit every dotted quarter plus its echo 3/16 later. The
        # broadband hats outweigh the 60 Hz kick in the full-band flux and repeat every 1.5 beats, so the
        # comb settles on 93 BPM (2/3 tempo); the bass onsets on each candidate grid must restore 140.
        # (A hat on every beat with its echo repeats every beat and does not fool the comb.)
        beat, seconds = 60 / 140, 60
        truth = np.arange(.25, seconds - .5, beat)
        dotted_quarters = np.arange(.25, seconds - .5, 1.5 * beat)
        signal = np.random.default_rng(2).normal(0, .002, seconds * 48000).astype(np.float32)
        add_bursts(signal, truth, 60, .9, .03)
        add_bursts(signal, truth + beat / 2, 110, .35, .02)
        add_hats(signal, dotted_quarters, .3)
        add_hats(signal, dotted_quarters + .75 * beat, .2)
        bpm, beats, comb_bpm, method, candidates = tracked_tempo(signal)
        detail = f'comb {comb_bpm:.2f} BPM, {method}, candidates {candidates}'
        self.assertEqual(method, 'bass-grid', detail)
        self.assertAlmostEqual(bpm, 140, delta=1.5, msg=detail)
        self.assertGreaterEqual(on_beat_fraction(beats, truth), .9)

    def test_plain_slow_track_keeps_its_tempo(self):
        # 92 BPM kick on every beat and nothing 1.5 beats apart: the disambiguation must not move it to 138.
        truth = np.arange(.25, 59.5, 60 / 92)
        signal = np.random.default_rng(3).normal(0, .002, 60 * 48000).astype(np.float32)
        add_bursts(signal, truth, 60, .9, .03)
        bpm, beats, comb_bpm, method, candidates = tracked_tempo(signal)
        detail = f'comb {comb_bpm:.2f} BPM, {method}, candidates {candidates}'
        self.assertEqual(method, 'comb', detail)
        self.assertAlmostEqual(bpm, 92, delta=1, msg=detail)
        self.assertGreaterEqual(on_beat_fraction(beats, truth), .9)


if __name__ == '__main__':
    unittest.main()
