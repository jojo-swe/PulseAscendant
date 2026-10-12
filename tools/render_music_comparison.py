"""Short exact-time before/after preview. Never starts a full-show render.

Supply the immutable v0.5 movie as --baseline. Both sides use identical music
excerpts, with one PCM assembly and one AAC encode to prevent concat drift.
"""
import argparse
import json
import subprocess
import sys
import wave
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--assemble-only', action='store_true', help='Reuse validated native clips; create new final outputs')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    app = args.app.resolve()
    show = json.loads(subprocess.check_output([str(app), '--show-info'], text=True))
    clips = [dict(start=t, duration=6, file=f'clip-{i+1}.mp4', label=label)
             for i, (t, label) in enumerate([
                 (48.5, 'ACT I / HYPERDRIVE'), (68.5, 'ACT I / REACTOR'),
                 (168.5, 'ACT I / CARRIER'), (396.5, 'ACT II / FOUNDRY'),
                 (412.5, 'ACT II / PRISM'), (648., 'ACT III / CANYON'),
                 (710., 'ACT III / SEED')])]
    (out/'manifest.json').write_text(json.dumps(dict(version=show['version'], clips=clips), indent=2))
    for clip in clips:
        target = out/clip['file']
        if args.assemble_only:
            probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                               '-show_streams', '-of', 'json', str(target)]))['streams'][0]
            assert int(probe['nb_frames']) == round(clip['duration']*60)
            continue
        if target.exists():
            raise RuntimeError(f'Refusing to replace previous output: {target}')
        print(clip['label'], flush=True)
        subprocess.run([sys.executable, str(root/'tools/render_video.py'), '--app', str(app),
                        '--output', str(target), '--start', str(clip['start']),
                        '--duration', str(clip['duration']), '--width', '1920', '--height', '1080',
                        '--fps', '60', '--require-rt'], check=True)
    pcm = out/'preview.wav'
    with wave.open(str(app.parent/'assets'/show['music']), 'rb') as source, wave.open(str(pcm), 'wb') as dest:
        dest.setparams(source.getparams())
        for clip in clips:
            source.setpos(round(clip['start']*source.getframerate()))
            dest.writeframes(source.readframes(round(clip['duration']*source.getframerate())))
    concat = out/'clips.txt'
    concat.write_text(''.join(f"file '{c['file']}'\nduration {c['duration']}\n" for c in clips))
    movie = out/'PulseAscendant-v06-Music-Preview-1080p60.mp4'
    subprocess.run(['ffmpeg', '-hide_banner', '-nostdin', '-n', '-f', 'concat', '-safe', '0', '-i', str(concat),
                    '-i', str(pcm), '-map', '0:v:0', '-map', '1:a:0', '-c:v', 'copy', '-c:a', 'aac',
                    '-b:a', '320k', '-movflags', '+faststart', str(movie)], check=True)
    baseline = []
    for i, clip in enumerate(clips):
        target = out/f'before-{i+1}.mp4'
        if args.assemble_only:
            probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                               '-show_streams', '-of', 'json', str(target)]))['streams'][0]
            assert int(probe['nb_frames']) == round(clip['duration']*60)
            baseline.append(target)
            continue
        subprocess.run(['ffmpeg', '-hide_banner', '-nostdin', '-n', '-ss', str(clip['start']),
                        '-i', str(args.baseline.resolve()), '-t', str(clip['duration']), '-an',
                        '-c:v', 'h264_nvenc', '-preset', 'p5', '-cq', '18', '-b:v', '0',
                        '-vf', 'scale=1920:1080', '-r', '60', str(target)], check=True)
        baseline.append(target)
    before_list = out/'before.txt'
    before_list.write_text(''.join(f"file '{p.name}'\n" for p in baseline))
    graph = ("[0:v]setpts=N/(60*TB),scale=960:540,pad=960:600:0:60:color=0x080d16,"
             r"drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='v0.5 / BEFORE':x=26:y=17:fontsize=26:fontcolor=white[left];"
             "[1:v]setpts=N/(60*TB),scale=960:540,pad=960:600:0:60:color=0x080d16,"
             r"drawtext=fontfile='C\:/Windows/Fonts/arial.ttf':text='v0.6 / MUSIC-DRIVEN':x=26:y=17:fontsize=26:fontcolor=0x54ddff[right];"
             "[left][right]hstack=inputs=2[v]")
    comparison = out/'PulseAscendant-v06-Before-After-60fps.mp4'
    subprocess.run(['ffmpeg', '-hide_banner', '-nostdin', '-n', '-f', 'concat', '-safe', '0', '-i', str(before_list),
                    '-i', str(movie), '-filter_complex', graph, '-map', '[v]', '-map', '1:a:0',
                    '-c:v', 'h264_nvenc', '-preset', 'p6', '-cq', '18', '-b:v', '0', '-c:a', 'copy',
                    '-r', '60', '-fps_mode', 'cfr', '-frames:v', str(sum(round(c['duration']*60) for c in clips)),
                    '-movflags', '+faststart', str(comparison)], check=True)
    print(f'PREVIEW: {movie}\nCOMPARISON: {comparison}', flush=True)


if __name__ == '__main__':
    main()
