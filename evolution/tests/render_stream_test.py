"""Run Evolution headless: --render-stream must emit exactly the expected raw RGB frames, and
--device-info must print JSON that reports ray tracing. Arguments after the executable (e.g. --validation)
are passed to every run."""

import json
import subprocess
import sys

WIDTH, HEIGHT, FPS, DURATION = 64, 36, 10, 1.05
FRAMES = 11  # ceil(1.05 * 10): frames at t = 0.0, 0.1, ..., 1.0


def main(exe, extra):
    stream = subprocess.run(
        [exe, *extra, "--render-stream", "--width", str(WIDTH), "--height", str(HEIGHT), "--fps", str(FPS),
         "--duration", str(DURATION)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
    sys.stderr.write(stream.stderr.decode(errors="replace"))
    if stream.returncode != 0:
        sys.exit(f"--render-stream exited with {stream.returncode}")

    frame_size = WIDTH * HEIGHT * 3
    data = stream.stdout
    if len(data) != FRAMES * frame_size:
        sys.exit(f"expected {FRAMES} frames ({FRAMES * frame_size} bytes), got {len(data)} bytes")
    first = data[:frame_size]
    last = data[10 * frame_size:11 * frame_size]
    if first == last:
        sys.exit("frame 0 and frame 10 are identical; time is not advancing")
    print(f"render-stream: {FRAMES} frames of {WIDTH}x{HEIGHT}, frame 0 != frame 10")

    info = subprocess.run([exe, *extra, "--device-info"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    sys.stderr.write(info.stderr.decode(errors="replace"))
    if info.returncode != 0:
        sys.exit(f"--device-info exited with {info.returncode}")
    parsed = json.loads(info.stdout)
    if parsed.get("rayTracing") is not True:
        sys.exit(f"device-info does not report rayTracing: {parsed}")
    print(f"device-info: {parsed['device']}, rayTracing true")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit("usage: render_stream_test.py <Evolution executable> [Evolution options...]")
    main(sys.argv[1], sys.argv[2:])
