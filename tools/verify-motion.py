"""Check exact-time tour captures for nonempty images and substantial movement, excluding captions."""
import json
import struct
import sys
from pathlib import Path
import numpy as np


def read_bmp(path):
    data = path.read_bytes()
    offset, = struct.unpack_from('<I', data, 10)
    width, height = struct.unpack_from('<ii', data, 18)
    bits, = struct.unpack_from('<H', data, 28)
    if data[:2] != b'BM' or bits not in (24, 32):
        raise ValueError(f'Unsupported BMP: {path}')
    pixel_size = bits // 8
    stride = (width * pixel_size + 3) // 4 * 4
    rows = np.frombuffer(data, np.uint8, offset=offset, count=stride*abs(height)).reshape(abs(height), stride)
    image = rows[:, :width*pixel_size].reshape(abs(height), width, pixel_size)[:, :, :3]
    if height > 0:
        image = image[::-1]
    return image[int(abs(height)*.15):int(abs(height)*.8)].astype(np.float32) / 255


root = Path(sys.argv[1])
report = []
for chapter in range(1, 32):
    a, b = (read_bmp(root/f'chapter-{chapter}-{sample}.bmp') for sample in ('a', 'b'))
    difference = np.abs(a-b).mean(axis=2)
    motion = float(difference.mean())
    coverage = float((difference > .05).mean())
    brightness = float(a.mean())
    if not (motion > .012 and coverage > .10 and .01 < brightness < .95):
        raise RuntimeError(f'Chapter {chapter}: motion={motion:.4f}, changed={coverage:.3f}, brightness={brightness:.3f}')
    report.append(dict(chapter=chapter, mean_change=round(motion,4), changed_fraction=round(coverage,4), mean_brightness=round(brightness,4)))
result = json.dumps(dict(passed=len(report), chapters=report), indent=2) + '\n'
if len(sys.argv) > 2:
    Path(sys.argv[2]).write_text(result, encoding='utf-8')
    print(f'{len(report)}/31 chapters passed; report: {sys.argv[2]}')
else:
    print(result, end='')
