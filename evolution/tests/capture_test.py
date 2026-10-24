"""Run Evolution with --capture-dir: it must create the nested directory, write frame-001.500.png at
96x54, print the path, and succeed again when the file already exists. "--time -0" must name its file
frame-000.000.png, not frame--00.000.png. EVO_FORCE_ERA=1 must change the frame at 1.5 s (BOOT, a 1993
chapter) and a value other than 0 or 1 must be ignored. "--spp 0" must be refused with exit code 1, and in a
path-traced chapter (a "scene" other than the placeholder in --show-info: the Studio) --spp must reach the
picture: --spp 1 and --spp 2 differ, and the same --spp twice gives the same bytes. Arguments after <tmpdir>
(e.g. --validation --spp 2) are passed to every run, before the test's own."""

import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WIDTH, HEIGHT = 96, 54


def capture(exe, extra, capture_dir, time="1.5", force_era=None, more=()):
    env = dict(os.environ)
    env.pop("EVO_FORCE_ERA", None)
    if force_era is not None:
        env["EVO_FORCE_ERA"] = force_era
    result = subprocess.run(
        [exe, *extra, "--capture-dir", capture_dir, "--time", time, "--width", str(WIDTH), "--height",
         str(HEIGHT), *more],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120, env=env)
    sys.stderr.write(result.stderr.decode(errors="replace"))
    if result.returncode != 0:
        sys.exit(f"--capture-dir exited with {result.returncode}")
    return result.stdout.decode().strip()


def main(exe, tmpdir, extra):
    shutil.rmtree(tmpdir, ignore_errors=True)  # the nested directory must not exist beforehand
    capture_dir = os.path.join(tmpdir, "a", "b")
    expected = os.path.join(capture_dir, "frame-001.500.png")

    printed = capture(exe, extra, capture_dir)
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

    capture(exe, extra, capture_dir)  # captures are diagnostics: an existing file is overwritten
    print("capture: second run overwrote the file")

    printed = capture(exe, extra, capture_dir, time="-0")
    if os.path.basename(printed) != "frame-000.000.png":
        sys.exit(f"--time -0: expected frame-000.000.png, got '{printed}'")
    print("capture: --time -0 wrote frame-000.000.png")

    # EVO_FORCE_ERA (diagnostic): 1 shows the 2026 picture in a 1993 chapter; any other value but 0 is ignored.
    with open(expected, "rb") as f:
        natural = f.read()
    for value, same in (("0", True), ("1", False), ("2", True), ("", True), ("1.0", True)):
        with open(capture(exe, extra, capture_dir, force_era=value), "rb") as f:
            forced = f.read()
        if (forced == natural) != same:
            sys.exit(f"EVO_FORCE_ERA='{value}': expected the frame to {'match' if same else 'differ from'} "
                     "the unforced one")
    print("capture: EVO_FORCE_ERA=1 forces 2026, 0 and invalid values leave BOOT in 1993")

    # --spp: 1..4096; 0 is bad usage, refused before any GPU work.
    refused = subprocess.run([exe, *extra, "--capture-dir", capture_dir, "--spp", "0"], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, timeout=60)
    if refused.returncode != 1 or b"--spp must be between 1 and 4096" not in refused.stderr:
        sys.stderr.write(refused.stderr.decode(errors="replace"))
        sys.exit(f"--spp 0: expected exit code 1 and the range on stderr, got {refused.returncode}")
    print("capture: --spp 0 exits 1")

    # In a path-traced chapter the samples per pixel change the picture, and nothing else does.
    info = json.loads(subprocess.run([exe, "--show-info"], stdout=subprocess.PIPE, timeout=60).stdout)
    traced = [c for c in info["chapters"] if c["scene"] != "Placeholder"]
    if not traced:
        print("capture: no chapter is path traced yet (every scene is the placeholder); --spp not checked "
              "on a picture")
        return
    chapter = traced[0]
    t = f"{chapter['start'] + 0.4 * chapter['duration']:.4f}"

    def read(path):
        with open(path, "rb") as f:
            return f.read()

    one = read(capture(exe, extra, capture_dir, time=t, more=("--spp", "1")))
    two = read(capture(exe, extra, capture_dir, time=t, more=("--spp", "2")))
    again = read(capture(exe, extra, capture_dir, time=t, more=("--spp", "2")))
    if one == two:
        sys.exit(f"{chapter['title']} at {t} s: --spp 1 and --spp 2 give the same picture")
    if two != again:
        sys.exit(f"{chapter['title']} at {t} s: two captures at --spp 2 differ")
    print(f"capture: {chapter['title']} ({chapter['scene']}) at {t} s: --spp 1 != --spp 2, --spp 2 repeats")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit("usage: capture_test.py <Evolution executable> <tmpdir> [Evolution options...]")
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
