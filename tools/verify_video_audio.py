"""Compare decoded video audio to the runtime WAV at windows across all three acts.

This checks alignment/content, not subjective listening quality. Requires ffmpeg
and numpy. The offsets reported are relative to the reference audio clock.
"""
import argparse
import json
import subprocess
from pathlib import Path

import numpy as np


def decode(path, start, duration):
    data = subprocess.check_output([
        'ffmpeg', '-v', 'error', '-nostdin', '-ss', str(start), '-i', str(path),
        '-t', str(duration), '-vn', '-ac', '1', '-ar', '8000', '-f', 'f32le', 'pipe:1'])
    return np.frombuffer(data, dtype='<f4').astype(np.float64)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--video', type=Path, required=True)
    parser.add_argument('--wav', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    windows = [2, 42, 110, 220, 298, 300, 342, 410, 500, 582, 588, 590,
               620, 710, 815, 846, 880, 886]
    observations = []
    for start in windows:
        reference = decode(args.wav, start, 2)
        actual = decode(args.video, start, 2)
        # Exclude resampling/codec edge transients, then search +/-25 ms.
        margin, radius = 400, 200
        size = min(len(reference), len(actual))
        if size < 8000:
            raise RuntimeError(f'Insufficient decoded audio at {start}s')
        center = reference[margin:size-margin]
        center = center-center.mean()
        candidates = actual[margin-radius:size-margin+radius]
        dots = np.correlate(candidates, center, mode='valid')
        sums = np.r_[0., np.cumsum(candidates)]
        squares = np.r_[0., np.cumsum(candidates*candidates)]
        n = len(center)
        norms = np.sqrt(np.maximum(0., squares[n:]-squares[:-n]-(sums[n:]-sums[:-n])**2/n))
        scores = dots / np.maximum(1e-15, np.sqrt(np.sum(center*center))*norms)
        best = int(np.argmax(scores))
        observation = dict(start=start, duration=2, correlation=float(scores[best]),
                           offset_ms=(best-radius)/8, rms=float(np.sqrt(np.mean(center**2))))
        observation['pass'] = observation['correlation'] >= .98 and abs(observation['offset_ms']) <= 2
        observations.append(observation)
    result = dict(video=str(args.video), wav=str(args.wav), sample_rate=8000,
                  scope='Decoded mono correlation at 18 two-second windows; not full subjective listening',
                  observations=observations, passed=all(o['pass'] for o in observations))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f"AUDIO {'PASS' if result['passed'] else 'FAIL'}: {sum(o['pass'] for o in observations)}/{len(observations)} windows; "
          f"minimum correlation {min(o['correlation'] for o in observations):.6f}; "
          f"maximum absolute offset {max(abs(o['offset_ms']) for o in observations):.3f} ms")
    if not result['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
