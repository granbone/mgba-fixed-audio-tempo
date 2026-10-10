"""Serial hidden/muted actual frontend tests, after the headless matrix.
SPDX-License-Identifier: MPL-2.0. No automatic playback or foreign process changes.
"""
import argparse,json,subprocess,sys,time
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--after-matrix',type=Path);a=p.parse_args();r=Path.cwd();d=a.output.resolve();d.mkdir(parents=True,exist_ok=False)
    if a.after_matrix:
        begin=time.monotonic();print('Waiting for the bounded 72-run matrix; real-time tests run without it',flush=True)
        while time.monotonic()-begin<1200:
            try:matrix=json.loads(a.after_matrix.read_text());ready=len(matrix['runs'])==72 and all(x['returncode']==0 for x in matrix['runs'])
            except (OSError,ValueError):ready=False
            if ready:break
            time.sleep(2)
        else:raise TimeoutError('Headless matrix did not finish; no frontend test started')
    core=r/'build-phase6/final/runtime/mgba_fixed_audio_libretro.dll';bridge=core.with_name('libmgba_mp2k_bridge.dll');scenes=json.loads((r/'build-phase3/scenes.json').read_text());native=json.loads((r/'build-phase8/native-checked/results.json').read_text());jobs=[]
    for code in ('AFXJ','AFEJ','AAKJ','A8CJ','AORJ','B6JJ'):
        if code in scenes:rom,state=scenes[code]['rom'],scenes[code]['state']
        else:
            row=next(x for x in native['rows'] if x['game']==code);rom,state=row['command'][1:3]
        jobs.append((code+'-switch-1313',rom,state,'1,3,1,3','experimental',False))
    fixture=r/'build-phase4/fixture-AORJ-before-SE/saved-frame-1000-slot-0.rawstate'
    for n in range(2):jobs.append((f'AORJ-2x-repeat-{n}',scenes['AORJ']['rom'],fixture,'2','experimental',True))
    jobs.append(('AORJ-native-1x',scenes['AORJ']['rom'],fixture,'1','disabled',True))
    rows=[]
    for name,rom,state,schedule,mode,single in jobs:
        # Serial sessions may reuse these tested ports; every test checks both
        # bindings before launching, and owns only its own frontend process.
        args=[sys.executable,str(r/'tools/public-integration/test_phase3_audible.py'),'--core',str(core),'--bridge',str(bridge),
            '--rom',str(rom),'--state',str(state),'--output',str(d/name),'--schedule',schedule,'--segment-seconds','8',
            '--mode',mode,'--port','56583','--muted','--incremental-log']
        if single:args.append('--single-se')
        with (d/(name+'.log')).open('xb') as log:proc=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,timeout=85)
        rows.append(dict(case=name,returncode=proc.returncode,command=args));print(name,proc.returncode,flush=True)
        (d/'jobs.json').write_text(json.dumps(rows,indent=2)+'\n');assert proc.returncode==0,name

if __name__=='__main__':main()
