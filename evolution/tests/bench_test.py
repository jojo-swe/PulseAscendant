"""Run Evolution --bench at a tiny size: one JSON object per chapter on stdout, with every key the RTX
hardware checkpoint reads (step 5 Task 4), sane values, and the exit code 0. Arguments after the executable (e.g.
--validation) are passed to every run."""

import json
import subprocess
import sys

KEYS = {"chapter", "title", "scene", "frames", "spp", "traceScale", "width", "height", "traceWidth",
        "traceHeight", "device", "timestamps", "gpuMs", "fps"}
PASSES = {"total", "retro", "fill", "tlas", "trace", "denoise", "upscale", "tonemap", "compositor"}


def bench(exe, extra, args):
    result = subprocess.run([exe, *extra, "--bench", "--width", "96", "--height", "54", *args],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
    sys.stderr.write(result.stderr.decode(errors="replace"))
    if result.returncode != 0:
        sys.exit(f"--bench {' '.join(args)} exited with {result.returncode}")
    lines = result.stdout.decode().splitlines()
    return [json.loads(line) for line in lines]


def check(record, chapter, frames, scale, trace_size):
    missing = KEYS - record.keys()
    if missing:
        sys.exit(f"chapter {chapter}: missing keys {sorted(missing)} in {record}")
    if record["chapter"] != chapter or record["frames"] != frames or record["spp"] != 1:
        sys.exit(f"chapter {chapter}: wrong chapter, frames or spp in {record}")
    if (record["width"], record["height"]) != (96, 54) or record["traceScale"] != scale:
        sys.exit(f"chapter {chapter}: wrong size or scale in {record}")
    if (record["traceWidth"], record["traceHeight"]) != trace_size:
        sys.exit(f"chapter {chapter}: trace size {record['traceWidth']}x{record['traceHeight']}, "
                 f"expected {trace_size[0]}x{trace_size[1]}")
    if not record["device"] or not record["title"] or not record["scene"]:
        sys.exit(f"chapter {chapter}: empty device, title or scene in {record}")
    ms = record["gpuMs"]
    if set(ms) != PASSES:
        sys.exit(f"chapter {chapter}: gpuMs passes {sorted(ms)}, expected {sorted(PASSES)}")
    if any(not isinstance(v, (int, float)) or v < 0 for v in ms.values()):
        sys.exit(f"chapter {chapter}: a negative or non-numeric time in {ms}")
    if record["timestamps"] and any(v > ms["total"] * 1.0001 for v in ms.values()):
        sys.exit(f"chapter {chapter}: a pass longer than the frame in {ms}")
    if not record["fps"] > 0:
        sys.exit(f"chapter {chapter}: fps {record['fps']}")


def main(exe, extra):
    # The reference case: STUDIO NIGHT, 3 frames, at the window's default trace scale 0.67.
    records = bench(exe, extra, ["--chapters", "11", "--bench-frames", "3"])
    if len(records) != 1:
        sys.exit(f"expected one JSON line, got {len(records)}")
    studio = records[0]
    check(studio, 11, 3, 0.67, (64, 36))
    if studio["scene"] != "Studio" or studio["title"] != "STUDIO NIGHT":
        sys.exit(f"chapter 11 is not the Studio's STUDIO NIGHT: {studio}")
    ms = studio["gpuMs"]
    if studio["timestamps"] and not (ms["trace"] > 0 and ms["upscale"] > 0 and ms["total"] > 0):
        sys.exit(f"STUDIO NIGHT at scale 0.67 must time the trace and the upscale: {ms}")
    print(f"bench: {json.dumps(studio)}")

    # Two chapters in the order given, at scale 1: no upscale pass; BOOT has no 2026 scene (the placeholder).
    records = bench(exe, extra, ["--chapters", "11,0", "--bench-frames", "2", "--trace-scale", "1"])
    if [r["chapter"] for r in records] != [11, 0]:
        sys.exit(f"expected chapters 11 and 0 in order, got {records}")
    check(records[0], 11, 2, 1, (96, 54))
    check(records[1], 0, 2, 1, (96, 54))
    if records[0]["gpuMs"]["upscale"] != 0 or records[1]["scene"] != "Placeholder":
        sys.exit(f"scale 1 must not upscale, BOOT must be the placeholder: {records}")
    print(f"bench: chapters 11 and 0 at scale 1: {[r['gpuMs']['total'] for r in records]} ms")

    # A chapter the show does not have.
    result = subprocess.run([exe, "--bench", "--chapters", "99", "--width", "96", "--height", "54"],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    if result.returncode != 1 or b"--chapters" not in result.stderr:
        sys.exit(f"--chapters 99: expected exit 1 naming --chapters, got {result.returncode}: {result.stderr}")
    print("bench: --chapters 99 refused")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: bench_test.py <Evolution executable> [Evolution options...]")
    main(sys.argv[1], sys.argv[2:])
