"""A song join must be continuous, bounded and retain both songs' sample clocks."""
import sys
from pathlib import Path
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_show import blend_boundary


def main():
    rate = 48000
    outgoing = np.full((rate*2, 2), .75, np.float32)
    incoming = np.full_like(outgoing, -.65)
    mixed = blend_boundary(outgoing, incoming, rate)
    assert mixed.shape == (rate*4, 2)
    assert np.max(np.abs(mixed)) <= 1
    assert np.max(np.abs(np.diff(mixed, axis=0))) < .001
    assert np.allclose(mixed[0], outgoing[0], atol=1e-6)
    assert np.allclose(mixed[-1], incoming[-1], atol=1e-6)
    # Retained outgoing tail overlaps the incoming song at its original clock.
    silence = blend_boundary(outgoing, np.zeros_like(incoming), rate)
    assert np.sqrt(np.mean(silence[rate*2:rate*2+rate//10]**2)) > .005
    assert np.max(np.abs(silence[-rate//4:])) == 0
    assert np.all(blend_boundary(np.zeros_like(outgoing), np.zeros_like(incoming), rate) == 0)
    print('AUDIO BRIDGE PASS: continuous, bounded, no length change, decaying tail, silent input stays silent')


if __name__ == '__main__':
    main()
