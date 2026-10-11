"""Recreate all three acts from preserved, hash-checked source MP3s. Requires ffmpeg and numpy."""
import hashlib
import json
import shutil
import subprocess
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [('Luminous Flow', 'fd8c915c3d7a52cb3175b3ce84bceadf99405e6ea2efa675651838161a5adff3', '1f52e6e8-5a07-45da-9c9b-09753a609eb3'),
           ('Adventurous Uplift', 'f82793aa129acfd5d9d8efbf15e67b08647666c09b615a38b99d4063855028d0', '29b80971-2988-41e1-b658-00291a06b4ae'),
           ('Rigid Arpeggios', 'eab637ad1cd987632b1f76a6209c901a1fb2dcda683bcb6d4e4c00761349255d', '2a49b46a-28b5-40f9-b6e8-195ba877380a')]
STEMS = ('luminous-flow-act1', 'adventurous-uplift', 'rigid-arpeggios')
FRAMES = (14_400_000, 13_916_160, 14_340_480)


def main():
    for index, (title, digest, suno) in enumerate(SOURCES):
        destination = ROOT / 'music/source' / f'{title}.mp3'
        source = Path(sys.argv[index+1]).resolve() if len(sys.argv) > index+1 else destination
        assert hashlib.sha256(source.read_bytes()).hexdigest() == digest, f'{title}: source digest mismatch'
        destination.parent.mkdir(parents=True, exist_ok=True)
        if source != destination:
            shutil.copy2(source, destination)
        stem = STEMS[index]
        runtime = ROOT / 'assets/music' / f'{stem}.wav'
        runtime.parent.mkdir(parents=True, exist_ok=True)
        filters = ['-af', 'atrim=end=300,afade=t=out:st=299:d=1:curve=qsin'] if index == 0 else []
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(source), '-map', '0:a:0',
                        '-ar', '48000', '-ac', '2', *filters, '-c:a', 'pcm_s16le', str(runtime)], check=True)
        analysis_name = 'analysis.json' if index == 0 else f'analysis-act{index+1}.json'
        subprocess.run([sys.executable, str(ROOT / 'tools/analyze_music.py'), str(runtime),
                        str(ROOT / 'src' / ('music_data.hpp' if index == 0 else f'music_data_act{index+1}.hpp')),
                        str(ROOT / 'music' / analysis_name), 'music_data' if index == 0 else f'music_data_act{index+1}'], check=True)
        analysis = json.loads((ROOT / 'music' / analysis_name).read_text())
        assert analysis['frames'] == FRAMES[index]
        receipt = dict(title=title, act=index+1, source_sha256=digest, source_file=f'music/source/{title}.mp3',
                       source_duration=472.36 if index == 0 else FRAMES[index]/48000, suno_id=suno, runtime_file=f'assets/music/{stem}.wav',
                       excerpt_start=0, excerpt_duration=analysis['duration'], runtime_frames=analysis['frames'], sample_rate=48000,
                       channels=2, encoding='PCM signed 16 bit', fades='equal-power fade out 299-300 s' if index == 0 else 'none; complete supplied song',
                       runtime_sha256=analysis['runtime_sha256'], listening_acceptance='pending',
                       publication_requested_by_project_owner=True, public_release_policy='Free non-commercial demo; Suno attribution retained; music excluded from MIT')
        (ROOT / 'music' / ('asset-receipt.json' if index == 0 else f'asset-receipt-act{index+1}.json')).write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    combined = ROOT / 'assets/music/second-reality-three-acts.wav'
    with wave.open(str(combined), 'wb') as output:
        output.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
        for stem in STEMS:
            with wave.open(str(ROOT / f'assets/music/{stem}.wav'), 'rb') as audio:
                while data := audio.readframes(48000):
                    output.writeframesraw(data)
    (ROOT / 'music/show-audio.json').write_text(json.dumps(dict(frames=sum(FRAMES), duration=sum(FRAMES)/48000,
                    act2_start_frame=FRAMES[0], act3_start_frame=sum(FRAMES[:2]),
                    sha256=hashlib.sha256(combined.read_bytes()).hexdigest(), runtime_file='assets/music/second-reality-three-acts.wav'), indent=2)+'\n', encoding='utf-8')
    subprocess.run([sys.executable, str(ROOT / 'tools/direct_music.py')], check=True)


if __name__ == '__main__':
    main()
