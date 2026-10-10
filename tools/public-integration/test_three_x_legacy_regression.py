"""Run existing external golden/recovery tests without copying private history.
SPDX-License-Identifier: MPL-2.0. ROMs/states are supplied locally, read-only.
"""
import argparse, hashlib, json, subprocess, sys, time
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('legacy-tools','runner','core','bridge','baseline','rom-dir','output','afej-state','ffta-state','b6jj-golden'):
        p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    common=['--runner',a.runner,'--core',a.core,'--bridge',a.bridge]
    aamj=a.rom_dir/'Akumajou Dracula - Circle of the Moon (Japan).gba'
    afxj=a.rom_dir/'Final Fantasy Tactics Advance (Japan).gba'
    afej=a.rom_dir/'Fire Emblem - Fuuin no Tsurugi (Japan).gba'
    b6jj=a.rom_dir/'Super Robot Taisen J (Japan).gba'
    jobs={
        'aamj-golden': ['test_aamj_golden.py',*common,'--rom',aamj,'--output-dir',a.output/'aamj'],
        'ffta-golden': ['test_ffta_golden.py',*common,'--baseline',a.baseline,'--rom',afxj,'--output',a.output/'ffta-golden'],
        'ffta-late-load':['test_track_recovery.py',*common,'--rom',afxj,'--state',a.ffta_state,
            '--count','1','--spacing','1300','--output-dir',a.output/'ffta-late-load'],
        'afej-rewind':['test_rewind_recovery.py',*common,'--rom',afej,'--state',a.afej_state,'--output-dir',a.output/'afej-rewind'],
        'hard-guards':['test_state_load_guards.py',*common,'--rom',afej,'--state',a.afej_state,'--output-dir',a.output/'hard-guards'],
        'srwj-golden':['test_srwj_j5_golden.py',*common,'--golden',a.b6jj_golden,'--rom',b6jj,'--output',a.output/'srwj-golden'],
        '14-rom':['test_mp2k_rom_suite.py',*common,'--rom-dir',a.rom_dir,
            '--game-codes','AAMJ,BVGJ,AB2J,A8CJ,BK3J,AQAJ,AFXJ,AFEJ,AE7J,BE8J,ASRJ,A6SJ,B6JJ,AJ9J',
            '--output-dir',a.output/'14-rom','--frames','2400','--all-speeds']}
    for mode in ('normal','ff'):
        jobs['afej-modt127-'+mode]=['test_track_recovery.py',*common,'--rom',afej,'--state',a.afej_state,
            '--count','15','--mode',mode,'--modt127-clone','--output-dir',a.output/('afej-modt127-'+mode)]
    for group in ('loads','rewind','off'):
        jobs['srwj-'+group]=['test_srwj_j5_recovery.py',*common,'--golden',a.baseline,'--rom',b6jj,
            '--output',a.output/('srwj-'+group),'--group',group]
    inputs=[a.afej_state,a.ffta_state,aamj,afxj,afej,b6jj]
    hashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs}
    def run(item):
        name,cmd=item;script=a.legacy_tools/cmd[0];start=time.perf_counter()
        with (a.output/(name+'.log')).open('wb') as log:
            try:r=subprocess.run([sys.executable,str(script),*map(str,cmd[1:])],stdout=log,stderr=subprocess.STDOUT,timeout=1200);rc=r.returncode
            except subprocess.TimeoutExpired:rc='TIMEOUT'
        row=dict(case=name,returncode=rc,seconds=round(time.perf_counter()-start,3),
                 script_sha256=hashlib.sha256(script.read_bytes()).hexdigest())
        print(json.dumps(row),flush=True);return row
    with ThreadPoolExecutor(max_workers=2) as pool:rows=list(pool.map(run,jobs.items()))
    unchanged=all(hashlib.sha256(Path(f).read_bytes()).hexdigest()==h for f,h in hashes.items())
    result=dict(passed=all(r['returncode']==0 for r in rows) and unchanged,cases=rows,input_hashes_unchanged=unchanged)
    (a.output/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    assert result['passed'], 'Inspect individual test logs'

if __name__=='__main__':main()
