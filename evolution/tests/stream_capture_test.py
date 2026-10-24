"""Seeking and export see the same show (Review Focus 4): for one time T in every chapter, frame 5 of a
--render-stream that starts 5 frames before T must equal a fresh --capture-dir at T, byte for byte, at
160x90. The stream renders frames T - 5/fps .. T in one process, so its retro stage has rendered (and cached)
the retro frames before T; the capture renders T from nothing. Any effect state carried over from earlier
frames shows up as a difference.

Every chapter is compared as the show has it (unforced) at T, the chapter's middle; a chapter that shows
both eras (not a Cut) is compared unforced a second time at 0.3 of its length, so the blended paths (a wipe's
seam, a portal's cross-fade, a BeatSwap flash) are compared too wherever they fall. On top of that, every
era a chapter shows (the "eras" of --show-info) is compared on its own at T, with both runs forced into it by
EVO_FORCE_ERA, so the 1993 side of every chapter that has one is checked, whatever the transition shows at T
(a BeatSwap may be on 2026 there). A forced capture must differ from one forced into the other era, which
shows the forcing took (the test fails rather than skips otherwise); a forced stream must say on stderr that
it is forced.

The stream runs at 64 fps, faster than every retro rate (35 and 10 Hz), so consecutive frames also hit the
retro cache inside one retro frame; every time compared is a multiple of 1/64 s, so
T - 5/64 + 5/64 is exactly T in double precision.

Step 4 (Review Focus 2): the path-traced chapters, with the TLAS rebuilt every frame, and the export stays
pure: no temporal history, the seed from the show time alone. Path tracing is what costs time on lavapipe, so
the chapters come in two parts that CTest runs in parallel: "studio", the traced part (every chapter whose
--show-info "scene" is not "Placeholder": the Studio's five, Decision 1 of step 4, and each step 5 scene's
chapters as they land), and "rest" (every other chapter); "all" runs both. CTest passes --spp 2 at 160x90.
All five Studio chapters must report the "Studio" scene and be in the traced part, so it never compares
placeholders where it means path-traced frames.

Usage: stream_capture_test.py <Evolution> <tmpdir> studio|rest|all [Evolution options...]; the options
(e.g. --validation --spp 2) are passed to every run."""

import json
import os
import shutil
import struct
import subprocess
import sys
import time
import zlib

WIDTH, HEIGHT = 160, 90
FPS = 64
LEAD = 5  # frames before T
FRACTION = 0.5  # where in a chapter T lies
NATURAL_EXTRA = 0.3  # where a chapter that shows both eras is compared unforced a second time
ERA_VALUE = {1993: "0", 2026: "1"}  # EVO_FORCE_ERA
# The chapters that show the Studio in step 4 (Decision 1): the "studio" part, path traced.
STUDIO_CHAPTERS = ("FINALE", "PORTAL", "STUDIO NIGHT", "RETURN", "REBOOT")
PARTS = ("studio", "rest", "all")


def in_part(chapter, part):
    traced = chapter["scene"] != "Placeholder"
    return part == "all" or (part == "studio") == traced


def run(command, env=None, stderr_out=None):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120, env=env)
    if result.returncode != 0:
        sys.stderr.write(result.stderr.decode(errors="replace"))
        sys.exit(f"{' '.join(command)} exited with {result.returncode}")
    if stderr_out is not None:
        stderr_out.append(result.stderr.decode(errors="replace"))
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
    stderr = []
    out = run([exe, *extra, "--render-stream", "--time", f"{seconds - LEAD / FPS:.6f}", "--fps", str(FPS),
               "--duration", f"{frames / FPS:.6f}", "--width", str(WIDTH), "--height", str(HEIGHT)], env, stderr)
    notice = f"render-stream: EVO_FORCE_ERA={force_era}"
    if (force_era is not None) != (notice in stderr[0]):
        sys.exit(f"stream from {seconds - LEAD / FPS:.6f} (EVO_FORCE_ERA={force_era}): expected "
                 f"{'a' if force_era is not None else 'no'} '{notice}' line on stderr")
    size = WIDTH * HEIGHT * 3
    if len(out) != frames * size:
        sys.exit(f"stream from {seconds - LEAD / FPS:.6f}: expected {frames} frames, got {len(out)} bytes")
    return out[LEAD * size:]


def differing_pixels(a, b):
    return sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3])


