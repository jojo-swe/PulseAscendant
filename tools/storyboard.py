"""Make pre/post chapter boards from native --tour-test captures.

These are directing aids, not a measure of artistic quality. Tour samples are
18% and 82% through each chapter, with captions excluded from motion metrics.
"""
import argparse
import json
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--captures', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    show = json.loads(subprocess.check_output([str(args.app.resolve()), '--show-info'], text=True))
    for i in range(len(show['chapters'])):
        for sample in ('a','b'):
            path=args.captures/f'chapter-{i+1}-{sample}.bmp'
            if not path.is_file():
                parser.error(f'Missing native capture: {path}')
    args.output.mkdir(parents=True, exist_ok=True)
    font = ImageFont.load_default(size=15)
    chapters = [dict(chapter=i+1, **c) for i,c in enumerate(show['chapters'])]
    (args.output/'chapters.json').write_text(json.dumps(chapters, indent=2)+'\n', encoding='utf-8')
    for scene in range(16):
        selected = [c for c in chapters if c['scene']==scene]
        board = Image.new('RGB', (768, len(selected)*245), '#10131c')
        draw = ImageDraw.Draw(board)
        for row,c in enumerate(selected):
            y = row*245
            draw.text((8,y+4), f'{c["chapter"]:02}  {c["title"]}  / visit {c["variant"]+1}  / {c["start"]:.2f}s', font=font, fill='white')
            for column,sample in enumerate(('a','b')):
                with Image.open(args.captures/f'chapter-{c["chapter"]}-{sample}.bmp') as im:
                    board.paste(im.convert('RGB').resize((384,216)), (column*384,y+27))
        board.save(args.output/f'world-{scene:02}.jpg', quality=92)
    print(f'16 world boards, {len(chapters)} chapters; pre/post samples, not continuous viewing')


if __name__ == '__main__':
    main()
