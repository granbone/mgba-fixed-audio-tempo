"""Isolated 3x host probes and byte-exact v0.3 regressions. SPDX-License-Identifier: MPL-2.0.

Exact ROM hashes, private PCM/state output, and final routing are recorded.
Unthrottled host throughput is headroom, never proof of real frontend 3x.
"""
import argparse, hashlib, json, os, re, subprocess, time
from pathlib import Path
from test_v03_rc import comparisons, cpu_seconds, FPS

MARKERS = ('[MP2K EVENT]', '[MP2K SEMANTIC]', '[MP2K FIRST TICK]',
           '[MP2K FRONTEND]', '[FIXED AUDIO RUN]', '[FIXED AUDIO] end',
           '[B6JJ RUN]', '[B6JJ CLOCK]', '[B6JJ EVENTS]', '[B6JJ BUFFER]')

def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest().upper()

def clock_span_error(clocks, sample_rate=32768):
    """Exclude rate changes and intentional clock seeks/resets from drift."""
    spans=[]
    for start in range(0,len(clocks)-120,120):
        window=clocks[start:start+121];x,y=window[0],window[-1]
        continuous=all(next_[0]==prev[0]+1 and next_[3]-prev[3]==next_[2]
                       for prev,next_ in zip(window,window[1:]))
        if continuous and x[1]>0 and all(c[1]==x[1] for c in window):
            spans.append(abs((y[3]-x[3])-sample_rate*(y[0]-x[0])/x[1]))
    return round(max(spans),4) if spans else None

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('root', 'rom-dir', 'runner', 'core', 'baseline', 'output'):
        p.add_argument('--'+n, type=Path, required=True)
    p.add_argument('--group', choices=['regression', 'three', 'guards'], required=True)
    p.add_argument('--codes', default='AAMJ,AFXJ,AFEJ,B6JJ,AORJ,AAKJ,A8CJ,A2QJ')
    p.add_argument('--frames', type=int, default=6000)
    p.add_argument('--controls',type=Path,help='Read-only explicit gameplay/SE input schedule')
    p.add_argument('--bridge',type=Path,help='Read-only explicit bridge DLL; both baseline and PoC use it')
    p.add_argument('--lifetime-poll-trace',action='store_true',help='Read-only polling player/track evidence; no native hook or STOP policy override')
    a=p.parse_args(); a.output.mkdir(parents=True, exist_ok=False)
    db=json.loads((a.root/'compatibility/gba-compatibility.json').read_text(encoding='utf8'))['records']
    files={}
    for f in a.rom_dir.glob('*.gba'):
        with f.open('rb') as s: h=s.read(0xc0)
        if len(h)==0xc0: files.setdefault(h[0xac:0xb0].decode('ascii', errors='replace'), []).append(f)
    identities={}
    for code in a.codes.split(','):
        for row in sorted((r for r in db if r['game_code']==code and r.get('sha256')),
                          key=lambda r:r['driver_family']=='NOT_ANALYZED'):
            found=next((f for f in files.get(code,[]) if sha(f)==row['sha256']), None)
            if found: identities[code]=(found,row); break
        assert code in identities, 'Exact identity unavailable: '+code
    controls=a.output/'controls.txt'
    controls.write_text(''.join(f'{f} {b} 3\n' for f in range(300,a.frames-100,180) for b in ('START','A')))
    if a.controls:controls=a.controls.resolve()
    base={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
    results=[]; checks=[]
    def run(code, label, speed=1, mode='experimental', commands=(), core=None, extra=None):
        d=a.output/label; d.mkdir(); rom,row=identities[code]
        actions=d/'actions.txt'
        actions.write_text(''.join(f'{f} {op} {v}\n' for f,op,v in ([(0,'SPEED',speed)] + list(commands))))
        env=dict(base, LIBRETRO_SYSTEM_DIRECTORY=str(d), MGBA_RUNNER_FIXED_AUDIO_MODE=mode,
                 MGBA_RUNNER_CALLBACK_PATH=str(d/'callback.s16le'), MGBA_FIXED_AUDIO_RUN_TRACE='1',
                 MGBA_FIXED_AUDIO_DIAGNOSTICS='1', MGBA_RUNNER_STATE_ACTIONS=str(actions),
                 MGBA_MP2K_BRIDGE_PATH=str(a.bridge or a.core.parent/'libmgba_mp2k_bridge.dll'))
        if extra: env.update(extra)
        if a.lifetime_poll_trace: env['MGBA_MP2K_LIFETIME_FINISH_PC']='0'
        start=time.perf_counter()
        with (d/'process.log').open('wb') as log:
            proc=subprocess.Popen([str(a.runner), str(core or a.core), str(rom), str(d/'run'),
                                   'normal',str(a.frames),str(controls)],env=env,stdout=log,stderr=subprocess.STDOUT)
            try: rc=proc.wait(timeout=300)
            except subprocess.TimeoutExpired: proc.kill();proc.wait();rc='TIMEOUT'
            cpu=cpu_seconds(proc)
        wall=time.perf_counter()-start
        text=(d/'run.log').read_text(errors='replace') if (d/'run.log').exists() else ''
        final=next((x for x in reversed(text.splitlines()) if '[FIXED AUDIO] end ' in x), '')
        diag=re.findall(r'candidate_active=(\d)',text)
        b6=code=='B6JJ' and '[B6JJ RUN]' in text
        reasons=re.findall(r'(?:SAFE FALLBACK reason=|NATIVE_FALLBACK reason=)([^\n]+)',text)
        normal_b6={'UNLOAD','STATE_LOAD_OR_REWIND','REWIND_BEGIN'}
        safety=[r for r in reasons if r.split()[0] not in normal_b6]
        clocks=[tuple(map(float,x)) for x in re.findall(r'\[AUDIO CLOCK\] run=(\d+) rate=([\d.]+) advance=(\d+) sample=(\d+)',text)]
        event_lines=[x for x in text.splitlines() if any(m in x for m in MARKERS)]
        event_sha=hashlib.sha256(('\n'.join(event_lines)+'\n').encode()).hexdigest().upper()
        row=dict(label=label,game_code=code,mode=mode,speed=speed,rom_sha256=row['sha256'],
                 driver=row['driver_family'],public_status=row['public_status'],returncode=rc,
                 frames=a.frames,wall_seconds=round(wall,4),cpu_seconds=round(cpu,4) if cpu is not None else None,
                 headroom_speed_x=round(a.frames/FPS/wall,3),fixed_activation=b6 or 'FIXED AUDIO ACTIVE' in text,
                 final_fixed=(diag[-1]=='1') if diag else (b6 and not safety),
                 callback_sha256=sha(d/'callback.s16le'),event_sha256=event_sha,
                 callback_samples=(d/'callback.s16le').stat().st_size//4,
                 final=final,safety_fallback_reasons=safety,
                 underrun=max([int(x) for x in re.findall(r'\bunderrun(?:_count)?=(\d+)',text)] or [0]),
                 overrun=max([int(x) for x in re.findall(r'\boverrun(?:_count)?=(\d+)',text)] or [0]),
                 queue_peak=max([int(x) for x in re.findall(r'(?:queuePeak|queue_size)=(\d+)',text)] or [0]),
                 queue_dropped=max([int(x) for x in re.findall(r'queue_dropped=(\d+)',text)] or [0]),
                 recovery_events=len(re.findall(r'STATE_LOAD_RECOVERED|REWIND_RECOVERED|\[B6JJ RECOVERY\] READY',text)),
                 state_calls=re.findall(r'\[HOST\] state (\w+) slot=(\d+) frame=(\d+) success=(\d)',text),
                 observed_speed_logs=sorted(set(re.findall(r'(?:FIXED AUDIO speed (\d)\.0x|\[B6JJ AUDIO\] speed=(\d))',text))),
                 bgm='HUMAN_REVIEW_REQUIRED',se='HUMAN_REVIEW_REQUIRED')
        # Bounds one contiguous, stable-speed generation; Load/rewind resets are excluded.
        row['clock_120_run_max_error_samples']=clock_span_error(clocks) if not b6 else None
        results.append(row)
        assert sha(rom)==row['rom_sha256'],'ROM changed'
        (a.output/'results.json').write_text(json.dumps(dict(runs=results,checks=checks,real_3x_proven=False),indent=2)+'\n')
        print(label,'rc',rc,'fixed',row['final_fixed'],'under/over',row['underrun'],row['overrun'],'fallback',len(safety),flush=True)
        return d,row
    if a.group=='regression':
        recovery=[(1500,'SAVE',0),(1800,'SAVE',1),(2200,'LOAD',0),(3500,'REWIND',1),
                  (3510,'LOAD',1),(3530,'LOAD',0),(3600,'REWIND',0)]
        for code in identities:
            for label,speed,mode,commands in [('1x',1,'experimental',()),('2x',2,'experimental',()),
                ('switch',1,'experimental',[(2000,'SPEED',2),(4000,'SPEED',1)]),
                ('disabled',2,'disabled',()),('recovery',2,'experimental',recovery),
                ('conservative',2,'conservative',())]:
                _,before=run(code,code+'-'+label+'-v03',speed,mode,commands,a.baseline)
                _,after=run(code,code+'-'+label+'-poc',speed,mode,commands)
                ok=before['returncode']==after['returncode']==0 and all(before[k]==after[k] for k in ('callback_sha256','event_sha256'))
                checks.append(dict(case=code+'-'+label,byte_exact=ok))
                (a.output/'results.json').write_text(json.dumps(dict(runs=results,checks=checks,
                    real_3x_proven=False),indent=2)+'\n')
    elif a.group=='three':
        for code in identities:
            refs=[]
            for speed in (1,2,3): refs.append(run(code,code+'-'+str(speed)+'x',speed))
            comparison=comparisons(refs[0][0]/'callback.s16le',refs[2][0]/'callback.s16le')
            checks.append(dict(case=code+'-1x-vs-3x',comparison=comparison,
                               fixed_route_required=refs[0][1]['final_fixed'] and refs[2][1]['final_fixed']))
            for name,speeds in [('12321',[1,2,3,2,1]),('131',[1,3,1]),('313',[3,1,3])]:
                commands=[(int(a.frames*i/len(speeds)),'SPEED',s) for i,s in enumerate(speeds) if i]
                run(code,code+'-switch-'+name,speeds[0],commands=commands)
            recovery=[(800,'SAVE',0),(1000,'SPEED',3),(1800,'LOAD',0),(2600,'SAVE',1),
                      (3000,'SPEED',1),(3100,'LOAD',1),(3800,'SPEED',3),(3900,'LOAD',1),
                      (4500,'REWIND',1),(4520,'LOAD',1),(4540,'LOAD',0),(4600,'REWIND',0),
                      (4601,'SPEED',1),(5000,'SPEED',3)]
            run(code,code+'-load-rewind-matrix',1,commands=recovery)
            for mode in ('conservative','disabled'): run(code,code+'-3x-'+mode,3,mode)
    else:
        for code in identities:
            for kind in ('unknown','zero','other'):
                run(code,code+'-rate-'+kind,3,extra=dict(MGBA_RUNNER_RATE_FAULT_AT='1800',
                    MGBA_RUNNER_FAULT_DURATION='120',MGBA_RUNNER_RATE_FAULT_KIND=kind))
            for label,extra in [('partial',{'MGBA_RUNNER_PARTIAL_BATCH_AT':'1800'}),
                ('ring-overflow',{'MGBA_FIXED_AUDIO_INJECT_RING_OVERRUN_AT':'1800'}),
                ('ring-shortage',{'MGBA_FIXED_AUDIO_INJECT_RING_UNDERRUN_AT':'1800'})]:
                run(code,code+'-'+label,3,extra=extra)
            run(code,code+'-explicit-disabled-conflict',3,'disabled',extra=dict(
                MGBA_FIXED_AUDIO_TEMPO='1',MGBA_FIXED_AUDIO_EWRAM_EXPERIMENTAL_ALL='1'))
    (a.output/'results.json').write_text(json.dumps(dict(runs=results,checks=checks,real_3x_proven=False,
        method='Deterministic unthrottled host. All private PCM/logs isolated. Waveform windows cannot certify complete BGM/SE quality.',
        core_sha256=sha(a.core),baseline_sha256=sha(a.baseline)),indent=2)+'\n')
    assert all(r['returncode']==0 for r in results)
    assert all(c.get('byte_exact',True) for c in checks), 'v0.3 PCM/event mismatch; inspect every recorded case'

if __name__=='__main__': main()
