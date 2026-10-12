"""Render a 72s 720p60 HEVC camera/act-transition preview from the native demo."""
import argparse
import json
import subprocess
import sys
import wave
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    app, out = args.app.resolve(), args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    show = json.loads(subprocess.check_output([str(app), '--show-info']))
    clips = [dict(start=t, duration=8, file=f'camera-{i+1}.mp4', label=label)
             for i, (t, label) in enumerate([
                 (48.5, 'Forward flight'), (68.5, 'Reactor arc'), (168.5, 'Carrier tracking'),
                 (396.5, 'RT foundry dolly'), (412.5, 'RT prism crane'),
                 (648, 'Sanctuary fly-through'), (710, 'Seed flight'),
                 (296, 'Act I to II'), (585.92, 'Act II to III')])]
    movie = out/'PulseAscendant-v07-Camera-Preview-720p60-HEVC.mp4'
    if movie.exists() or any((out/c['file']).exists() for c in clips):
        parser.error('Preview files already exist; choose a new output directory')
    (out/'manifest.json').write_text(json.dumps(dict(version=show['version'], clips=clips), indent=2)+'\n')
    for clip in clips:
        print(clip['label'], flush=True)
        subprocess.run([sys.executable, str(root/'tools/render_video.py'), '--app', str(app),
                        '--output', str(out/clip['file']), '--start', str(clip['start']),
                        '--duration', str(clip['duration']), '--width', '1280', '--height', '720',
                        '--fps', '60', '--require-rt'], check=True)
    pcm = out/'camera-preview.wav'
    with wave.open(str(app.parent/'assets'/show['music']), 'rb') as source, wave.open(str(pcm), 'wb') as destination:
        destination.setparams(source.getparams())
        for clip in clips:
            source.setpos(round(clip['start']*48000))
            data = source.readframes(round(clip['duration']*48000))
            assert len(data) == round(clip['duration']*48000)*4
            destination.writeframesraw(data)
    listing = out/'camera-clips.txt'
    listing.write_text(''.join(f"file '{c['file']}'\nduration {c['duration']}\n" for c in clips))
    # Reset every frame PTS and encode PCM once to avoid AAC priming accumulating
    # at the montage edits. Native clips keep high-quality H.264 intermediates.
    subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', '-n', '-f', 'concat', '-safe', '0', '-i', str(listing),
                    '-i', str(pcm), '-map', '0:v:0', '-map', '1:a:0',
                    '-vf', 'setpts=N/(60*TB),format=p010le', '-r', '60', '-fps_mode', 'cfr',
                    '-c:v', 'hevc_nvenc', '-preset', 'p7', '-tune', 'hq', '-profile:v', 'main10',
                    '-tag:v', 'hvc1', '-rc', 'vbr', '-cq', '23', '-b:v', '0', '-maxrate', '4M', '-bufsize', '8M',
                    '-multipass', 'fullres', '-spatial-aq', '1', '-temporal-aq', '1', '-g', '120',
                    '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709', '-color_range', 'tv',
                    '-frames:v', '4320', '-af', 'atrim=duration=72', '-c:a', 'aac', '-b:a', '320k', '-ar', '48000',
                    '-movflags', '+faststart', '-metadata', 'title=Pulse Ascendant v0.7 camera preview', str(movie)], check=True)
    probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(movie)]))
    video = next(s for s in probe['streams'] if s['codec_type'] == 'video')
    audio = next(s for s in probe['streams'] if s['codec_type'] == 'audio')
    assert video['codec_name'] == 'hevc' and int(video['nb_frames']) == 4320
    assert (video['width'], video['height'], video['avg_frame_rate']) == (1280, 720, '60/1')
    assert abs(float(video['start_time'])) < .001 and abs(float(audio['start_time'])) < .001
    assert abs(float(audio['duration'])-72) < .01
    (out/'camera-preview-probe.json').write_text(json.dumps(probe, indent=2)+'\n')
    print(f'CAMERA PREVIEW PASS: {movie}, 4320 frames, 72s', flush=True)


if __name__ == '__main__':
    main()
