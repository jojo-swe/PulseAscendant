"""The step 5 byte-identity baseline: render a fixed set of outputs and print their SHA-256 sums.

The set covers the path-traced Studio (every chapter, a two-dispatch spp 5 frame, the STUDIO NIGHT accent
cut), the placeholder gradient, 1993 effects, a wipe, two chapters forced into 2026 (EVO_FORCE_ERA=1), and
four --render-stream runs (across the accent cut, FINALE's beat swaps, STUDIO NIGHT into ASCENT, and DOTS).
Every run passes --validation, so a validation message fails the run. It also prints the mean display
luminance of each Studio capture (Rec. 709 weights on the 8-bit sRGB bytes / 255, the measure of
tests/scene_fixture.cpp's checkChapterPictures).

The committed SUMS and LUMINANCE files next to this script were made on lavapipe (Mesa llvmpipe). History:
first at 4f300f6 (main after Tasks 1, 5 and 7), which Task 1b's own commits kept byte for byte; then
regenerated after merging main d91fb09 (Tasks 2, 3 and 4), whose environments and light selection changed
seven outputs (the lit Studio captures and streams; the merged build equals d91fb09's bytes exactly); then
regenerated after the NEE shadow-ray fix (150714d), which changed seven outputs again: studio_night_40,
finale_50, studio_night_acc, return_30 and the streams stream_studio_cut, stream_finale, stream_studio_ascent.
Then regenerated after merging step 5 Task 14 (Studio polish, a422931): the Studio's light rig, cameras and
concrete changed seven outputs again (studio_night_40, finale_50, studio_night_acc, return_30 and the streams
stream_studio_cut, stream_finale, stream_studio_ascent); reboot_60, portal_50, reboot_10 and every non-Studio
output stayed byte for byte.
Then, merging step 5 Task 9, its export shutter changed one more: studio_night_acc (spp 5 is two dispatches, so two time slices of the
180 degree shutter at the default 60 fps; mean luminance +0.13 %). Every capture at spp 4 or fewer and every
stream (spp 1-2) has a single slice and stayed byte for byte the same.
Then regenerated after merging step 5 Task 6 (the integrator v2: path regularisation, the clamp after a rough
vertex, the Studio's camera-only LEDs), which changed the same seven Studio outputs (mean luminance within
+-0.1 %); the caustic photon maps (Task 13) and the light tree (Task 12) changed nothing, and no non-Studio
output moved.
Then regenerated after merging the Particles and Spheres scenes: only stream_dots and raytrace_2026 changed.
Then regenerated for the City scene (step 5 Task 23): ASCENT now renders the city, so stream_studio_ascent
changed; every other output, the Studio's included, stayed byte for byte.
Then regenerated for the Nebula scene (PLASMA's 2026 half and NEBULA): only nebula_40 and plasma_wipe changed.
Then regenerated for the FINALE megamix's director: after D the room carries the 1993 card, so finale_50 and
stream_finale changed (mean luminance -0.13 %); no other output moved.
They are lavapipe-only: another driver or GPU gives other bytes.

How later tasks use it (run from evolution/ after building the linux preset):

    python3 tests/baseline/hash_outputs.py build/linux/bin/Evolution build/linux/build-out/baseline \\
        --check tests/baseline/SUMS --luminance tests/baseline/LUMINANCE

- A task whose output must stay byte-exact (Task 11 (j) "a scene without a medium", Task 13 (e) "a scene
  without receivers", and every pure ABI/refactor change) passes --check: every sum must match.
- A task that changes the picture on purpose (Task 2's environments, Task 6's integrator, Task 14's Studio
  polish, ...) checks the luminance instead: --luminance prints each Studio capture's change against the
  committed value (Task 2 (e): within 5 %), and the task commits a new SUMS/LUMINANCE in the same commit,
  saying so in its commit message, so the next change compares against the new reference.

Usage: hash_outputs.py <Evolution> <outdir> [--check SUMS] [--luminance LUMINANCE] [--write]
  --write   also writes <outdir>/SUMS and <outdir>/LUMINANCE (copy them here to update the reference).
Exit code 1 when --check finds a difference."""

import argparse
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import zlib

