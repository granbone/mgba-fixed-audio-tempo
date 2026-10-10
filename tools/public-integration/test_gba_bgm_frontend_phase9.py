"""Serial, hidden/muted real RetroArch survey using direct GBA frame counters.
SPDX-License-Identifier: MPL-2.0. No audible playback or new PCM capture.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

from test_three_x_poc import FPS, sha

FRAME = re.compile(r'\[GBA FRAME PROBE\] runs=(\d+) us=(\d+) counter=(\d+) '
                   r'emulatedFrames=(\d+) stepAnomalies=(\d+) callbackRequested=(\d+) callbackAccepted=(\d+)')


def read(path):
    return json.loads(path.read_text(encoding='utf8'))


def measure(folder):
    """Align direct frame samples to the same logged run/time pair, never UI FPS."""
    report=read(folder/'results.json')
    points={int(x[0]):list(map(int,x)) for x in FRAME.findall((folder/'frontend.log').read_text(errors='replace'))}
    rows=[]
    for segment in report['segments']:
        q=[points[o[1]] for o in segment['probes'] if o[1] in points and points[o[1]][1]==o[2]]
        row=dict(target_speed=segment['target_speed'],begin=segment['begin'],end=segment['end'],
                 samples=q,steady_fixed=segment['all_steady_probes_fixed'])
        if len(q)>=2:
            a,b=q[0],q[-1];dt=(b[1]-a[1])/1e6
            row.update(measurement_seconds=dt,internal_frame_delta=b[3]-a[3],
                       frame_counter_delta_mod32=(b[2]-a[2])&0xffffffff,
                       gba_frames_per_second=(b[3]-a[3])/dt,
                       internal_speed_x=(b[3]-a[3])/dt/FPS,
                       step_anomalies=b[4]-a[4],
                       callback_requested_samples_per_second=(b[5]-a[5])/dt,
                       callback_accepted_samples_per_second=(b[6]-a[6])/dt,
                       callback_partial_samples=(b[5]-a[5])-(b[6]-a[6]))
        rows.append(row)
    return dict(segments=rows,frontend=report,method='core->frameCounter() immediately before/after each actual core->runFrame(); cumulative uint32 deltas exclude pre-run Load/Rewind counter rollback. Same monotonic timestamps as run probe. Callback frames are core submissions/acceptances, not Windows device output.')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,required=True)
    p.add_argument('--fixtures',type=Path,required=True)
    p.add_argument('--core',type=Path,required=True)
    p.add_argument('--bridge',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--codes')
    p.add_argument('--schedule',default='1,3')
    p.add_argument('--seconds',type=float,default=10)
    p.add_argument('--recovery',action='store_true')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    cases=read(a.manifest)['rows'];fixtures=read(a.fixtures)['rows']
    if a.codes:
        codes=set(a.codes.split(','));cases=[r for r in cases if r['game_code'] in codes]
        assert {r['game_code'] for r in cases}==codes
    result=dict(core_sha256=sha(a.core),bridge_sha256=sha(a.bridge),rows=[],
                physical_output='MUTED; HUMAN_REVIEW_REQUIRED',serial_execution=True)
    for case in cases:
        code=case['game_code'];state=next(r['state'] for r in fixtures if r['game']==code)
        folder=a.output/code
        command=[sys.executable,str(Path(__file__).with_name('test_phase3_audible.py')),
                 '--core',str(a.core.resolve()),'--bridge',str(a.bridge.resolve()),'--rom',case['rom'],
                 '--state',state,'--output',str(folder.resolve()),'--schedule',a.schedule,
                 '--segment-seconds',str(a.seconds),'--muted','--incremental-log','--no-pcm-capture']
        if a.recovery:command.append('--recovery')
        with (a.output/(code+'.log')).open('xb') as log:
            process=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=len(a.schedule.split(','))*a.seconds+50)
        row=dict(game=code,rom_sha256=case['sha256'],returncode=process.returncode,command=command)
        if (folder/'results.json').exists():row['measurement']=measure(folder)
        result['rows'].append(row)
        (a.output/'results.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
        print(code,'rc',process.returncode,[(s['target_speed'],round(s.get('internal_speed_x',0),5)) for s in row.get('measurement',{}).get('segments',[])],flush=True)
        assert process.returncode==0,'Keep failed run; do not replace its evidence'


if __name__=='__main__':main()
