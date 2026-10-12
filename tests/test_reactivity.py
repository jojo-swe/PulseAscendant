"""Meaningful timing and frequency-isolation checks for musical transients."""
import importlib.util
from pathlib import Path
import unittest
import numpy as np

spec = importlib.util.spec_from_file_location('reactivity', Path(__file__).resolve().parents[1] / 'tools/analyze_reactivity.py')
reactivity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reactivity)


class ReactivityTests(unittest.TestCase):
    def test_silence_does_not_generate_hits(self):
        values, peaks = reactivity.extract(np.zeros(48000))
        self.assertFalse(values.any())
        self.assertEqual(peaks, [[], [], []])

    def test_band_attacks_keep_timing_and_decay(self):
        for channel, frequency in enumerate((80, 1000, 9000)):
            with self.subTest(frequency=frequency):
                time = np.arange(96000) / 48000
                onset = .5
                age = np.maximum(time-onset, 0)
                # A20ms smooth attack keeps this narrow-band; an instantaneous
                # gated sine genuinely contains a broadband click.
                burst = np.where(time >= onset, np.sin(np.minimum(age/.02, 1)*np.pi/2)**2 * np.exp(-age/.04), 0)
                values, peaks = reactivity.extract(.7 * np.sin(2*np.pi*frequency*time) * burst)
                self.assertTrue(peaks[channel])
                strongest = int(np.argmax(values[:, channel]))
                self.assertLessEqual(abs(strongest / reactivity.RATE-onset), .03)
                self.assertGreater(values[strongest, channel], 180)
                self.assertLess(values[strongest+50, channel], 20)
                self.assertLess(int(values[:45, :3].max()), 10)
                # A pure tone must not generate substantial distant-band hits.
                other = 2 if channel == 0 else 0
                self.assertLess(int(values[:, other].max()), 100)


if __name__ == '__main__':
    unittest.main()
