"""Synthetic tests for beat tracking: a drifting click track and its downbeats.

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


if __name__ == '__main__':
    unittest.main()
