"""Render the show to a video file: Evolution's --render-stream piped into ffmpeg, in chunks that survive an
interruption, then joined and muxed with the show's music.

    python3 tools/render_video.py --exe build/linux/bin/Evolution --out dist/render/EVOLUTION-4K60.mp4
    python3 tools/render_video.py --exe EXE --out OUT.mp4 --codec hevc_nvenc
    python3 tools/render_video.py --exe EXE --out OUT.mp4 --bench --chapters 0,7,15

Defaults: 3840x2160, 60 fps, 64 samples per pixel, the whole show (--start 0 --end <show duration>), 10-second
chunks, libx264.

Chunks. Chunk k is frames [k * chunk * fps, (k + 1) * chunk * fps) of the range (the last one shorter), rendered
with `--render-stream --time START --first-frame F`, so every frame is the very frame a single stream of the
whole range would give, and encoded into <out>.parts/chunk_KKKKK.mkv. A chunk is written as .part and renamed
when ffmpeg finishes; a rerun skips every chunk whose file holds exactly the frames it should (ffprobe), so an
interrupted render resumes where it stopped. Change the size, rate, spp or codec, or rebuild the executable or its
shaders (the parts directory's settings.json records their size, time and hashes), and the parts directory no
longer matches: the script refuses rather than mixing them (delete it or pick another --out). A chunk that fails
(the app or ffmpeg exits with an error, e.g. a lost GPU device) is rendered once more before the script stops.

Codecs. x264: libx264 -preset slow -crf 12, High profile, 4:2:0, converted from the stream's full-range RGB with
the BT.709 matrix into TV range and tagged BT.709 (ffmpeg's default matrix is BT.601). hevc_nvenc: the same
conversion and tags on an NVIDIA encoder. ffv1: lossless RGB in Matroska, for checks and archives.

Assembly. The chunks are joined without re-encoding (concat, -c copy), and the music, cut sample-exact to the
same range and padded with silence to the video's length, is muxed as AAC-LC at 384 kb/s, 48 kHz; MP4 gets
+faststart and a video timescale of the frame rate, so every frame lasts exactly 1/fps (the Matroska chunks
keep millisecond timestamps).

--coda appends, after the show, 12 s of black and silence, then 28 s of `--render-stream --secret-part` over the
music's first 28 s, then 2 s of black and silence.

--bench renders one export frame at each chapter's accent (and the frame after it, to separate the per-frame cost
from the start-up), prints seconds a frame per chapter and an estimate of the whole render. --chapters LIST picks
chapters (indices as --show-info numbers them); the others are estimated from the mean.

Other options go to Evolution unchanged after `--`, e.g. `-- --detail 0.5`.
"""

import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import time
import wave

CODA_BLACK_BEFORE = 12.0
CODA_PART = 28.0
CODA_BLACK_AFTER = 2.0
AUDIO_RATE = 48000
FINGERPRINT_HEAD = 1 << 20  # the executable's bytes hashed for its fingerprint
ATTEMPTS = 2  # a failed chunk is rendered once more

VIDEO_ARGS = {
    "x264": ["-c:v", "libx264", "-preset", "slow", "-crf", "12", "-profile:v", "high", "-pix_fmt", "yuv420p"],
    "hevc_nvenc": ["-c:v", "hevc_nvenc", "-preset", "p7", "-tune", "hq", "-rc", "vbr", "-cq", "16", "-b:v", "0",
                   "-maxrate", "240M", "-bufsize", "480M", "-profile:v", "main", "-pix_fmt", "yuv420p"],
    "ffv1": ["-c:v", "ffv1", "-level", "3", "-pix_fmt", "gbrp"],
}
BT709 = ["-vf", "scale=out_color_matrix=bt709:out_range=tv", "-color_primaries", "bt709", "-color_trc", "bt709",
         "-colorspace", "bt709", "-color_range", "tv"]


def log(message):
    print(message, flush=True)


