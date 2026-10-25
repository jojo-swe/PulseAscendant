"""tools/render_video.py on a short stretch at a tiny size: two chunks that resume after one is deleted, frames
equal to a single --render-stream byte for byte (lossless ffv1), on a 1993 stretch at 1 spp and on the
path-traced Studio at 8 spp (two time slices of the export shutter), audio as long as the video to within a
frame, a parts directory of another build refused, --coda's exact frame count and its black, and --bench's
estimate. Runs a copy of the executable and its shaders, so it can stand in for a rebuild. Needs ffmpeg and
ffprobe.

    python3 tests/render_video_test.py build/linux/bin/Evolution
"""

import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "tools", "render_video.py")
W, H = 64, 36
APP_ARGS = ["--detail", "0.004"]


def fail(message):
    sys.exit(f"FAILED: {message}")


def run_tool(args):
    out = subprocess.run([sys.executable, TOOL, *args, "--", *APP_ARGS], stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, timeout=240)
    if out.returncode != 0:
        sys.stderr.write(out.stderr.decode(errors="replace"))
        fail(f"render_video.py {' '.join(args)} exited with {out.returncode}")
    return out.stdout.decode()


def decode_video(path, width=W, height=H):
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-map", "0:v", "-fps_mode", "passthrough", "-f",
                          "rawvideo", "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE, check=True)
    size = width * height * 3
    data = out.stdout
    return [data[i:i + size] for i in range(0, len(data), size)]


def audio_seconds(path):
    """The audio's decoded length less the encoder's priming, which Matroska keeps as a negative start time."""
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-map", "0:a", "-f", "s16le", "-ac", "2", "-ar",
                          "48000", "-"], stdout=subprocess.PIPE, check=True)
    start = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=start_time",
                            "-of", "csv=p=0", path], stdout=subprocess.PIPE, check=True).stdout.decode().strip()
    return len(out.stdout) / 4 / 48000 + min(float(start or 0), 0.0)


def write_music(path, seconds):
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(48000)
        frames = bytearray()
        for i in range(int(seconds * 48000)):
            v = int(8000 * math.sin(2 * math.pi * 220 * i / 48000))
            frames += struct.pack("<hh", v, v)
        w.writeframes(bytes(frames))


def stream_bytes(exe, start, duration, spp):
    return subprocess.run([exe, "--render-stream", "--width", str(W), "--height", str(H), "--fps", "60",
                           "--spp", str(spp), "--time", repr(start), "--duration", repr(duration), *APP_ARGS],
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=True).stdout


def main(built):
    if shutil.which("ffmpeg") is None or shutil.which("ffprobe") is None:
        fail("ffmpeg and ffprobe are needed")
    work = tempfile.mkdtemp(prefix="render_video_test_")
    try:
        # A copy of the executable beside a copy of its shaders: touching it later stands in for a rebuild.
        bin_dir = os.path.join(work, "bin")
        os.makedirs(bin_dir)
        exe = os.path.join(bin_dir, os.path.basename(built))
        shutil.copy2(built, exe)
        shutil.copytree(os.path.join(os.path.dirname(os.path.abspath(built)), "shaders"),
                        os.path.join(bin_dir, "shaders"))
        music = os.path.join(work, "music.wav")
        write_music(music, 30)
        common = ["--exe", exe, "--music", music, "--width", str(W), "--height", str(H), "--spp", "1",
                  "--codec", "ffv1"]

        # 1.0-1.5 s at 60 fps in two chunks of 15 frames.
        out = os.path.join(work, "show.mkv")
        args = common + ["--out", out, "--start", "1.0", "--end", "1.5", "--chunk", "0.25"]
        first = run_tool(args)
        if first.count(": rendering") != 2:
            fail(f"expected two chunks rendered:\n{first}")
        os.remove(os.path.join(out + ".parts", "chunk_00001.mkv"))
        second = run_tool(args)
        if "chunk 1/2 (frames 0-14): done, skipping" not in second or "chunk 2/2 (frames 15-29): rendering" \
                not in second:
            fail(f"a rerun should render only the deleted chunk:\n{second}")

        frames = decode_video(out)
        stream = stream_bytes(exe, 1.0, 0.5, 1)
        if len(frames) != 30 or b"".join(frames) != stream:
            fail(f"the video's {len(frames)} frames differ from one --render-stream")
        video_s = len(frames) / 60
        audio_s = audio_seconds(out)
        if abs(audio_s - video_s) > 1 / 60:
            fail(f"audio {audio_s:.4f} s against video {video_s:.4f} s")
        print(f"render_video: 2 chunks, resume renders 1, 30 frames byte-exact, audio {audio_s:.4f} s")

        # Another build (here: the same bytes with another time) must not resume these chunks.
        stat = os.stat(exe)
        os.utime(exe, ns=(stat.st_atime_ns, stat.st_mtime_ns - 10**9))
        refused = subprocess.run([sys.executable, TOOL, *args, "--", *APP_ARGS], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, timeout=240)
        if refused.returncode == 0 or b"another build" not in refused.stderr:
            fail(f"a parts directory of another build was not refused:\n{refused.stderr.decode(errors='replace')}")
        print("render_video: chunks of another build refused")

        # STUDIO NIGHT, path traced at 8 spp (two shutter slices), in two chunks of 8 and 7 frames.
        studio = os.path.join(work, "studio.mkv")
        studio_args = common + ["--out", studio, "--start", "205.0", "--end", "205.25", "--chunk", "0.125",
                                "--spp", "8"]
        if run_tool(studio_args).count(": rendering") != 2:
            fail("expected two Studio chunks rendered")
        studio_frames = decode_video(studio)
        if len(studio_frames) != 15 or b"".join(studio_frames) != stream_bytes(exe, 205.0, 0.25, 8):
            fail(f"the Studio video's {len(studio_frames)} frames differ from one --render-stream at 8 spp")
        print("render_video: the Studio at 8 spp in 2 chunks, 15 frames byte-exact")

        # The coda at 10 fps: 5 show frames, then 120 black, 280 of the part, 20 black.
        coda_out = os.path.join(work, "coda.mkv")
        run_tool(common + ["--out", coda_out, "--start", "1.0", "--end", "1.5", "--fps", "10", "--coda"])
        coda = decode_video(coda_out)
        if len(coda) != 5 + (12 + 28 + 2) * 10:
            fail(f"--coda: {len(coda)} frames, expected {5 + 420}")
        black = bytes(W * H * 3)
        if any(f != black for f in coda[5:125]) or any(f != black for f in coda[405:]):
            fail("--coda: the black before and after the part is not black")
        lit = sum(1 for f in coda[125:405] if f != black)
        if lit < 250:
            fail(f"--coda: only {lit} of the part's 280 frames show anything")
        coda_audio = audio_seconds(coda_out)
        if abs(coda_audio - len(coda) / 10) > 1 / 10:
            fail(f"--coda: audio {coda_audio:.3f} s against video {len(coda) / 10:.3f} s")
        print(f"render_video: --coda adds 420 frames, black around a part of {lit} lit frames")

        bench = run_tool(["--exe", exe, "--width", "32", "--height", "18", "--spp", "1", "--bench", "--chapters",
                          "0,3,30"])
        if "ETA:" not in bench or bench.count("s/frame") < 3:
            fail(f"--bench printed no estimate:\n{bench}")
        print("render_video: " + bench.strip().splitlines()[-1])
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: render_video_test.py <Evolution executable>")
    main(sys.argv[1])
