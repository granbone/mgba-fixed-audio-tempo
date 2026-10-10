"""AORJ original-package/Phase3/probe/Phase4/native comparisons, no playback.
SPDX-License-Identifier: MPL-2.0. Exact hashes never become tolerance passes.
"""
import argparse, hashlib, json, os, re, subprocess
from pathlib import Path
import numpy as np
from test_three_x_poc import MARKERS, sha


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('runner','core','bridge','probe','rom','controls','output'):
        p.add_argument('--'+n, type=Path, required=True)
    p.add_argument('--root', type=Path, default=Path.cwd())
    p.add_argument('--frames', type=int, default=2400)
    p.add_argument('--speeds', default='1,2,3')
    p.add_argument('--compare-legacy-three', action='store_true', help='Also test Phase3 production/model at3x; public3x fallback remains outside fixed-wave comparisons')
    a=p.parse_args(); d=a.output.resolve(); d.mkdir(parents=True,exist_ok=False)
    root=a.root.resolve(); public=root.parent/'mgba_public_v03_fresh/build-publication-distribution/package'
    oldcore=public/'cores/mgba_fixed_audio_libretro.dll'; oldbridge=public/'runtime/libmgba_mp2k_bridge.dll'
    phase3=root/'build-phase2/candidate/runtime'; phase3probe=root/'build-phase3/priority-probe/build/libmgba_mp2k_bridge.dll'
    inputs={str(f.resolve()):sha(f) for f in (a.runner,a.core,a.bridge,a.probe,a.rom,a.controls,oldcore,oldbridge,phase3/'mgba_fixed_audio_libretro.dll',phase3/'libmgba_mp2k_bridge.dll',phase3probe)}
    assert sha(a.rom)=='0E9997636409C47734895EA2180521FA7AD8898D7F45E01634C9E207039D7808'
    variants={
        'A-original':(oldcore,oldbridge,{}),
        'B-phase3':(phase3/'mgba_fixed_audio_libretro.dll',phase3/'libmgba_mp2k_bridge.dll',{}),
        'C-phase3-probe':(a.core,phase3probe,{'MGBA_PHASE2_PSG_ORDER':'LOGICAL','MGBA_PHASE3_NATIVE_PRIORITY':'1'}),
        'D-phase4':(a.core,a.bridge,{}),
        'E-native':(a.core,a.bridge,{'MGBA_RUNNER_FIXED_AUDIO_MODE':'disabled'}),
        'B-instrumented':(a.core,a.probe,{'MGBA_PHASE4_LEGACY_PRIORITY':'1'}),
        'D-instrumented':(a.core,a.probe,{})}
    rows=[]
    speeds=list(map(int,a.speeds.split(',')));assert set(speeds)<={1,2,3}
    for speed in speeds:
        for label,(core,bridge,extra) in variants.items():
            if speed==3 and (label in ('A-original','C-phase3-probe','E-native') or
                label in ('B-phase3','B-instrumented') and not a.compare_legacy_three): continue
            repeats=2 if label in ('A-original','B-phase3','D-phase4') and speed<3 else 1
            for repeat in range(repeats):
                folder=d/f'{label}-{speed}-{repeat}';folder.mkdir()
                action=folder/'actions.txt';action.write_text(f'0 SPEED {speed}\n')
                env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
                env.update(MGBA_MP2K_BRIDGE_PATH=str(bridge.resolve()),LIBRETRO_SYSTEM_DIRECTORY=str(folder),
                    MGBA_RUNNER_FIXED_AUDIO_MODE='experimental',MGBA_RUNNER_CALLBACK_PATH=str(folder/'callback.s16le'),
                    MGBA_RUNNER_CALLBACK_INDEX='1',MGBA_RUNNER_INITIAL_SNAPSHOT=str(folder/'initial'),
                    MGBA_RUNNER_STATE_ACTIONS=str(action),MGBA_FIXED_AUDIO_RUN_TRACE='1',MGBA_FIXED_AUDIO_DIAGNOSTICS='1',
                    MGBA_MP2K_PSG_OWNER_TRACE='1',MGBA_RUNNER_NATIVE_PSG_BASE='0x03006b90',
                    MGBA_PHASE4_PSG_TRACE=str(folder/'psg.log'),MGBA_MP2K_STARTUP_TRACE_PATH=str(folder/'startup.log'))
                env.update(extra)
                with (folder/'process.log').open('wb') as f:
                    result=subprocess.run([str(a.runner.resolve()),str(core.resolve()),str(a.rom.resolve()),str(folder/'run'),
                        'normal',str(a.frames),str(a.controls.resolve())],env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180)
                text=(folder/'run.log').read_text(errors='replace');events=[x for x in text.splitlines() if any(m in x for m in MARKERS)]
                psg=(folder/'psg.log').read_text().splitlines() if (folder/'psg.log').exists() else []
                native=[fields(x) for x in text.splitlines() if '[HOST NATIVE PSG]' in x]
                row=dict(case=folder.name,variant=label,speed=speed,repeat=repeat,returncode=result.returncode,
                    core_sha256=sha(core),bridge_sha256=sha(bridge),pcm_sha256=sha(folder/'callback.s16le'),
                    event_sha256=hashlib.sha256(('\n'.join(events)+'\n').encode()).hexdigest().upper(),
                    initial_state_sha256=sha(folder/'initial.rawstate'),initial_ram_sha256=sha(folder/'initial.ram'),
                    native_transition_sha256=hashlib.sha256(('\n'.join(x for x in text.splitlines() if '[HOST NATIVE PSG]' in x)+'\n').encode()).hexdigest().upper(),
                    native_se_transitions=[n for n in native if n.get('track')=='03005a10'],
                    requests=[fields(x) for x in psg if 'player=2 track=0 key=88 ' in x and 'decision=END' not in x],
                    ends=[fields(x) for x in psg if 'decision=END player=2 track=0 key=88 ' in x],
                    max_existing_psg_voices=max([0]+[int(fields(x)['voices']) for x in psg if 'voices=' in x]),
                    underrun=max([0]+list(map(int,re.findall(r'\bunderrun=(\d+)',text)))),
                    overrun=max([0]+list(map(int,re.findall(r'\boverrun=(\d+)',text)))))
                rows.append(row);print(row['case'],row['pcm_sha256'],flush=True)
                (d/'results.json').write_text(json.dumps(dict(inputs=inputs,runs=rows),indent=2)+'\n')
    comparisons=[]
    for speed in speeds:
        ref=d/f'D-phase4-{speed}-0';x=np.fromfile(ref/'callback.s16le','<i2').reshape(-1,2)
        for r in rows:
            if r['speed']!=speed or r['case']==ref.name:continue
            y=np.fromfile(d/r['case']/'callback.s16le','<i2').reshape(-1,2);n=min(len(x),len(y))
            diff=np.flatnonzero(np.any(x[:n]!=y[:n],axis=1))
            first=int(diff[0]) if len(diff) else n if len(x)!=len(y) else None
            comparisons.append(dict(speed=speed,left=r['case'],right=ref.name,byte_exact=first is None,
                first_difference_sample=first,phase4_timestamp_seconds=first/32768 if first is not None else None,
                event_exact=r['event_sha256']==next(q['event_sha256'] for q in rows if q['case']==ref.name)))
    assert all(sha(Path(f))==h for f,h in inputs.items())
    report=dict(inputs=inputs,runs=rows,comparisons=comparisons,input_hashes_unchanged=True,
        method='Identical boot, ROM, initial snapshot/RAM, callback acceptance and game-frame controls. Native rates differ: do not interpret E waveform mismatch as lost notes. Instrumentation and probe SHA are distinct from production.')
    (d/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    assert all(r['returncode']==0 for r in rows)
    for speed in speeds:
        for label in ('D-instrumented',)+ (('C-phase3-probe',) if speed<3 else ()):
            assert next(c['byte_exact'] for c in comparisons if c['left']==f'{label}-{speed}-0')
    assert all(r['max_existing_psg_voices']<=1 for r in rows)


if __name__=='__main__':main()
