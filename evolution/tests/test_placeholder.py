"""The synthetic placeholder song: exact length, format, headroom, section loudness and determinism."""
import hashlib
import subprocess
import sys
import tempfile
import unittest
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
BAR_SECONDS = 60 / 134 * 4


def generate(path):
    result = subprocess.run([sys.executable, str(ROOT / 'tools/make_placeholder.py'), str(path)],
                            check=True, capture_output=True, text=True)
    return result.stdout


def bar_rms(samples, first, last):
    """RMS over bars first..last inclusive."""
    start, end = round(first * BAR_SECONDS * 48000), round((last + 1) * BAR_SECONDS * 48000)
    block = samples[start:end].astype(np.float64)
    return float(np.sqrt(np.mean(block * block)))


def db(ratio):
    return 20 * np.log10(ratio)


class PlaceholderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        directory = Path(cls.temp.name)
        cls.first, cls.second = directory / 'first.wav', directory / 'second.wav'
        cls.output = generate(cls.first)
        generate(cls.second)
        with wave.open(str(cls.first), 'rb') as audio:
            cls.params = (audio.getnchannels(), audio.getframerate(), audio.getsampwidth(), audio.getnframes())
            cls.samples = np.frombuffer(audio.readframes(audio.getnframes()), '<i2').reshape(-1, 2)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_format_and_length(self):
        self.assertEqual(self.params, (2, 48000, 2, 12_960_000))
        self.assertEqual(len(self.samples), 12_960_000)

    def test_peak_is_normalised_to_minus_one_dbfs(self):
        peak = int(np.abs(self.samples.astype(np.int32)).max()) / 32768
        self.assertLessEqual(db(peak), -1 + .1)
        self.assertGreater(db(peak), -1.5)

    def test_drop_is_louder_than_intro(self):
        self.assertGreaterEqual(db(bar_rms(self.samples, 88, 119) / bar_rms(self.samples, 0, 7)), 6)

    def test_breakdown_is_quieter_than_the_section_before(self):
        self.assertLess(bar_rms(self.samples, 80, 87), bar_rms(self.samples, 56, 79))

    def test_is_deterministic_and_reports_its_digest(self):
        first = hashlib.sha256(self.first.read_bytes()).hexdigest()
        self.assertEqual(first, hashlib.sha256(self.second.read_bytes()).hexdigest())
        self.assertIn(first, self.output)
        self.assertIn('12960000', self.output)


if __name__ == '__main__':
    unittest.main()
