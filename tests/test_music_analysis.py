"""Frequency isolation: a bass tone must not animate the treble geometry."""
import sys
import unittest
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from analyze_music import spectrum_envelopes, BAND_EDGES


class SpectrumTests(unittest.TestCase):
    def test_silence(self):
        bands = spectrum_envelopes(np.zeros(48000, dtype=np.float32), 48000)
        self.assertEqual(bands.shape, (20, 24))
        self.assertTrue(np.isfinite(bands).all())
        self.assertEqual(float(bands.max()), 0)

    def test_frequency_isolation(self):
        clock = np.arange(96000) / 48000
        for frequency in (100, 1000, 10000):
            with self.subTest(frequency=frequency):
                bands = spectrum_envelopes(.4 * np.sin(2 * np.pi * frequency * clock), 48000)
                # Exclude the real onset/ending transient and let the release envelope settle.
                levels = bands[10:-10].mean(axis=0)
                expected = np.searchsorted(BAND_EDGES, frequency, side='right') - 1
                self.assertEqual(int(levels.argmax()), expected)
                self.assertGreater(levels[expected], .8)
                distant = np.abs(np.arange(24) - expected) > 1
                self.assertLess(float(levels[distant].max()), .02)


if __name__ == '__main__':
    unittest.main()
