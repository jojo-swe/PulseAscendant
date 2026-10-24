"""Run tools/storyboard.py on chapters 0 and 3 at 320x180: it must write act1.png (both chapters are in Act I)
and no other act, the grid must decode to the documented layout's size, and the forced 1993 and 2026 cells of
each row must differ, which shows that EVO_FORCE_ERA reached the captures. It empties <outdir> first, so CTest
gives it build-out/storyboard_test, never the build-out/storyboard that README and HANDOFF have people fill."""

import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(os.path.dirname(HERE), "tools")
sys.path.insert(0, TOOLS)

import storyboard  # noqa: E402  (the tool's PNG reader and layout constants)

WIDTH, HEIGHT = 320, 180
CHAPTERS = [0, 3]


def main(exe, outdir):
    shutil.rmtree(outdir, ignore_errors=True)
    result = subprocess.run(
        [sys.executable, os.path.join(TOOLS, "storyboard.py"), "--app", exe, "--out", outdir, "--width",
         str(WIDTH), "--height", str(HEIGHT), "--chapters", ",".join(map(str, CHAPTERS))],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=280)
    sys.stderr.write(result.stderr.decode(errors="replace"))
    if result.returncode != 0:
        sys.exit(f"storyboard.py exited with {result.returncode}")

    grid_path = os.path.join(outdir, "act1.png")
    printed = [os.path.normpath(line) for line in result.stdout.decode().split()]
    if printed != [os.path.normpath(grid_path)]:
        sys.exit(f"expected only {grid_path} on stdout, got {printed}")
    for other in ("act2.png", "act3.png"):
        if os.path.exists(os.path.join(outdir, other)):
            sys.exit(f"{other} was written although no chosen chapter is in that act")

    # The grid is also a valid PNG to the independent decoder the other tests use.
    decoded = subprocess.run([sys.executable, os.path.join(HERE, "check_png.py"), grid_path],
                             stdout=subprocess.PIPE, timeout=60)
    summary = decoded.stdout.decode().strip()

    # The documented layout, spelled out: cells are half the capture size, six per row with a gutter before,
    # between and after them; a text band heads the grid and each row, and a gutter closes each row.
    cell_w, cell_h = WIDTH // 2, HEIGHT // 2
    gutter, band = 2, 11
    expected_w = 6 * cell_w + 7 * gutter
    expected_h = band + len(CHAPTERS) * (band + cell_h + gutter)
    if storyboard.grid_size(len(CHAPTERS), cell_w, cell_h) != (expected_w, expected_h):
        sys.exit("storyboard.grid_size disagrees with the documented layout")
    if decoded.returncode != 0 or not summary.startswith(f"{expected_w}x{expected_h} "):
        sys.exit(f"check_png.py: expected {expected_w}x{expected_h}, got '{summary}'")
    print(f"storyboard: {grid_path} decodes as {summary}")

    grid = storyboard.read_png(grid_path)
    for row, chapter in enumerate(CHAPTERS):
        y = band + row * (band + cell_h + gutter) + band
        for sample in range(3):
            x_then = gutter + sample * (cell_w + gutter)
            x_now = gutter + (sample + 3) * (cell_w + gutter)
            then = grid[y:y + cell_h, x_then:x_then + cell_w].astype(int)
            now = grid[y:y + cell_h, x_now:x_now + cell_w].astype(int)
            changed = (abs(then - now).sum(axis=2) > 24).mean()
            if changed < 0.25:
                sys.exit(f"chapter {chapter}, sample {sample}: the 1993 and 2026 cells differ in only "
                         f"{changed:.0%} of their pixels; EVO_FORCE_ERA did not take")
    print("storyboard: every row's 1993 and 2026 cells differ")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: storyboard_test.py <Evolution executable> <outdir>")
    main(sys.argv[1], sys.argv[2])
