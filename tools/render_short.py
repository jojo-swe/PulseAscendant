"""Render an original 1080x1920 trailer using the native demo and measured music attacks."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TIMES = (8.65, 127.55, 354.95, 603.47, 663.32, 626.97, 840.77)


def directed_cuts(hits, music_start, duration, count):
    """Choose strong attacks while keeping every segment within its five-second source."""
    states = [(0., [0.])]
    for section in range(1, count):
        nominal = section * duration / count
        candidates = range(round((music_start+nominal-.4)*20), round((music_start+nominal+.4)*20)+1)
        next_states = []
        for sample in candidates:
            cut = round((sample/20-music_start)*60)/60
            choices = [(score+hits[sample]/255-.12*abs(cut-nominal), cuts+[cut])
                       for score,cuts in states if 3.8 <= cut-cuts[-1] <= 5]
            if choices:
                next_states.append(max(choices, key=lambda choice: choice[0]))
        states = next_states
    choices = [(score,cuts+[duration]) for score,cuts in states if 3.8 <= duration-cuts[-1] <= 5]
    if not choices:
        raise ValueError('No music-directed cut sequence fits the cached native clips')
    return max(choices, key=lambda choice: choice[0])[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--font', required=True, type=Path, help='Bold TrueType/OpenType font')
    args = parser.parse_args()
    app, output, font = args.app.resolve(), args.output.resolve(), args.font.resolve()
    if output.exists() or not font.is_file():
        parser.error('Choose a new output filename and an existing font')
    identity = hashlib.sha256(b'1080x1920@60; five-second native clips; v1')
    sources = [app] + sorted(p for p in (app.parent/'assets').rglob('*') if p.is_file())
    for source in sources:
        identity.update(source.relative_to(app.parent).as_posix().encode())
        with source.open('rb') as stream:
            while block := stream.read(4*1024*1024):
                identity.update(block)
    source_hash = identity.hexdigest()
    work = ROOT / 'build/short-v03' / source_hash
    work.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    partial = output.with_suffix('.rendering.mp4')
    if partial.exists():
        parser.error('A partial output already exists')
    # Use the exact same precomputed onset curve as the playable third act.
    header = (ROOT / 'src/music_data_act3.hpp').read_text()
    values = list(map(int, re.findall(r'\d+', re.search(r'envelope\[\] = \{(.*?)\};', header, re.S)[1])))
    hits = values[2::3]
    music_start, duration = 70., 32.
    cuts = directed_cuts(hits, music_start, duration, len(TIMES))
    assert len(cuts) == len(TIMES)+1
    assert all(3.8 <= b-a <= 5 for a,b in zip(cuts,cuts[1:]))
    clips = []
    for start in TIMES:
        clip = work / f'clip-{round(start*100):06d}.mp4'
        if not clip.exists():
            subprocess.run([sys.executable, str(ROOT/'tools/render_video.py'), '--app', str(app),
                            '--require-rt', '--width', '1080', '--height', '1920', '--start', str(start),
                            '--duration', '5', '--output', str(clip)], check=True)
        probe = json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-of','json',str(clip)]))
        video = next(s for s in probe['streams'] if s['codec_type']=='video')
        assert (video['width'],video['height'],int(video['nb_frames']),video['avg_frame_rate']) == (1080,1920,300,'60/1')
        clips.append(clip)
    # Copy the font beside the filter file to avoid platform-specific path escaping.
    import shutil
    shutil.copyfile(font, work/'trailer-font.ttf')
    filters = []
    for i,(a,b) in enumerate(zip(cuts,cuts[1:])):
        filters.append(f'[{i}:v]trim=duration={b-a:.9f},setpts=PTS-STARTPTS[v{i}]')
    filters.append(''.join(f'[v{i}]' for i in range(len(TIMES)))+f'concat=n={len(TIMES)}:v=1:a=0[montage]')
    captions = [('16 WORLDS','ONE PULSE',cuts[0],cuts[2]),('HARDWARE RAY TRACING','MUSIC DRIVES THE MOTION',cuts[2],cuts[4]),
                ('THREE ACTS','14 MINUTES OF EXPLORATION',cuts[4],cuts[6]),('PULSE ASCENDANT','RUN IT. GET THE SOURCE.',cuts[6],cuts[7])]
    overlay = "drawbox=x=0:y=120:w=iw:h=220:color=black@0.55:t=fill"
    for title,subtitle,a,b in captions:
        enable = f"between(t,{a},{b-.001})"
        overlay += f",drawtext=fontfile=trailer-font.ttf:text='{title}':fontsize=60:fontcolor=white:x=(w-tw)/2:y=160:enable='{enable}'"
        overlay += f",drawtext=fontfile=trailer-font.ttf:text='{subtitle}':fontsize=36:fontcolor=0x58e6ff:x=(w-tw)/2:y=244:enable='{enable}'"
    overlay += ",drawbox=x=0:y=1540:w=iw:h=110:color=black@0.55:t=fill,drawtext=fontfile=trailer-font.ttf:text='WINDOWS + LINUX  /  SOURCE ON GITHUB':fontsize=32:fontcolor=white:x=(w-tw)/2:y=1580"
    filters.append('[montage]'+overlay+'[video]')
    filters.append(f'[{len(TIMES)}:a]atrim=duration={duration},asetpts=PTS-STARTPTS,afade=t=out:st=31.65:d=0.35[audio]')
    filter_file = work/'trailer-filter.txt'
    filter_file.write_text(';\n'.join(filters),encoding='utf-8')
    command = ['ffmpeg','-hide_banner','-nostdin','-n']
    for clip in clips:
        command += ['-i',str(clip)]
    command += ['-ss',str(music_start),'-i',str(ROOT/'assets/music/rigid-arpeggios.wav'),
                '-filter_complex_script',filter_file.name,'-map','[video]','-map','[audio]',
                '-c:v','h264_nvenc','-preset','p6','-cq','18','-b:v','16M','-maxrate','24M','-bufsize','48M',
                '-pix_fmt','yuv420p','-r','60','-colorspace','bt709','-color_primaries','bt709',
                '-color_trc','bt709','-color_range','tv','-c:a','aac','-b:a','320k','-ar','48000',
                '-t',str(duration),'-movflags','+faststart','-metadata','title=PULSE ASCENDANT / 16 worlds. One pulse.',
                '-metadata','artist=jojo-swe; music: Rigid Arpeggios / joyolo (Suno)',str(partial)]
    with (work/'assembly.log').open('w',encoding='utf-8') as log:
        subprocess.run(command,cwd=work,stdout=log,stderr=subprocess.STDOUT,check=True)
    probe = json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(partial)]))
    video = next(s for s in probe['streams'] if s['codec_type']=='video')
    audio = next(s for s in probe['streams'] if s['codec_type']=='audio')
    assert (video['width'],video['height'],int(video['nb_frames']),video['avg_frame_rate']) == (1080,1920,1920,'60/1')
    assert abs(float(audio['duration'])-duration)<.05
    partial.rename(output)
    manifest = dict(title='PULSE ASCENDANT',trailer_revision=2,source_hash=source_hash,duration=duration,fps=60,music='Rigid Arpeggios / joyolo (Suno)',
                    music_start=music_start,cuts=cuts,source_times=TIMES,probe=probe)
    output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(f'SHORT PASS: {output}, 1080x1920, 60 fps, 1920 frames, 32 seconds',flush=True)


if __name__ == '__main__':
    main()
