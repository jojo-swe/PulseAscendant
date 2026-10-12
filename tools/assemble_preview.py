"""Mux a native preview montage with sample-exact excerpts of its source WAV.

Rebuilding audio once avoids the AAC priming offset introduced when concatenating
individually encoded clips. Video packets remain unchanged; normalize their PTS
origin to zero before muxing the single continuous audio encode.
"""
import argparse
import json
from pathlib import Path
import subprocess
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--video', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--wav', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output already exists; use a new filename')
    clips = json.loads(args.manifest.read_text())['clips']
    soundtrack = args.output.with_suffix('.wav')
    if soundtrack.exists():
        parser.error('Preview soundtrack already exists; use a new filename')
    with wave.open(str(args.wav), 'rb') as source, wave.open(str(soundtrack), 'wb') as destination:
        if (source.getframerate(), source.getnchannels(), source.getsampwidth()) != (48000, 2, 2):
            parser.error('Expected48kHz stereo PCM16 source')
        destination.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
        for clip in clips:
            source.setpos(round(clip['start']*48000))
            count = round(clip['duration']*48000)
            data = source.readframes(count)
            if len(data) != count*4:
                raise RuntimeError('Preview excerpt exceeds source WAV')
            destination.writeframesraw(data)
    streams = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-of', 'json', str(args.video)]))['streams']
    video = next(s for s in streams if s['codec_type'] == 'video')
    subprocess.run(['ffmpeg', '-v', 'error', '-nostdin', '-itsoffset', str(-float(video.get('start_time', 0))),
                    '-i', str(args.video), '-i', str(soundtrack), '-map', '0:v:0', '-map', '1:a:0',
                    '-c:v', 'copy', '-c:a', 'aac', '-b:a', '320k', '-movflags', '+faststart', str(args.output)], check=True)
    print(f'Preview assembled: {args.output}')


if __name__ == '__main__':
    main()
