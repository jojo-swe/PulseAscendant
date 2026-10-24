"""Capture a storyboard of the show: for every chapter, frames at 20 %, at the accent and at 80 % of the
chapter, once forced into 1993 and once into 2026 (the app's diagnostic EVO_FORCE_ERA), composed into one PNG
grid per act.

    python3 tools/storyboard.py --app build/linux/bin/Evolution --out build/linux/build-out/storyboard
    python3 tools/storyboard.py --app EXE --out DIR --width 320 --height 180 --chapters 0,3 --no-crt

Each act's grid (act1.png, act2.png, act3.png; an act with no chosen chapter is skipped) has a row per
chapter and six columns: 1993 at 20 %, accent and 80 %, then 2026 at the same three times. Every cell is the
capture box-filtered down 2x (the HUD stays on, so the epoch card shows in the cells too; the chapter title is window-only).
Layout, in grid pixels: a BAND-high text band on top names the columns; each row is a BAND-high label band
(chapter number, title, transition, start-end and accent in show seconds), then the cells, then a GUTTER;
cells sit GUTTER apart with a GUTTER before the first and after the last. See grid_size(). Prints the path of
every grid it writes. Needs numpy; the PNGs are read and written here with zlib.
"""

import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib

import numpy as np

GUTTER = 2  # dark grey between cells, in grid pixels
BAND = 11   # a text band: the 7-pixel font with 2 pixels above and below
SAMPLES = (("20%", lambda c: c["start"] + 0.2 * c["duration"]),
           ("ACCENT", lambda c: c["accent"]),
           ("80%", lambda c: c["start"] + 0.8 * c["duration"]))
ERAS = ((0, "1993"), (1, "2026"))

GUTTER_RGB = (0x30, 0x30, 0x30)
LABEL_RGB = (0xE0, 0xE0, 0xE0)
ERA_RGB = {0: (0xFF, 0xC0, 0x40), 1: (0x60, 0xD0, 0xFF)}  # the column headers: amber 1993, cyan 2026

# A 5x7 font for the labels: seven rows per glyph, '#' set.
FONT = {
    "A": ".###. #...# #...# ##### #...# #...# #...#", "B": "####. #...# #...# ####. #...# #...# ####.",
    "C": ".###. #...# #.... #.... #.... #...# .###.", "D": "####. #...# #...# #...# #...# #...# ####.",
    "E": "##### #.... #.... ####. #.... #.... #####", "F": "##### #.... #.... ####. #.... #.... #....",
    "G": ".###. #...# #.... #.### #...# #...# .####", "H": "#...# #...# #...# ##### #...# #...# #...#",
    "I": ".###. ..#.. ..#.. ..#.. ..#.. ..#.. .###.", "J": "..### ...#. ...#. ...#. ...#. #..#. .##..",
    "K": "#...# #..#. #.#.. ##... #.#.. #..#. #...#", "L": "#.... #.... #.... #.... #.... #.... #####",
    "M": "#...# ##.## #.#.# #.#.# #...# #...# #...#", "N": "#...# #...# ##..# #.#.# #..## #...# #...#",
    "O": ".###. #...# #...# #...# #...# #...# .###.", "P": "####. #...# #...# ####. #.... #.... #....",
    "Q": ".###. #...# #...# #...# #.#.# #..#. .##.#", "R": "####. #...# #...# ####. #.#.. #..#. #...#",
    "S": ".#### #.... #.... .###. ....# ....# ####.", "T": "##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..",
    "U": "#...# #...# #...# #...# #...# #...# .###.", "V": "#...# #...# #...# #...# #...# .#.#. ..#..",
    "W": "#...# #...# #...# #.#.# #.#.# #.#.# .#.#.", "X": "#...# #...# .#.#. ..#.. .#.#. #...# #...#",
    "Y": "#...# #...# .#.#. ..#.. ..#.. ..#.. ..#..", "Z": "##### ....# ...#. ..#.. .#... #.... #####",
    "0": ".###. #...# #..## #.#.# ##..# #...# .###.", "1": "..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###.",
    "2": ".###. #...# ....# ...#. ..#.. .#... #####", "3": "##### ...#. ..#.. ...#. ....# #...# .###.",
    "4": "...#. ..##. .#.#. #..#. ##### ...#. ...#.", "5": "##### #.... ####. ....# ....# #...# .###.",
    "6": "..##. .#... #.... ####. #...# #...# .###.", "7": "##### ....# ...#. ..#.. .#... .#... .#...",
    "8": ".###. #...# #...# .###. #...# #...# .###.", "9": ".###. #...# #...# .#### ....# ...#. .##..",
    " ": "..... ..... ..... ..... ..... ..... .....", ".": "..... ..... ..... ..... ..... .##.. .##..",
    "-": "..... ..... ..... ##### ..... ..... .....", "+": "..... ..#.. ..#.. ##### ..#.. ..#.. .....",
    "%": "##... ##..# ...#. ..#.. .#... #..## ...##", ":": "..... .##.. .##.. ..... .##.. .##.. .....",
    "/": "..... ....# ...#. ..#.. .#... #.... .....", "?": ".###. #...# ....# ...#. ..#.. ..... ..#..",
}