def compare(exe, extra, tmpdir, name, t, era, force, fresh=None):
    """The streamed frame at t equals a fresh capture (given, or captured here), with both forced or not."""
    if fresh is None:
        fresh = capture(exe, extra, tmpdir, t, force_era=force)
    streamed = stream_frame(exe, extra, t, force_era=force)
    what = f"{era}, forced" if force is not None else "unforced"
    if streamed != fresh:
        sys.exit(f"{name} at {t:.6f} s ({what}): the streamed frame differs from a fresh capture in "
                 f"{differing_pixels(streamed, fresh)} of {WIDTH * HEIGHT} pixels")
    print(f"stream_capture: {name:<38} at {t:9.6f} s ({what}): stream frame {LEAD} == fresh capture")


def main(exe, tmpdir, part, extra):
    began = time.monotonic()
    shutil.rmtree(tmpdir, ignore_errors=True)
    info = json.loads(run([exe, "--show-info"]))
    titles = [chapter["title"] for chapter in info["chapters"]]
    if any(title not in titles for title in STUDIO_CHAPTERS):
        sys.exit(f"--show-info lacks a Studio chapter: expected {STUDIO_CHAPTERS} among {titles}")
    # Every Studio chapter path traces the Studio (Decision 1), or this part would compare placeholders where
    # it means to compare path-traced frames.
    studio_scenes = {chapter["title"]: chapter["scene"] for chapter in info["chapters"]
                     if chapter["title"] in STUDIO_CHAPTERS}
    if set(studio_scenes.values()) != {"Studio"}:
        sys.exit(f"some Studio chapters are not path traced: {studio_scenes}")
    chosen = [chapter for chapter in info["chapters"] if in_part(chapter, part)]
    traced = [chapter["title"] for chapter in chosen if chapter["scene"] != "Placeholder"]
    print(f"stream_capture: part '{part}', {len(chosen)} chapters, path traced: {', '.join(traced) or 'none'}")
    if part in ("studio", "all") and not set(STUDIO_CHAPTERS) <= set(traced):
        sys.exit(f"part '{part}' path traces {traced}, expected every one of {STUDIO_CHAPTERS} among them")
    compared = {"unforced": 0, 1993: 0, 2026: 0}
    for chapter in chosen:
        start, duration, eras = chapter["start"], chapter["duration"], chapter["eras"]
        name = f"chapter {chapter['index']} {chapter['title']}"
        if chapter["scene"] != "Placeholder":
            name += f" ({chapter['scene']})"
        times = [round((start + fraction * duration) * FPS) / FPS
                 for fraction in ([FRACTION] if chapter["transition"] == "Cut" else [FRACTION, NATURAL_EXTRA])]
        if not all(start + LEAD / FPS <= t < start + duration for t in times):
            sys.exit(f"{name} is too short for a {LEAD}-frame lead")
        if not eras or any(era not in ERA_VALUE for era in eras):
            sys.exit(f"{name}: unexpected eras {eras}")
        if chapter["transition"] != "Cut" and eras != [1993, 2026]:
            sys.exit(f"{name}: a {chapter['transition']} chapter should show both eras, not {eras}")

        # As the show has it, transitions and all.
        for t in times:
            compare(exe, extra, tmpdir, name, t, None, None)
            compared["unforced"] += 1
        if eras == [2026]:
            continue  # nothing to force: the chapter holds 2026 throughout

        # Each era the chapter shows, forced, at the middle.
        t = times[0]
        forced = {era: capture(exe, extra, tmpdir, t, force_era=ERA_VALUE[era]) for era in ERA_VALUE}
        if forced[1993] == forced[2026]:
            sys.exit(f"{name} at {t:.6f} s: the captures forced to 1993 and 2026 are identical, so the 1993 "
                     "side cannot be compared")
        for era in eras:
            compare(exe, extra, tmpdir, name, t, era, ERA_VALUE[era], forced[era])
            compared[era] += 1
    print(f"stream_capture: {len(chosen)} chapters match ({len(traced)} path traced): {compared['unforced']} "
          f"times unforced, {compared[1993]} forced to 1993, {compared[2026]} forced to 2026, in "
          f"{time.monotonic() - began:.1f} s")


if __name__ == "__main__":
    if len(sys.argv) < 4 or sys.argv[3] not in PARTS:
        sys.exit("usage: stream_capture_test.py <Evolution executable> <tmpdir> studio|rest|all "
                 "[Evolution options...]")
    main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:])
