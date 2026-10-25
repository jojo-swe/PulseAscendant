"""Resumable chunked export of the full show, for machines without NVENC.

Renders fixed-length chunks (exact frame times: chunk k starts at frame
k*chunk*fps) to video-only x264 files, skipping chunks already verified, then
concatenates them losslessly and muxes the original combined soundtrack once.
A restart resumes at the first missing chunk. Use tools/render_video.py for the
single-pass NVIDIA path.
"""
import argparse
import json
import math
import os
import subprocess
import sys
import time
import wave
from pathlib import Path


def frames_in(path):
    probe = subprocess.run(['ffprobe', '-v', 'error', '-count_packets', '-select_streams', 'v:0',
                            '-show_entries', 'stream=nb_read_packets,width,height', '-of', 'json', str(path)],
                           capture_output=True, text=True)
    if probe.returncode:
        return None
    streams = json.loads(probe.stdout).get('streams', [])
    return (int(streams[0]['nb_read_packets']), streams[0]['width'], streams[0]['height']) if streams else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path, help='Chunk directory (kept for resume)')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--width', type=int, default=3840)
    parser.add_argument('--height', type=int, default=2160)
    parser.add_argument('--fps', type=int, default=60)
    parser.add_argument('--render-scale', type=float, default=.5)
    parser.add_argument('--chunk', type=float, default=30, help='Chunk length in seconds; chunk*fps must be an integer')
    parser.add_argument('--crf', type=int, default=15)
    parser.add_argument('--preset', default='veryfast')
    parser.add_argument('--no-rt', action='store_true')
    parser.add_argument('--only', help='Comma-separated chunk indices to render (for testing)')
    args = parser.parse_args()
    if abs(args.chunk * args.fps - round(args.chunk * args.fps)) > 1e-9:
        parser.error('chunk*fps must be a whole number of frames')
    app = args.app.resolve()
    show = json.loads(subprocess.check_output([str(app), '--show-info'], text=True))
    length = float(show['duration'])
    music = (app.parent / 'assets' / show['music']).resolve()
    with wave.open(str(music), 'rb') as audio:
        if (audio.getframerate(), audio.getnchannels(), audio.getsampwidth()) != (48000, 2, 2) or audio.getnframes() != round(length * 48000):
            parser.error('Music format or sample count differs from the native show timeline')
    total = math.ceil(length * args.fps - 1e-9)
    per = round(args.chunk * args.fps)
    count = math.ceil(total / per)
    args.work.mkdir(parents=True, exist_ok=True)
    selected = range(count) if not args.only else [int(i) for i in args.only.split(',')]
    for k in selected:
        frames = min(per, total - k * per)
        chunk = args.work / f'chunk-{k:03d}.mp4'
        info = frames_in(chunk) if chunk.exists() else None
        if info == (frames, args.width, args.height):
            continue
        partial = args.work / f'chunk-{k:03d}.partial.mp4'
        partial.unlink(missing_ok=True)
        start = k * per / args.fps
        producer = [str(app), '--render-stream', '--fps', str(args.fps), '--width', str(args.width), '--height', str(args.height),
                    '--render-scale', str(args.render_scale), '--time', repr(start), '--duration', repr(min(frames / args.fps, length - start))]
        if args.no_rt:
            producer.append('--no-rt')
        encoder = ['ffmpeg', '-hide_banner', '-nostdin', '-loglevel', 'error', '-f', 'rawvideo', '-pixel_format', 'rgb24',
                   '-video_size', f'{args.width}x{args.height}', '-framerate', str(args.fps), '-i', 'pipe:0',
                   '-vf', 'vflip,scale=in_range=full:out_range=tv:out_color_matrix=bt709,format=yuv420p',
                   '-c:v', 'libx264', '-preset', args.preset, '-crf', str(args.crf), '-tune', 'film',
                   '-g', str(args.fps * 2), '-bf', '3', '-colorspace', 'bt709', '-color_primaries', 'bt709',
                   '-color_trc', 'bt709', '-color_range', 'tv', '-frames:v', str(frames), str(partial)]
        began = time.time()
        with open(args.work / f'chunk-{k:03d}.log', 'wb') as log:
            render = subprocess.Popen(producer, stdout=subprocess.PIPE, stderr=log)
            encode = subprocess.Popen(encoder, stdin=render.stdout, stderr=log)
            render.stdout.close()
            if encode.wait() or render.wait():
                raise SystemExit(f'Chunk {k} failed; see {args.work}/chunk-{k:03d}.log')
        if frames_in(partial) != (frames, args.width, args.height):
            raise SystemExit(f'Chunk {k} has the wrong frame count or size')
        os.replace(partial, chunk)
        print(f'CHUNK {k+1}/{count}: {frames} frames from {start:.3f} s in {time.time()-began:.0f} s', flush=True)
    if args.only:
        return
    listing = args.work / 'chunks.txt'
    listing.write_text(''.join(f"file 'chunk-{k:03d}.mp4'\n" for k in range(count)))
    partial = args.output.with_suffix('.rendering.mp4')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['ffmpeg', '-hide_banner', '-nostdin', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0',
                    '-i', str(listing), '-i', str(music), '-map', '0:v:0', '-map', '1:a:0', '-c:v', 'copy',
                    '-af', f'atrim=duration={length}', '-c:a', 'aac', '-b:a', '320k', '-ar', '48000',
                    '-movflags', '+faststart', '-metadata', f'title={show["title"]}',
                    '-metadata', 'artist=jojo-swe; music: Luminous Flow, Adventurous Uplift and Rigid Arpeggios / joyolo',
                    '-metadata', 'comment=Three acts.', str(partial)], check=True)
    probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-count_packets', '-show_streams', '-show_format',
                                                '-of', 'json', str(partial)]))
    video = next(s for s in probe['streams'] if s['codec_type'] == 'video')
    audio = next(s for s in probe['streams'] if s['codec_type'] == 'audio')
    assert (video['width'], video['height']) == (args.width, args.height)
    assert video['avg_frame_rate'] == f'{args.fps}/1', video['avg_frame_rate']
    assert int(video['nb_read_packets']) == total, video['nb_read_packets']
    assert abs(float(audio['duration']) - length) < .05
    os.replace(partial, args.output)
    print(f'VIDEO PASS: {args.output.name}, {args.width}x{args.height}, {args.fps} fps, {total} frames, '
          f'{float(probe["format"]["duration"]):.3f} s, {args.output.stat().st_size} bytes', flush=True)


if __name__ == '__main__':
    sys.exit(main())
