"""Bounded BGM-only loaded-state comparisons for the three additional titles.
SPDX-License-Identifier: MPL-2.0. No new WAV and no audio replay.
"""
import argparse,json,subprocess,sys
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=Path.cwd();d=a.output.resolve();d.mkdir(parents=True,exist_ok=False)
    cases=json.loads((r/'build-phase8/native-checked/results.json').read_text())['rows'];rows=[]
    for code in ('AFEJ','AAKJ','A8CJ'):
        case=next(x for x in cases if x['game']==code);rom,state=case['command'][1:3];folder=d/code
        args=[sys.executable,str(r/'tools/public-integration/test_phase2_scene.py'),'--runner',str(r/'build-phase6/retro-runner.exe'),
            '--core',str(r/'build-phase6/final/runtime/mgba_fixed_audio_libretro.dll'),'--bridge',str(r/'build-phase6/final/runtime/libmgba_mp2k_bridge.dll'),
            '--rom',rom,'--state',state,'--output',str(folder),'--seconds','12']
        with (d/(code+'.log')).open('xb') as log:proc=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,timeout=180)
        assert proc.returncode==0
        args=[sys.executable,str(r/'tools/public-integration/analyze_phase2_scene.py'),str(folder),'--output',str(d/(code+'-bgm.json'))]
        with (d/(code+'-analysis.log')).open('xb') as log:proc=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,timeout=60)
        assert proc.returncode==0
        rows.append(dict(game=code,scene=json.loads((folder/'results.json').read_text()),analysis=json.loads((d/(code+'-bgm.json')).read_text()),
            scope='No-input12s BGM scene; three available unretimed2s windows (2/5/8s). No SFX or whole-game quality certification.'))
        (d/'results.json').write_text(json.dumps(dict(rows=rows,new_wavs=0),indent=2)+'\n');print(code,'BGM measured',flush=True)

if __name__=='__main__':main()
