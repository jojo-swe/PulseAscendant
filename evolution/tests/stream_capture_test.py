"""Seeking and export see the same show (Review Focus 4): for one time T in every chapter, frame 5 of a
--render-stream that starts 5 frames before T must equal a fresh --capture-dir at T, byte for byte, at
160x90. The stream renders frames T - 5/fps .. T in one process, so its retro stage has rendered (and cached)
the retro frames before T; the capture renders T from nothing. Any effect state carried over from earlier
frames shows up as a difference.

Every era a chapter shows (the "eras" of --show-info) is compared on its own, with both runs forced into it
by EVO_FORCE_ERA, so the 1993 side of every chapter that has one is checked, whatever the transition shows
at T (a BeatSwap may be on 2026 there). A forced capture must differ from one forced into the other era,
which shows the forcing took; the stream must then match it. A chapter fixed to 2026 runs unforced, once.

The stream runs at 64 fps, faster than every retro rate (35 and 10 Hz), so consecutive frames also hit the
retro cache inside one retro frame; T is the multiple of 1/64 s nearest the chapter's middle, so
T - 5/64 + 5/64 is exactly T in double precision. Arguments after <tmpdir> (e.g. --validation) are passed to
every run."""

import json
import os
import shutil
import struct
import subprocess
import sys
import zlib

WIDTH, HEIGHT = 160, 90
FPS = 64
LEAD = 5  # frames before T
FRACTION = 0.5  # where in a chapter T lies
ERA_VALUE = {1993: "0", 2026: "1"}  # EVO_FORCE_ERA


def run(command, env=None):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120, env=env)
    if result.returncode != 0:
        sys.stderr.write(result.stderr.decode(errors="replace"))
        sys.exit(f"{' '.join(command)} exited with {result.returncode}")
    return result.stdout


def read_png_rgb(path):
    """The raw RGB bytes of an 8-bit RGB PNG with filter type 0 on every scanline (what Evolution writes)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, width, height, idat = 8, None, None, []
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or colour != 2 or interlace != 0:
                sys.exit(f"{path}: expected 8-bit RGB without interlace")
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + length
    if (width, height) != (WIDTH, HEIGHT):
        sys.exit(f"{path}: expected {WIDTH}x{HEIGHT}, got {width}x{height}")
    raw = zlib.decompress(b"".join(idat))
    stride = 1 + width * 3
    rows = [raw[row * stride:(row + 1) * stride] for row in range(height)]
    if any(row[0] != 0 for row in rows):
        sys.exit(f"{path}: only filter type 0 is supported")
    return b"".join(row[1:] for row in rows)


def capture(exe, extra, directory, seconds, force_era=None):
    env = dict(os.environ)
    env.pop("EVO_FORCE_ERA", None)
    if force_era is not None:
        env["EVO_FORCE_ERA"] = force_era
    out = run([exe, *extra, "--capture-dir", directory, "--time", f"{seconds:.6f}", "--width", str(WIDTH),
               "--height", str(HEIGHT)], env)
    path = out.decode().strip().splitlines()[-1]
    rgb = read_png_rgb(path)
    os.remove(path)
    return rgb


def stream_frame(exe, extra, seconds, force_era=None):
    """Frame LEAD of a stream that starts LEAD frames before `seconds`."""
    env = dict(os.environ)
    env.pop("EVO_FORCE_ERA", None)
    if force_era is not None:
        env["EVO_FORCE_ERA"] = force_era
    frames = LEAD + 1
    out = run([exe, *extra, "--render-stream", "--time", f"{seconds - LEAD / FPS:.6f}", "--fps", str(FPS),
               "--duration", f"{frames / FPS:.6f}", "--width", str(WIDTH), "--height", str(HEIGHT)], env)
    size = WIDTH * HEIGHT * 3
    if len(out) != frames * size:
        sys.exit(f"stream from {seconds - LEAD / FPS:.6f}: expected {frames} frames, got {len(out)} bytes")
    return out[LEAD * size:]


def differing_pixels(a, b):
    return sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3])


def main(exe, tmpdir, extra):
    shutil.rmtree(tmpdir, ignore_errors=True)
    info = json.loads(run([exe, "--show-info"]))
    compared = {1993: 0, 2026: 0}
    for chapter in info["chapters"]:
        start, duration, eras = chapter["start"], chapter["duration"], chapter["eras"]
        name = f"chapter {chapter['index']} {chapter['title']}"
        t = round((start + FRACTION * duration) * FPS) / FPS
        if not start + LEAD / FPS <= t < start + duration:
            sys.exit(f"{name} is too short for a {LEAD}-frame lead")
        if not eras or any(era not in ERA_VALUE for era in eras):
            sys.exit(f"{name}: unexpected eras {eras}")

        if eras == [2026]:
            runs = [(2026, None)]  # nothing to force: the chapter holds 2026 throughout
        else:
            runs = [(era, ERA_VALUE[era]) for era in eras]
            forced = {era: capture(exe, extra, tmpdir, t, force_era=ERA_VALUE[era]) for era in ERA_VALUE}
            if forced[1993] == forced[2026]:
                sys.exit(f"{name} at {t:.6f} s: the captures forced to 1993 and 2026 are identical, so the "
                         "1993 side cannot be compared")

        for era, force in runs:
            fresh = forced[era] if force is not None else capture(exe, extra, tmpdir, t)
            streamed = stream_frame(exe, extra, t, force_era=force)
            if streamed != fresh:
                sys.exit(f"{name} at {t:.6f} s ({era}): the streamed frame differs from a fresh capture in "
                         f"{differing_pixels(streamed, fresh)} of {WIDTH * HEIGHT} pixels")
            compared[era] += 1
            print(f"stream_capture: {name:<29} at {t:9.6f} s ({era}{', forced' if force else ''}): "
                  f"stream frame {LEAD} == fresh capture")
    with1993 = sum(1 for chapter in info["chapters"] if 1993 in chapter["eras"])
    if compared[1993] != with1993:
        sys.exit(f"only {compared[1993]} of the {with1993} chapters with a 1993 side were compared on it")
    print(f"stream_capture: {len(info['chapters'])} chapters match: {compared[1993]} on their 1993 side, "
          f"{compared[2026]} on their 2026 side")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit("usage: stream_capture_test.py <Evolution executable> <tmpdir> [Evolution options...]")
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
