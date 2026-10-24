"""Run Evolution with --capture-dir: it must create the nested directory, write frame-001.500.png at
96x54, print the path, and succeed again when the file already exists. "--time -0" must name its file
frame-000.000.png, not frame--00.000.png. EVO_FORCE_ERA=1 must change the frame at 1.5 s (BOOT, a 1993
chapter) and a value other than 0 or 1 must be ignored. Arguments after <tmpdir> (e.g. --validation) are passed
to every run."""

import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WIDTH, HEIGHT = 96, 54


def capture(exe, extra, capture_dir, time="1.5", force_era=None):
    env = dict(os.environ)
    env.pop("EVO_FORCE_ERA", None)
    if force_era is not None:
        env["EVO_FORCE_ERA"] = force_era
    result = subprocess.run(
        [exe, *extra, "--capture-dir", capture_dir, "--time", time, "--width", str(WIDTH), "--height",
         str(HEIGHT)],
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


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit("usage: capture_test.py <Evolution executable> <tmpdir> [Evolution options...]")
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
