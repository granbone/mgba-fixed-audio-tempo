"""Controlled AORJ repeats with initial-state/RAM and callback diagnostics.
SPDX-License-Identifier: MPL-2.0. Diagnostic bridge policies never disable guards.
"""
import argparse,json,os,subprocess,hashlib,re,time
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from diagnose_phase2_pcm import compare_pcm
from test_three_x_poc import MARKERS,sha

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('runner','baseline','core','bridge','rom','controls','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--frames',type=int,default=4800);p.add_argument('--repeats',type=int,default=2)
    p.add_argument('--policies',default='ADDRESS');p.add_argument('--initial-state',type=Path)
    p.add_argument('--baseline-bridge',type=Path,help='Original distribution bridge, for a separately labelled package comparison')
    p.add_argument('--no-initial-snapshot',action='store_true',help='Reproduce the original runner allocation pattern; starting state hashes will be unavailable')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    assert sha(a.rom)=='0E9997636409C47734895EA2180521FA7AD8898D7F45E01634C9E207039D7808'
    inputs={str(f):sha(f) for f in (a.rom,a.controls,a.bridge,a.baseline,a.core,a.runner)}
    if a.initial_state:inputs[str(a.initial_state)]=sha(a.initial_state)
    if a.baseline_bridge:inputs[str(a.baseline_bridge)]=sha(a.baseline_bridge)
    jobs=[(policy,label,speed,i) for policy in a.policies.split(',') for label in ('v03','poc') for speed in (1,2) for i in range(a.repeats)]
    def run(job):
        policy,label,speed,i=job;d=(a.output/f'{policy}-{label}-{speed}-{i}').resolve();d.mkdir()
        actions=d/'actions.txt';actions.write_text(f'0 SPEED {speed}\n')
        env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
        env.update(LIBRETRO_SYSTEM_DIRECTORY=str(d),MGBA_RUNNER_FIXED_AUDIO_MODE='experimental',MGBA_RUNNER_CALLBACK_PATH=str(d/'callback.s16le'),
            MGBA_FIXED_AUDIO_RUN_TRACE='1',MGBA_FIXED_AUDIO_DIAGNOSTICS='1',MGBA_RUNNER_STATE_ACTIONS=str(actions),
            MGBA_MP2K_BRIDGE_PATH=str(a.baseline_bridge if label=='v03' and a.baseline_bridge else a.bridge),MGBA_RUNNER_CALLBACK_INDEX='1',MGBA_RUNNER_INITIAL_SNAPSHOT=str(d/'initial'),
            MGBA_RUNNER_RAM_AT='0,1000,1030,4060',MGBA_PHASE2_PSG_ORDER=policy,MGBA_PHASE2_PSG_TRACE=str(d/'psg.log'),
            MGBA_MP2K_STARTUP_TRACE_PATH=str(d/'startup.log'))
        if a.initial_state:env['MGBA_RUNNER_INITIAL_STATE']=str(a.initial_state)
        if a.no_initial_snapshot:env.pop('MGBA_RUNNER_INITIAL_SNAPSHOT',None)
        with (d/'process.log').open('wb') as log:
            result=subprocess.run([str(a.runner),str(a.baseline if label=='v03' else a.core),str(a.rom),str(d/'run'),'normal',str(a.frames),str(a.controls)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=120)
        text=(d/'run.log').read_text(errors='replace');events=[s for s in text.splitlines() if any(m in s for m in MARKERS)]
        row=dict(case=d.name,policy=policy,core=label,speed=speed,repeat=i,returncode=result.returncode,
            bridge_sha256=sha(a.baseline_bridge if label=='v03' and a.baseline_bridge else a.bridge),
            callback_sha256=sha(d/'callback.s16le'),event_sha256=hashlib.sha256(('\n'.join(events)+'\n').encode()).hexdigest().upper(),
            initial_state_sha256=sha(d/'initial.rawstate') if (d/'initial.rawstate').exists() else None,
            initial_ram_sha256=sha(d/'initial.ram') if (d/'initial.ram').exists() else None,
            ram_hashes={f.name:sha(f) for f in d.glob('run-*.ram')},
            fallback=re.findall(r'(?:SAFE FALLBACK reason=|NATIVE_FALLBACK reason=)([^\n]+)',text),
            final=next((s for s in reversed(text.splitlines()) if '[FIXED AUDIO] end ' in s),''))
        print(json.dumps(row),flush=True);return row
    with ThreadPoolExecutor(max_workers=2) as pool:rows=list(pool.map(run,jobs))
    comparisons=[]
    for policy in a.policies.split(','):
        for speed in (1,2):
            names=[r['case'] for r in rows if r['policy']==policy and r['speed']==speed]
            reference=a.output/names[0]
            for name in names[1:]:comparisons.append(compare_pcm(reference,a.output/name))
    assert all(sha(Path(f))==h for f,h in inputs.items())
    report=dict(inputs=inputs,records=rows,comparisons=comparisons,method='Identical input, rate, callback acceptance, diagnostic settings. Isolated output. No physical audio.')
    (a.output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    assert all(r['returncode']==0 for r in rows)
if __name__=='__main__':main()
