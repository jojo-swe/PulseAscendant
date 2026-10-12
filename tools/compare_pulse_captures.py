"""Compare exact-time native captures with transient gain1 versus0.

This proves that the transient channels affect pixels. It is not an artistic
score or a music-off comparison: energy, spectrum and directed motion remain.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--show', required=True, type=Path)
    parser.add_argument('--on', required=True, type=Path)
    parser.add_argument('--off', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    show = json.loads(args.show.read_text())
    comparisons = []
    board = Image.new('RGB', (1280, 8*207), '#10131c')
    draw = ImageDraw.Draw(board)
    for scene in range(16):
        rows = []
        for i, chapter in enumerate(show['chapters']):
            if chapter['scene'] != scene:
                continue
            for sample in ('a', 'b'):
                filename = f'chapter-{i+1}-{sample}.bmp'
                on = np.asarray(Image.open(args.on / filename).convert('RGB'), dtype=np.float32)
                off = np.asarray(Image.open(args.off / filename).convert('RGB'), dtype=np.float32)
                difference = np.abs(on-off)
                rows.append(dict(chapter=i+1, sample=sample, title=chapter['title'], file=filename,
                                 mean_channel_difference=float(difference.mean()/255),
                                 changed_pixel_fraction=float((difference.max(axis=2)>8).mean())))
        best = max(rows, key=lambda row: row['mean_channel_difference'])
        comparisons.append(dict(scene=scene, samples=rows, strongest_sample=best))
        x, y = (scene%2)*640, (scene//2)*207
        for column, path in enumerate((args.off, args.on)):
            board.paste(Image.open(path / best['file']).resize((320, 180)), (x+column*320, y))
        draw.text((x+5, y+183), f"{scene}: {best['title']} | gain0 / gain1 | changed {best['changed_pixel_fraction']:.1%}", fill='white')
    args.output.mkdir(parents=True, exist_ok=True)
    board.save(args.output / 'pulse-comparison.jpg', quality=94)
    report = dict(method='Same native times, pulse gain0 vs1; other musical direction remains active', worlds=comparisons)
    (args.output / 'pulse-comparison.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps([dict(scene=c['scene'], **c['strongest_sample']) for c in comparisons], indent=2))


if __name__ == '__main__':
    main()
