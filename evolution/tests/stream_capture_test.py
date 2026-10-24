"""Seeking and export see the same show (Review Focus 4): for one time T in every chapter, frame 5 of a
--render-stream that starts 5 frames before T must equal a fresh --capture-dir at T, byte for byte, at
160x90. The stream renders frames T - 5/fps .. T in one process, so its retro stage has rendered (and cached)
the retro frames before T; the capture renders T from nothing. Any effect state carried over from earlier
frames shows up as a difference.

The stream runs at 64 fps, faster than every retro rate (35 and 10 Hz), so consecutive frames also hit the
retro cache inside one retro frame; times are multiples of 1/64 s, so T - 5/64 + 5/64 is exactly T in double
precision. Where it can, T is a moment that shows the 1993 side (the natural capture equals one forced to
1993 with EVO_FORCE_ERA=0); chapters with no 1993 side are compared on what they show. Arguments after
<tmpdir> (e.g. --validation) are passed to every run."""

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
FRACTIONS = (0.5, 0.3, 0.7)  # where in a chapter T is tried, in order


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


def stream_frame(exe, extra, seconds):
    """Frame LEAD of a stream that starts LEAD frames before `seconds`."""
    env = dict(os.environ)
    env.pop("EVO_FORCE_ERA", None)
    frames = LEAD + 1
    out = run([exe, *extra, "--render-stream", "--time", f"{seconds - LEAD / FPS:.6f}", "--fps", str(FPS),
               "--duration", f"{frames / FPS:.6f}", "--width", str(WIDTH), "--height", str(HEIGHT)], env)
    size = WIDTH * HEIGHT * 3
    if len(out) != frames * size:
        sys.exit(f"stream from {seconds - LEAD / FPS:.6f}: expected {frames} frames, got {len(out)} bytes")
    return out[LEAD * size:]


def main(exe, tmpdir, extra):
    shutil.rmtree(tmpdir, ignore_errors=True)
    info = json.loads(run([exe, "--show-info"]))
    retro_moments = 0
    for chapter in info["chapters"]:
        start, duration = chapter["start"], chapter["duration"]
        candidates = []
        for fraction in FRACTIONS:
            t = round((start + fraction * duration) * FPS) / FPS
            if start + LEAD / FPS <= t < start + duration:
                candidates.append(t)
        if not candidates:
            sys.exit(f"chapter {chapter['index']} is too short for a {LEAD}-frame lead")

        chosen, natural, era = candidates[0], None, "2026 or mixed"
        for t in candidates:
            shown = capture(exe, extra, tmpdir, t)
            if natural is None:
                natural = shown
            if shown == capture(exe, extra, tmpdir, t, force_era="0"):
                chosen, natural, era = t, shown, "1993"
                retro_moments += 1
                break

        streamed = stream_frame(exe, extra, chosen)
        if streamed != natural:
            differing = sum(1 for i in range(0, len(natural), 3) if streamed[i:i + 3] != natural[i:i + 3])
            sys.exit(f"chapter {chapter['index']} {chapter['title']} at {chosen:.6f} s ({era}): the streamed frame "
                     f"differs from a fresh capture in {differing} of {WIDTH * HEIGHT} pixels")
        print(f"stream_capture: chapter {chapter['index']:2d} {chapter['title']:<18} at {chosen:9.6f} s "
              f"({era}): stream frame {LEAD} == fresh capture")
    print(f"stream_capture: {len(info['chapters'])} chapters match, {retro_moments} of them on the 1993 side")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit("usage: stream_capture_test.py <Evolution executable> <tmpdir> [Evolution options...]")
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
