"""Eight-second callback probes across 1->2->3->1, without gameplay input.
SPDX-License-Identifier: MPL-2.0. No playback or golden update.
"""
import argparse
import json
import os
import re
import subprocess
from pathlib import Path

import numpy as np

from analyze_phase2_scene import windows
from test_three_x_poc import clock_span_error, sha


def read(p):return json.loads(p.read_text(encoding='utf8'))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('manifest','fixtures','analysis','core','bridge','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--runner',type=Path,default=Path('build-phase6/retro-runner.exe'))
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    cases=read(a.manifest)['rows'];fixtures=read(a.fixtures)['rows'];reference={r['identity']['game_code']:r for r in read(a.analysis)['rows']};rows=[]
    for case in cases:
        code=case['game_code'];d=(a.output/code).resolve();d.mkdir();state=Path(next(r['state'] for r in fixtures if r['game']==code));(d/'controls.txt').write_text('')
        (d/'actions.txt').write_text('0 SPEED 1\n120 SPEED 2\n360 SPEED 3\n720 SPEED 1\n')
        protected={str(f):sha(f) for f in (a.core,a.bridge,Path(case['rom']),state)}
        env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
        env.update(LIBRETRO_SYSTEM_DIRECTORY=str(d),MGBA_RUNNER_FIXED_AUDIO_MODE='experimental',
            MGBA_MP2K_BRIDGE_PATH=str(a.bridge.resolve()),MGBA_RUNNER_INITIAL_STATE=str(state),
            MGBA_RUNNER_CALLBACK_PATH=str(d/'callback.s16le'),MGBA_RUNNER_STATE_ACTIONS=str(d/'actions.txt'),
            MGBA_FIXED_AUDIO_RUN_TRACE='1',MGBA_FIXED_AUDIO_DIAGNOSTICS='1')
        command=[str(a.runner.resolve()),str(a.core.resolve()),case['rom'],str(d/'run'),'normal','840',str(d/'controls.txt')]
        with (d/'process.log').open('xb') as out:r=subprocess.run(command,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=120)
        text=(d/'run.log').read_text(errors='replace');data=np.fromfile(d/'callback.s16le',dtype='<i2').reshape(-1,2).mean(axis=1)/32768
        original=Path(reference[code]['result']['root'])/'audio'/code/'fixed/1/callback.s16le';normal=np.fromfile(original,dtype='<i2').reshape(-1,2).mean(axis=1)/32768
        points=windows(normal,data,32768,starts=(2,4,6));clocks=[tuple(map(float,x)) for x in re.findall(r'\[AUDIO CLOCK\] run=(\d+) rate=([\d.]+) advance=(\d+) sample=(\d+)',text)]
        routing=re.findall(r'\[FIXED AUDIO DIAG\] run=(\d+) transition=run-end fixed_audio_enabled=\d candidate_active=(\d) native_fallback=(\d)',text)
        steady=[(int(active),int(native)) for run,active,native in routing if int(run)>=60]
        fixed=bool(steady) and all(active==1 and native==0 for active,native in steady)
        waveform=len(points)==3 and min(x['correlation'] for x in points)>.97
        row=dict(game=code,returncode=r.returncode,command=command,inputs=protected,
            callback_frames=len(data),callback_sha256=sha(d/'callback.s16le'),unretimed_windows=points,
            waveform_agreement=waveform,steady_fixed_after60runs=fixed,
            audio_continuity_comparison='PASS_SCOPED' if waveform and fixed else 'BGM_NOT_ACTIVE' if not fixed else 'INCONCLUSIVE',
            underrun=max([0]+list(map(int,re.findall(r'underrun_count=(\d+)',text)))),
            overrun=max([0]+list(map(int,re.findall(r'overrun_count=(\d+)',text)))),
            queue_dropped=max([0]+list(map(int,re.findall(r'queue_dropped=(\d+)',text)))),
            steady_clock_max_error_samples=clock_span_error(clocks),
            observed_speed_ratios=sorted(set(round(x[1]/59.727501,3) for x in clocks)),
            fallback_reasons=re.findall(r'(?:SAFE FALLBACK reason=|NATIVE_FALLBACK reason=)([^\n]+)',text),
            recovery_seek_markers=[l for l in text.splitlines() if 'STATE_LOAD_' in l and ('DETECTED' in l or 'RECONSTRUCT' in l or 'RECOVERED' in l)],
            human_listening='HUMAN_REVIEW_REQUIRED')
        assert all(sha(Path(f))==h for f,h in protected.items())
        rows.append(row);(a.output/'results.json').write_text(json.dumps(dict(rows=rows,method='840 actual emulated frames, variable run rate; 1x120 +2x240 +3x360 +1x120 frames, about8s audio. Existing same-fixture Fixed1x compared without tempo/pitch scaling. Unthrottled host is not real speed proof.'),indent=2)+'\n',encoding='utf8')
        print(code,row['audio_continuity_comparison'],flush=True);assert r.returncode==0


if __name__=='__main__':main()