def show_info(exe):
    out = subprocess.run([exe, "--show-info"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    return json.loads(out.stdout)


def frame_count(seconds, fps):
    """Frames a stream of `seconds` shows at `fps`: the app's ceil(duration * fps - 1e-9)."""
    return max(0, math.ceil(seconds * fps - 1e-9))


def encoder_args(args):
    video = list(VIDEO_ARGS[args.codec])
    if args.codec != "ffv1":
        video += BT709
    return video


def encode_command(args, output):
    return (["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s",
             f"{args.width}x{args.height}", "-r", str(args.fps), "-i", "pipe:0", *encoder_args(args),
             "-f", "matroska", output])


def probe_frames(path):
    """The video packets in a file (one a frame for these encoders), or -1 if it cannot be read."""
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-count_packets", "-show_entries",
                          "stream=nb_read_packets", "-of", "csv=p=0", path],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        return int(out.stdout.decode().strip().split(",")[0])
    except ValueError:
        return -1


def app_command(args, time_s, first_frame, frames, extra):
    return [args.exe, "--render-stream", "--width", str(args.width), "--height", str(args.height), "--fps",
            str(args.fps), "--spp", str(args.spp), "--time", repr(time_s), "--first-frame", str(first_frame),
            "--duration", repr(frames / args.fps), *extra]


def encode_attempt(args, partial, frames, source):
    """One try at encoding `frames` frames into `partial`; None on success, else what went wrong."""
    encoder = subprocess.Popen(encode_command(args, partial), stdin=subprocess.PIPE)
    app = None
    try:
        if source is None:
            black = bytes(args.width * args.height * 3)
            for _ in range(frames):
                encoder.stdin.write(black)
            encoder.stdin.close()
            app_code = 0
        else:
            app = subprocess.Popen(source, stdout=encoder.stdin)
            encoder.stdin.close()  # the app holds the pipe now
            app_code = app.wait()
        enc_code = encoder.wait()
    except BaseException:
        # Ctrl+C or an error here: stop the app too, or it keeps rendering on the GPU.
        for process in (app, encoder):
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
        raise
    if app_code != 0 or enc_code != 0:
        return f"Evolution exited with {app_code}, ffmpeg with {enc_code}"
    got = probe_frames(partial)
    if got != frames:
        return f"expected {frames} frames, ffmpeg wrote {got}"
    return None


def encode_piece(args, path, frames, source):
    """Encodes `frames` frames into `path` (via path.part), from an app command or from black (None); a failed
    attempt (e.g. the GPU lost its device) is tried once more."""
    if os.path.exists(path) and probe_frames(path) == frames:
        return False
    partial = path + ".part"
    for attempt in range(1, ATTEMPTS + 1):
        error = encode_attempt(args, partial, frames, source)
        if error is None:
            os.replace(partial, path)
            return True
        if attempt < ATTEMPTS:
            log(f"{os.path.basename(path)}: {error}; trying once more")
    sys.exit(f"{os.path.basename(path)}: {error} (twice)")


def plan_chunks(args, total):
    per_chunk = max(1, round(args.chunk * args.fps))
    return [(k, k * per_chunk, min(per_chunk, total - k * per_chunk)) for k in range((total + per_chunk - 1) // per_chunk)]


def build_fingerprint(exe):
    """What tells one build from another: the executable's size, modification time and the SHA-256 of its first
    MiB, and the SHA-256 of its compiled shaders (bin/shaders/*.spv beside it: a shader-only change leaves the
    executable as it was)."""
    stat = os.stat(exe)
    with open(exe, "rb") as f:
        head = hashlib.sha256(f.read(FINGERPRINT_HEAD)).hexdigest()
    shaders = hashlib.sha256()
    folder = os.path.join(os.path.dirname(os.path.abspath(exe)), "shaders")
    names = sorted(n for n in os.listdir(folder) if n.endswith(".spv")) if os.path.isdir(folder) else []
    for name in names:
        shaders.update(name.encode() + b"\0")
        with open(os.path.join(folder, name), "rb") as f:
            shaders.update(hashlib.sha256(f.read()).digest())
    return {"size": stat.st_size, "mtime_ns": stat.st_mtime_ns, "sha256_head": head,
            "shaders": len(names), "shaders_sha256": shaders.hexdigest()}


def check_parts_dir(args, parts):
    settings = {"width": args.width, "height": args.height, "fps": args.fps, "spp": args.spp, "codec": args.codec,
                "start": args.start, "chunk": args.chunk, "extra": args.extra, "build": build_fingerprint(args.exe)}
    stamp = os.path.join(parts, "settings.json")
    if os.path.exists(stamp):
        with open(stamp) as f:
            recorded = json.load(f)
        others = {k: v for k, v in settings.items() if k != "build"}
        if recorded.get("build") != settings["build"] and {k: v for k, v in recorded.items() if k != "build"} == others:
            sys.exit(f"{parts} holds chunks of another build of Evolution (the executable or its shaders changed); "
                     "delete it or choose another --out, so the video does not mix two builds")
        if recorded != settings:
            sys.exit(f"{parts} holds a render with other settings; delete it or choose another --out")
    else:
        os.makedirs(parts, exist_ok=True)
        with open(stamp, "w") as f:
            json.dump(settings, f, indent=1)


def read_pcm(path):
    with wave.open(path, "rb") as w:
        if w.getframerate() != AUDIO_RATE or w.getsampwidth() != 2 or w.getnchannels() != 2:
            sys.exit(f"{path}: expected 48 kHz 16-bit stereo PCM")
        return w.readframes(w.getnframes())


def write_audio(args, path, total_frames, coda):
    """The music for the rendered range, sample-exact, padded with silence to the video's length; with the coda
    after it: silence, the music's first 28 s, silence."""
    pcm = read_pcm(args.music)
    bytes_per = 4
    first = round(args.start * AUDIO_RATE)
    count = round(total_frames / args.fps * AUDIO_RATE)
    body = pcm[first * bytes_per:(first + count) * bytes_per]
    body += bytes(count * bytes_per - len(body))
    if coda:
        body += bytes(round(CODA_BLACK_BEFORE * AUDIO_RATE) * bytes_per)
        part = pcm[:round(CODA_PART * AUDIO_RATE) * bytes_per]
        body += part + bytes(round(CODA_PART * AUDIO_RATE) * bytes_per - len(part))
        body += bytes(round(CODA_BLACK_AFTER * AUDIO_RATE) * bytes_per)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(AUDIO_RATE)
        w.writeframes(body)


def render(args, info):
    end = info["duration"] if args.end is None else args.end
    total = frame_count(end - args.start, args.fps)
    if total < 1:
        sys.exit("nothing to render: --end must be after --start")
    parts = args.out + ".parts"
    check_parts_dir(args, parts)
    pieces = []
    chunks = plan_chunks(args, total)
    for k, first, frames in chunks:
        path = os.path.join(parts, f"chunk_{k:05d}.mkv")
        label = f"chunk {k + 1}/{len(chunks)} (frames {first}-{first + frames - 1})"
        if os.path.exists(path) and probe_frames(path) == frames:
            log(f"{label}: done, skipping")
        else:
            log(f"{label}: rendering")
            started = time.monotonic()
            encode_piece(args, path, frames, app_command(args, args.start, first, frames, args.extra))
            log(f"{label}: {time.monotonic() - started:.1f} s")
        pieces.append(path)
    if args.coda:
        coda = [("coda_black_before.mkv", frame_count(CODA_BLACK_BEFORE, args.fps), None),
                ("coda_part.mkv", frame_count(CODA_PART, args.fps),
                 app_command(args, 0.0, 0, frame_count(CODA_PART, args.fps), ["--secret-part", *args.extra])),
                ("coda_black_after.mkv", frame_count(CODA_BLACK_AFTER, args.fps), None)]
        for name, frames, source in coda:
            path = os.path.join(parts, name)
            log(f"{name}: {'rendering' if encode_piece(args, path, frames, source) else 'done, skipping'}")
            pieces.append(path)

    listing = os.path.join(parts, "concat.txt")
    with open(listing, "w") as f:
        for path in pieces:
            f.write(f"file '{os.path.abspath(path)}'\n")
    audio = os.path.join(parts, "audio.wav")
    write_audio(args, audio, total, args.coda)
    mp4 = args.out.lower().endswith((".mp4", ".mov", ".m4v"))
    command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0", "-i", listing,
               "-i", audio, "-map", "0:v", "-map", "1:a", "-c:v", "copy", "-c:a", "aac", "-profile:a", "aac_low",
               "-b:a", "384k", "-ar", str(AUDIO_RATE)]
    if mp4:
        # The chunks' millisecond timestamps, rounded to the frame rate's timescale: every frame 1/fps.
        command += ["-movflags", "+faststart", "-video_track_timescale", str(args.fps)]
    if args.codec == "hevc_nvenc" and mp4:
        command += ["-tag:v", "hvc1"]
    subprocess.run(command + [args.out], check=True)
    log(f"wrote {args.out}: {total} show frames{' + coda' if args.coda else ''} at {args.fps} fps")


def bench(args, info):
    chapters = info["chapters"]
    chosen = [chapters[i] for i in args.chapters] if args.chapters else chapters
    log(f"bench: {args.width}x{args.height}, {args.spp} spp, {len(chosen)} of {len(chapters)} chapters")
    per_frame = {}
    setup = []
    for chapter in chosen:
        times = []
        for frames in (1, 2):
            started = time.monotonic()
            subprocess.run(app_command(args, chapter["accent"], 0, frames, args.extra), stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, check=True)
            times.append(time.monotonic() - started)
        cost = max(times[1] - times[0], 1e-3)
        per_frame[chapter["index"]] = cost
        setup.append(max(times[0] - cost, 0.0))
        log(f"  {chapter['index']:2d} {chapter['title']:<18} accent {chapter['accent']:7.2f} s  {cost:8.3f} s/frame")
    mean = sum(per_frame.values()) / len(per_frame)
    total_s = 0.0
    total_frames = 0
    for chapter in chapters:
        # The frames whose times fall in the chapter: those of one stream from 0, split at its bounds.
        frames = (frame_count(chapter["start"] + chapter["duration"], args.fps) -
                  frame_count(chapter["start"], args.fps))
        total_frames += frames
        total_s += frames * per_frame.get(chapter["index"], mean)
    chunks = math.ceil(total_frames / max(1, round(args.chunk * args.fps)))
    total_s += chunks * (sum(setup) / len(setup))
    note = "" if len(per_frame) == len(chapters) else f" (other chapters at the mean, {mean:.3f} s/frame)"
    log(f"ETA: {total_s / 3600:.2f} h for {total_frames} frames in {chunks} chunks{note}")
    return total_s


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="the Evolution executable")
    parser.add_argument("--out", help="the video file (.mp4 for x264/hevc_nvenc, .mkv for ffv1)")
    parser.add_argument("--music", help="the show's WAV (default: assets/music/show.wav beside the executable)")
    parser.add_argument("--width", type=int, default=3840)
    parser.add_argument("--height", type=int, default=2160)
    parser.add_argument("--fps", type=int, default=60)
    parser.add_argument("--spp", type=int, default=64)
    parser.add_argument("--chunk", type=float, default=10.0, help="seconds a chunk (default 10)")
    parser.add_argument("--start", type=float, default=0.0)
    parser.add_argument("--end", type=float, default=None, help="default: the show's duration")
    parser.add_argument("--codec", choices=sorted(VIDEO_ARGS), default="x264")
    parser.add_argument("--coda", action="store_true", help="append the coda after the show")
    parser.add_argument("--bench", action="store_true", help="time one frame per chapter and estimate the render")
    parser.add_argument("--chapters", type=lambda s: [int(x) for x in s.split(",")], default=None)
    parser.add_argument("extra", nargs="*", help="after --: options passed to Evolution")
    args = parser.parse_args()

    for tool in ("ffmpeg", "ffprobe"):
        if not args.bench and shutil.which(tool) is None:
            sys.exit(f"{tool} not found on PATH")
    info = show_info(args.exe)
    if args.bench:
        bench(args, info)
        return
    if not args.out:
        sys.exit("--out is required")
    if args.codec == "ffv1" and args.out.lower().endswith(".mp4"):
        sys.exit("ffv1 goes in Matroska: use an .mkv --out")
    if args.music is None:
        args.music = os.path.join(os.path.dirname(os.path.abspath(args.exe)), info["music"])
    if not os.path.exists(args.music):
        sys.exit(f"music not found: {args.music} (pass --music)")
    out_dir = os.path.dirname(os.path.abspath(args.out))
    os.makedirs(out_dir, exist_ok=True)
    render(args, info)


if __name__ == "__main__":
    main()
