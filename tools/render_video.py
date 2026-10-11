"""Render exact-time frames to a 4K60 MP4 without intermediate image files.

Requires ffmpeg/ffprobe with NVIDIA h264_nvenc, and the built native demo.
"""
import argparse
import json
import math
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--music', type=Path)
    parser.add_argument('--width', type=int, default=3840)
    parser.add_argument('--height', type=int, default=2160)
    parser.add_argument('--fps', type=int, default=60)
    parser.add_argument('--start', type=float, default=0)
    parser.add_argument('--duration', type=float, default=472.36)
    args = parser.parse_args()
    app, output = args.app.resolve(), args.output.resolve()
    music = (args.music or app.parent / 'assets/music/luminous-flow.wav').resolve()
    if args.width % 2 or args.height % 2:
        parser.error('H.264 output requires even width and height')
    if args.start < 0 or args.start + args.duration > 472.36 or args.duration <= 0:
        parser.error('Export range must be within the complete 472.36-second song')
    output.parent.mkdir(parents=True, exist_ok=True)
    partial = output.with_suffix('.rendering.mp4')
    if output.exists() or partial.exists():
        parser.error('Output or partial render already exists; choose a new output name')
    producer_args = [str(app), '--render-stream', '--fps', str(args.fps), '--width', str(args.width),
                     '--height', str(args.height), '--time', str(args.start), '--duration', str(args.duration)]
    encoder_args = ['ffmpeg', '-hide_banner', '-nostdin', '-f', 'rawvideo', '-pixel_format', 'rgb24',
                    '-video_size', f'{args.width}x{args.height}', '-framerate', str(args.fps), '-i', 'pipe:0',
                    '-ss', str(args.start), '-i', str(music), '-map', '0:v:0', '-map', '1:a:0',
                    '-vf', 'vflip,scale=in_range=full:out_range=tv:out_color_matrix=bt709,format=yuv420p',
                    '-c:v', 'h264_nvenc', '-preset', 'p6', '-tune', 'hq', '-rc', 'vbr', '-cq', '18',
                    '-b:v', '0', '-maxrate', '28M', '-bufsize', '56M', '-multipass', 'qres',
                    '-rc-lookahead', '24', '-spatial-aq', '1', '-temporal-aq', '1', '-bf', '3',
                    '-g', str(args.fps * 2), '-fps_mode', 'cfr', '-colorspace', 'bt709',
                    '-color_primaries', 'bt709', '-color_trc', 'bt709', '-color_range', 'tv',
                    '-c:a', 'aac', '-b:a', '320k', '-ar', '48000', '-t', str(args.duration),
                    '-movflags', '+faststart', '-metadata', 'title=Second Reality / Full Throttle',
                    '-metadata', 'artist=jojo-swe; music: Luminous Flow / joyolo (Suno)',
                    '-metadata', 'comment=Music generated with Suno; id=1f52e6e8-5a07-45da-9c9b-09753a609eb3',
                    '-progress', 'pipe:1', '-nostats', str(partial)]
    log_dir = output.parent / 'video-logs'
    log_dir.mkdir(exist_ok=True)
    prefix = log_dir / output.stem
    producer = encoder = None
    try:
        with Path(str(prefix) + '-render.log').open('wb') as render_log, \
             Path(str(prefix) + '-encode.log').open('wb') as encode_log, \
             Path(str(prefix) + '-progress.log').open('wb') as progress_log:
            producer = subprocess.Popen(producer_args, stdout=subprocess.PIPE, stderr=render_log)
            encoder = subprocess.Popen(encoder_args, stdin=producer.stdout, stdout=progress_log, stderr=encode_log)
            producer.stdout.close()
            encoder_status = encoder.wait()
            producer_status = producer.wait()
            if encoder_status or producer_status:
                raise RuntimeError(f'Render={producer_status}, encoder={encoder_status}; inspect {log_dir}')
    except BaseException:
        for process in (producer, encoder):
            if process is not None and process.poll() is None:
                process.terminate()
                process.wait()
        raise
    probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-show_format',
                                                '-of', 'json', str(partial)]))
    video = next(s for s in probe['streams'] if s['codec_type'] == 'video')
    audio = next(s for s in probe['streams'] if s['codec_type'] == 'audio')
    expected_frames = math.ceil(args.duration * args.fps - 1e-9)
    assert (video['width'], video['height']) == (args.width, args.height)
    assert video['avg_frame_rate'] == f'{args.fps}/1'
    assert int(video['nb_frames']) == expected_frames
    assert abs(float(audio['duration']) - args.duration) < .05
    assert partial.stat().st_size < 2_000_000_000, 'Video exceeds this release asset size budget'
    partial.rename(output)
    Path(str(prefix) + '-probe.json').write_text(json.dumps(probe, indent=2) + '\n', encoding='utf-8')
    print(f'VIDEO PASS: {output.name}, {args.width}x{args.height}, {args.fps} fps, {expected_frames} frames, '
          f'{float(probe["format"]["duration"]):.3f} s, {output.stat().st_size} bytes', flush=True)


if __name__ == '__main__':
    main()
