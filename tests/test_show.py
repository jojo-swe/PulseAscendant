"""Acceptance: all acts, supplied audio samples, musical edits and export bounds agree."""
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',required=True,type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    info=json.loads(subprocess.check_output([str(args.app.resolve()),'--show-info']))
    assert info['title']=='PULSE ASCENDANT'
    assert info['duration']==888.68 and info['act2_available'] and info['act3_available']
    assert len(info['acts'])==info['planned_acts']==3
    assert [a['frames'] for a in info['acts']]==[14_400_000,13_916_160,14_340_480]
    assert [a['start'] for a in info['acts']]==[0,300,589.92]
    assert len(info['chapters'])==76 and {c['scene'] for c in info['chapters']}==set(range(16))
    for number,expected in enumerate((31,21,24),1):
        chapters=[c for c in info['chapters'] if c['act']==number]
        assert len(chapters)==expected and len({c['title'] for c in chapters})==expected
        edges=[c['start'] for c in chapters]+[info['acts'][number-1]['start']+info['acts'][number-1]['duration']]
        assert all(5-1e-8<=b-a<=18+1e-8 for a,b in zip(edges,edges[1:]))
    for number in range(1,4):
        receipt=json.loads((root/'music'/('asset-receipt.json' if number==1 else f'asset-receipt-act{number}.json')).read_text())
        assert hashlib.sha256((root/receipt['source_file']).read_bytes()).hexdigest()==receipt['source_sha256']
        assert hashlib.sha256((root/receipt['runtime_file']).read_bytes()).hexdigest()==receipt['runtime_sha256']
    original=root/'assets/music/luminous-flow.wav'
    if original.exists():
        with wave.open(str(root/'assets/music/luminous-flow-act1.wav')) as audio,wave.open(str(original)) as source:
            for second in range(299):
                assert audio.readframes(48000)==source.readframes(48000),second
        print('299-second prefix: sample-identical to preserved original WAV')
    with tempfile.TemporaryDirectory(prefix='pulse-export-bounds-') as directory:
        output=Path(directory)/'invalid.mp4'
        result=subprocess.run([sys.executable,str(root/'tools/render_video.py'),'--app',str(args.app.resolve()),
                               '--output',str(output),'--start','888','--duration','2'],capture_output=True,text=True)
        assert result.returncode!=0 and '888.68 seconds' in result.stderr,result.stderr
        assert not output.exists() and not output.with_suffix('.rendering.mp4').exists()
    receipt=json.loads((root/'music/show-audio.json').read_text())
    assert hashlib.sha256((root/receipt['runtime_file']).read_bytes()).hexdigest()==receipt['sha256']
    with wave.open(str(root/'assets'/info['music'])) as combined:
        assert combined.getparams()[:3]==(2,2,48000) and combined.getnframes()==42_656_640
        for stem in ('luminous-flow-act1','adventurous-uplift','rigid-arpeggios'):
            with wave.open(str(root/f'assets/music/{stem}.wav')) as track:
                while chunk:=track.readframes(48000):
                    assert combined.readframes(len(chunk)//4)==chunk
        assert combined.readframes(1)==b''
    direction=json.loads((root/'music/direction.json').read_text())
    assert len(direction['acts'])==3
    for act in direction['acts']:
        # Every cut sits on a tracked beat; the beats chosen carry clearly stronger
        # attacks (+/-50 ms) than the editorial targets they replace.
        assert act['on_beat']==len(act['chapters'])-1,act['on_beat']
        assert act['mean_attack']>act['nominal_mean_attack']*1.5
    print('THREE ACTS PASS: 888.68 s, 76 chapters, 16 worlds, original source hashes, complete tracks II/III, sample-exact joins and bounded export')

if __name__=='__main__':
    main()