def read_png(path):
    """An 8-bit RGB PNG with filter type 0 on every scanline (what Evolution writes) as an (h, w, 3) array."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, width, height, idat = 8, None, None, []
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or colour != 2 or interlace != 0:
                raise ValueError(f"{path}: expected 8-bit RGB without interlace")
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + length
    if width is None:
        raise ValueError(f"{path}: no IHDR")
    raw = np.frombuffer(zlib.decompress(b"".join(idat)), dtype=np.uint8)
    rows = raw.reshape(height, 1 + width * 3)
    if rows[:, 0].any():
        raise ValueError(f"{path}: only filter type 0 is supported")
    return rows[:, 1:].reshape(height, width, 3).copy()


def write_png(path, image):
    """Writes an (h, w, 3) uint8 array as an 8-bit RGB PNG, filter type 0 on every scanline."""
    height, width, _ = image.shape
    rows = np.zeros((height, 1 + width * 3), dtype=np.uint8)
    rows[:, 1:] = image.reshape(height, width * 3)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(rows.tobytes(), 6)))
        f.write(chunk(b"IEND", b""))


def downscale2(image):
    """Halves an image with a 2x2 box filter (rounded); an odd last row or column is dropped."""
    h, w = image.shape[0] // 2, image.shape[1] // 2
    blocks = image[:2 * h, :2 * w].astype(np.uint16).reshape(h, 2, w, 2, 3)
    return ((blocks.sum(axis=(1, 3)) + 2) // 4).astype(np.uint8)


def draw_text(image, x, y, text, rgb):
    """Draws text in the 5x7 font with its top-left at (x, y), 6 pixels per character, clipped to the
    image; characters the font lacks draw as '?'."""
    for ch in text.upper():
        rows = FONT.get(ch, FONT["?"]).split()
        for dy, row in enumerate(rows):
            for dx, bit in enumerate(row):
                px, py = x + dx, y + dy
                if bit == "#" and 0 <= px < image.shape[1] and 0 <= py < image.shape[0]:
                    image[py, px] = rgb
        x += 6


def grid_size(rows, cell_w, cell_h):
    """(width, height) of a grid of `rows` chapters with cells of cell_w x cell_h."""
    columns = len(ERAS) * len(SAMPLES)
    return columns * cell_w + (columns + 1) * GUTTER, BAND + rows * (BAND + cell_h + GUTTER)


def row_label(chapter):
    end = chapter["start"] + chapter["duration"]
    return (f"{chapter['index']:02d} {chapter['title']}  {chapter['transition']}  "
            f"{chapter['start']:.2f}-{end:.2f} S  ACCENT {chapter['accent']:.2f} S")


def compose(chapters, cells, cell_w, cell_h):
    """The grid for these chapters; cells[(index, era, sample)] is a cell_h x cell_w x 3 array."""
    width, height = grid_size(len(chapters), cell_w, cell_h)
    grid = np.empty((height, width, 3), dtype=np.uint8)
    grid[:, :] = GUTTER_RGB
    column = 0
    for era, era_name in ERAS:
        for sample_name, _ in SAMPLES:
            draw_text(grid, GUTTER + column * (cell_w + GUTTER) + 1, 2, f"{era_name} {sample_name}",
                      ERA_RGB[era])
            column += 1
    for row, chapter in enumerate(chapters):
        top = BAND + row * (BAND + cell_h + GUTTER)
        draw_text(grid, GUTTER + 1, top + 2, row_label(chapter), LABEL_RGB)
        column = 0
        for era, _ in ERAS:
            for sample in range(len(SAMPLES)):
                x = GUTTER + column * (cell_w + GUTTER)
                grid[top + BAND:top + BAND + cell_h, x:x + cell_w] = cells[(chapter["index"], era, sample)]
                column += 1
    return grid


def show_info(app):
    result = subprocess.run([app, "--show-info"], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=60)
    if result.returncode != 0:
        sys.exit(f"{app} --show-info exited with {result.returncode}: "
                 f"{result.stderr.decode(errors='replace').strip()}")
    return json.loads(result.stdout)


def capture(app, directory, seconds, era, args):
    """One frame from the app at `seconds` forced into `era`; returns the decoded capture."""
    env = dict(os.environ, EVO_FORCE_ERA=str(era))
    command = [app, "--capture-dir", directory, "--time", f"{seconds:.4f}", "--width", str(args.width),
               "--height", str(args.height)]
    if args.no_crt:
        command.append("--no-crt")
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, timeout=300)
    if result.returncode != 0:
        sys.exit(f"capture at {seconds:.4f} s (EVO_FORCE_ERA={era}) exited with {result.returncode}:\n"
                 f"{result.stderr.decode(errors='replace')}")
    path = result.stdout.decode().strip().splitlines()[-1]
    image = read_png(path)
    os.remove(path)
    return image


def parse_chapters(text, count):
    if text is None:
        return list(range(count))
    try:
        chosen = sorted({int(part) for part in text.split(",") if part.strip()})
    except ValueError:
        sys.exit(f"--chapters: expected comma-separated chapter numbers, got '{text}'")
    bad = [index for index in chosen if not 0 <= index < count]
    if bad or not chosen:
        sys.exit(f"--chapters: chapters are 0..{count - 1}, got '{text}'")
    return chosen


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--app", required=True, help="the Evolution executable")
    parser.add_argument("--out", required=True, help="directory for act1.png, act2.png and act3.png")
    parser.add_argument("--width", type=int, default=640, help="capture width (cells are half)")
    parser.add_argument("--height", type=int, default=360, help="capture height (cells are half)")
    parser.add_argument("--chapters", help="comma-separated chapter numbers (default: all)")
    parser.add_argument("--no-crt", action="store_true", help="capture the 1993 picture without the CRT look")
    args = parser.parse_args(argv)

    info = show_info(args.app)
    chapters = info["chapters"]
    chosen = [chapters[index] for index in parse_chapters(args.chapters, len(chapters))]

    cells = {}
    with tempfile.TemporaryDirectory(prefix="storyboard-") as directory:
        for chapter in chosen:
            for era, _ in ERAS:
                for sample, (_, when) in enumerate(SAMPLES):
                    image = capture(args.app, directory, when(chapter), era, args)
                    cells[(chapter["index"], era, sample)] = downscale2(image)
            print(f"chapter {chapter['index']:02d} {chapter['title']}: captured", file=sys.stderr, flush=True)

    cell_h, cell_w = next(iter(cells.values())).shape[:2]
    for act in range(len(info["tracks"])):
        rows = [chapter for chapter in chosen if chapter["track"] == act]
        if not rows:
            continue
        path = os.path.join(args.out, f"act{act + 1}.png")
        write_png(path, compose(rows, cells, cell_w, cell_h))
        print(path, flush=True)


if __name__ == "__main__":
    main()