# name, show seconds, spp, width, height, EVO_FORCE_ERA (None: as the show has it), Studio?
CAPTURES = [
    ("studio_night_40", 202.68, 2, 160, 90, None, True),  # Task 1 Step 0
    ("reboot_60", 424.14576, 2, 160, 90, None, True),  # Task 1 Step 0
    ("finale_50", 176.4, 2, 160, 90, None, True),
    ("portal_50", 188.26, 2, 160, 90, None, True),
    ("studio_night_acc", 212.0, 5, 160, 90, None, True),  # after the accent cut, two dispatches
    ("return_30", 397.17, 2, 160, 90, None, True),
    ("reboot_10", 414.4, 1, 96, 54, None, True),
    ("boot_50", 3.5, 2, 160, 90, None, False),
    ("plasma_wipe", 55.7, 2, 160, 90, None, False),
    ("nebula_40", 274.5, 2, 160, 90, None, False),
    ("galaxy_20", 437.3, 2, 160, 90, None, False),
    ("flyby_2026", 15.0, 2, 160, 90, "1", False),
    ("raytrace_2026", 120.0, 2, 160, 90, "1", False),
]
# name, start seconds, duration, fps, spp (all at 96x54)
STREAMS = [
    ("stream_studio_cut", 209.3, 1, 10, 1),
    ("stream_finale", 178.0, 1, 8, 2),
    ("stream_studio_ascent", 213.0, 5, 2, 2),
    ("stream_dots", 40.0, 2, 4, 1),
]


def run(command, force_era):
    env = dict(os.environ)
    env.pop("EVO_FORCE_ERA", None)
    if force_era is not None:
        env["EVO_FORCE_ERA"] = force_era
    result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        sys.stderr.write(result.stderr.decode(errors="replace"))
        sys.exit(f"{' '.join(command)} exited with {result.returncode}")
    return result.stdout


def mean_luminance(path):
    """Mean display luminance of an 8-bit RGB PNG with filter type 0 (what Evolution writes)."""
    with open(path, "rb") as f:
        data = f.read()
    pos, width, height, idat = 8, 0, 0, []
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height = struct.unpack(">II", body[:8])
        elif kind == b"IDAT":
            idat.append(body)
        pos += 12 + length
    raw = zlib.decompress(b"".join(idat))
    stride, total = 1 + width * 3, 0.0
    for row in range(height):
        line = raw[row * stride + 1:(row + 1) * stride]
        r, g, b = sum(line[0::3]), sum(line[1::3]), sum(line[2::3])
        total += 0.2126 * r + 0.7152 * g + 0.0722 * b
    return total / (255.0 * width * height)


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def read_table(path):
    table = {}
    with open(path) as f:
        for line in f:
            if line.strip() and not line.startswith("#"):
                value, name = line.split()
                table[name] = value
    return table


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("exe")
    parser.add_argument("outdir")
    parser.add_argument("--check")
    parser.add_argument("--luminance")
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()

    shutil.rmtree(args.outdir, ignore_errors=True)
    os.makedirs(args.outdir)
    sums, luminance = {}, {}
    for name, seconds, spp, width, height, force_era, studio in CAPTURES:
        directory = os.path.join(args.outdir, name)
        os.makedirs(directory)
        out = run([args.exe, "--capture-dir", directory, "--time", repr(seconds), "--width", str(width),
                   "--height", str(height), "--spp", str(spp), "--validation"], force_era)
        path = out.decode().strip().splitlines()[-1]
        sums[name] = sha256(path)
        if studio:
            luminance[name] = mean_luminance(path)
    for name, seconds, duration, fps, spp in STREAMS:
        path = os.path.join(args.outdir, name + ".rgb")
        with open(path, "wb") as f:
            f.write(run([args.exe, "--render-stream", "--time", repr(seconds), "--duration", str(duration),
                         "--fps", str(fps), "--width", "96", "--height", "54", "--spp", str(spp),
                         "--validation"], None))
        sums[name] = sha256(path)

    sums_text = "".join(f"{value}  {name}\n" for name, value in sums.items())
    luminance_text = "".join(f"{value:.6f}  {name}\n" for name, value in luminance.items())
    sys.stdout.write(sums_text)
    sys.stdout.write("# mean display luminance (Studio)\n" + luminance_text)
    if args.write:
        with open(os.path.join(args.outdir, "SUMS"), "w") as f:
            f.write(sums_text)
        with open(os.path.join(args.outdir, "LUMINANCE"), "w") as f:
            f.write(luminance_text)

    failed = False
    if args.luminance:
        reference = read_table(args.luminance)
        for name, value in luminance.items():
            base = float(reference[name])
            print(f"luminance {name}: {value:.6f} vs {base:.6f} ({100.0 * (value - base) / base:+.2f} %)")
    if args.check:
        reference = read_table(args.check)
        for name in sorted(set(reference) | set(sums)):
            if reference.get(name) != sums.get(name):
                print(f"DIFFERS: {name}")
                failed = True
        print("byte-identical to " + args.check if not failed else "NOT byte-identical to " + args.check)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
