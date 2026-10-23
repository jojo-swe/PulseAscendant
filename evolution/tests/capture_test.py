"""Run Evolution with --capture-dir: it must create the nested directory, write frame-001.500.png at
96x54, print the path, and succeed again when the file already exists."""

import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WIDTH, HEIGHT = 96, 54


def capture(exe, capture_dir):
    result = subprocess.run(
        [exe, "--capture-dir", capture_dir, "--time", "1.5", "--width", str(WIDTH), "--height", str(HEIGHT)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    sys.stderr.write(result.stderr.decode(errors="replace"))
    if result.returncode != 0:
        sys.exit(f"--capture-dir exited with {result.returncode}")
    return result.stdout.decode().strip()


def main(exe, tmpdir):
    shutil.rmtree(tmpdir, ignore_errors=True)  # the nested directory must not exist beforehand
    capture_dir = os.path.join(tmpdir, "a", "b")
    expected = os.path.join(capture_dir, "frame-001.500.png")

    printed = capture(exe, capture_dir)
    if not os.path.isfile(expected):
        sys.exit(f"{expected} was not written")
    if os.path.normpath(printed) != os.path.normpath(expected):
        sys.exit(f"expected the written path on stdout, got '{printed}'")

    decoded = subprocess.run([sys.executable, os.path.join(HERE, "check_png.py"), expected],
                             stdout=subprocess.PIPE, timeout=60)
    summary = decoded.stdout.decode().strip()
    if decoded.returncode != 0 or not summary.startswith(f"{WIDTH}x{HEIGHT} "):
        sys.exit(f"check_png.py: expected {WIDTH}x{HEIGHT}, got '{summary}'")
    print(f"capture: {expected} decodes as {summary}")

    capture(exe, capture_dir)  # captures are diagnostics: an existing file is overwritten
    print("capture: second run overwrote the file")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: capture_test.py <Evolution executable> <tmpdir>")
    main(sys.argv[1], sys.argv[2])
