"""Equal audio-duration scene probes, with unscaled private PCM and cue timing.
SPDX-License-Identifier: MPL-2.0. BSV/state/ROM inputs remain read-only.
"""
import argparse,json,os,re,struct,subprocess
from pathlib import Path
from test_three_x_poc import sha,FPS

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('runner','core','bridge','rom','state','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--seconds',type=float,default=22)
    p.add_argument('--speeds',default='1,2,3')
    p.add_argument('--controls',type=Path,help='Read-only button schedule; default is no input')
    p.add_argument('--scale-control-frames',action='store_true',help='Align controlled SE triggers on the audio timeline; press lengths remain in game frames')
    p.add_argument('--psg-trace',action='store_true',help='Private Phase4 diagnostic bridge allocations and core owner trace')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    inputs={str(f):sha(f) for f in (a.runner,a.core,a.bridge,a.rom,a.state)}
    if a.controls:inputs[str(a.controls)]=sha(a.controls)
    raw=a.state.read_bytes()
    if a.state.suffix=='.bsv':
        size=struct.unpack_from('<I',raw,12)[0];assert 16+size<=len(raw)
        raw=raw[16:16+size]
    state=(a.output/'scene.rawstate').resolve();state.write_bytes(raw)
    controls=(a.output/'no-input.txt').resolve();controls.write_text('')
    records=[]
    for speed in map(int,a.speeds.split(',')):
        d=(a.output/str(speed)).resolve();d.mkdir()
        schedule=controls
        if a.controls:
            schedule=d/'controls.txt'
            lines=[]
            for line in a.controls.read_text().splitlines():
                fields=line.split()
                if not fields or fields[0].startswith('#'):continue
                assert len(fields)==3
                frame=int(fields[0])*(speed if a.scale_control_frames else 1)
                lines.append(f'{frame} {fields[1]} {fields[2]}\n')
            schedule.write_text(''.join(lines))
        actions=d/'actions.txt';actions.write_text(f'0 SPEED {speed}\n')
        env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
        env.update(LIBRETRO_SYSTEM_DIRECTORY=str(d),MGBA_RUNNER_FIXED_AUDIO_MODE='experimental',
            MGBA_MP2K_BRIDGE_PATH=str(a.bridge.resolve()),MGBA_RUNNER_INITIAL_STATE=str(state),
            MGBA_RUNNER_CALLBACK_PATH=str(d/'callback.s16le'),MGBA_RUNNER_CALLBACK_INDEX='1',
            MGBA_RUNNER_STATE_ACTIONS=str(actions),MGBA_RUNNER_INITIAL_SNAPSHOT=str(d/'initial'),
            MGBA_FIXED_AUDIO_RUN_TRACE='1',MGBA_FIXED_AUDIO_DIAGNOSTICS='1',
            MGBA_MP2K_AUDIO_TRACE='1',MGBA_MP2K_AUDIO_TRACE_PATH=str(d/'backend.s16le'),
            MGBA_B6JJ_EVENT_TRACE='1',MGBA_MP2K_STARTUP_TRACE_PATH=str(d/'startup.log'))
        frames=int(a.seconds*FPS*speed)
        if a.psg_trace:
            env.update(MGBA_PHASE4_PSG_TRACE=str(d/'psg.log'),MGBA_MP2K_PSG_OWNER_TRACE='1')
        with (d/'process.log').open('wb') as log:
            result=subprocess.run([str(a.runner.resolve()),str(a.core.resolve()),str(a.rom.resolve()),
                str(d/'run'),'normal',str(frames),str(schedule)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
        text=(d/'run.log').read_text(errors='replace')
        records.append(dict(speed=speed,frames=frames,returncode=result.returncode,
            initial_state_sha256=sha(d/'initial.rawstate'),initial_ram_sha256=sha(d/'initial.ram'),
            callback_sha256=sha(d/'callback.s16le'),callback_frames=(d/'callback.s16le').stat().st_size//4,
            events=[line for line in text.splitlines() if '[B6JJ EVENT]' in line or '[MP2K SEMANTIC]' in line],
            underrun=max([int(x) for x in re.findall(r'\bunderrun(?:_count)?=(\d+)',text)] or [0]),
            overrun=max([int(x) for x in re.findall(r'\boverrun(?:_count)?=(\d+)',text)] or [0]),
            fallback=re.findall(r'(?:SAFE FALLBACK reason=|NATIVE_FALLBACK reason=)([^\n]+)',text)))
        print(json.dumps(records[-1]),flush=True)
    assert all(sha(Path(f))==h for f,h in inputs.items())
    (a.output/'results.json').write_text(json.dumps(dict(inputs=inputs,records=records,
        method='Same initial loaded native state; '+('controlled input, audio-aligned triggers' if a.scale_control_frames else 'same game-frame input' if a.controls else 'no input')+'. Equal ideal audio duration, independent clocks. Not wall-clock speed proof.'),indent=2)+'\n')
    assert all(r['returncode']==0 for r in records)
if __name__=='__main__':main()
