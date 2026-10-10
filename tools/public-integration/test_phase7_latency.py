"""Paired input/no-input onset measurements, without playback or policy changes.
SPDX-License-Identifier: MPL-2.0.
"""
import argparse, hashlib, json, os, re, subprocess
from pathlib import Path
import numpy as np
from test_three_x_poc import FPS, sha

def fields(s): return dict(re.findall(r'(\w+)=([^\s]+)',s))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('runner','core','bridge','rom','state','output'): p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--repetitions',type=int,default=3)
    a=p.parse_args();d=a.output.resolve();d.mkdir(parents=True,exist_ok=False)
    inputs={str(f.resolve()):sha(f) for f in (a.runner,a.core,a.bridge,a.rom,a.state)}
    state=d/'scene.rawstate';state.write_bytes(a.state.read_bytes());rows=[]
    for mode,speed in [('disabled',1),('experimental',1),('experimental',2),('experimental',3)]:
        runs=[]
        for trial in range(a.repetitions):
            paired=[]
            for pressed in (False,True):
                out=d/f'{mode}-{speed}-{trial}-{int(pressed)}';out.mkdir()
                actions=out/'actions.txt';actions.write_text(f'0 SPEED {speed}\n')
                controls=out/'controls.txt';controls.write_text(f'{19*speed} A 1\n' if pressed else '')
                env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
                env.update(LIBRETRO_SYSTEM_DIRECTORY=str(out),MGBA_RUNNER_FIXED_AUDIO_MODE=mode,
                    MGBA_MP2K_BRIDGE_PATH=str(a.bridge.resolve()),MGBA_RUNNER_INITIAL_STATE=str(state),
                    MGBA_RUNNER_STATE_ACTIONS=str(actions),MGBA_RUNNER_CALLBACK_PATH=str(out/'callback.s16le'),
                    MGBA_RUNNER_CALLBACK_INDEX='1',MGBA_RUNNER_INITIAL_SNAPSHOT=str(out/'initial'),
                    MGBA_MP2K_CANDIDATE_PATH=str(out/'candidate.s16le'),
                    MGBA_FIXED_AUDIO_RUN_TRACE='1',MGBA_FIXED_AUDIO_DIAGNOSTICS='1',
                    MGBA_MP2K_LIFETIME_STOP_PC='0x08082158',MGBA_MP2K_LIFETIME_FINISH_PC='0x08081422',
                    MGBA_RUNNER_LIFETIME_PLAYER='0x03006d10',MGBA_RUNNER_NATIVE_PSG_BASE='0x03006b90')
                with (out/'process.log').open('wb') as log:
                    proc=subprocess.run([str(a.runner.resolve()),str(a.core.resolve()),str(a.rom.resolve()),
                        str(out/'run'),'normal',str(int(.9*FPS*speed)),str(controls)],env=env,
                        stdout=log,stderr=subprocess.STDOUT,timeout=90)
                assert proc.returncode==0
                text=(out/'run.log').read_text(errors='replace')
                events=[fields(x) for x in text.splitlines() if '[MP2K SEMANTIC]' in x and 'song=202 ' in x]
                ticks=[fields(x) for x in text.splitlines() if '[MP2K FIRST TICK]' in x and 'song=202 ' in x]
                native=[x for x in text.splitlines() if '[HOST NATIVE PSG]' in x and 'key=76 ' in x]
                paired.append(dict(pressed=pressed,pcm=out/'callback.s16le',initial_ram=sha(out/'initial.ram'),
                    candidate=out/'candidate.s16le',av_rates=[fields(x) for x in text.splitlines() if '[HOST] av_rate=' in x],
                    callback_sha256=sha(out/'callback.s16le'),events=events,first_ticks=ticks,native_first_key=native[:1],
                    callbacks=[fields(x) for x in text.splitlines() if '[HOST CALLBACK]' in x],
                    run_clock=[x for x in text.splitlines() if '[AUDIO CLOCK] run=' in x],
                    ring_errors=max([0]+[int(x) for x in re.findall(r'(?:under|over)run(?:_count)?=(\d+)',text)]),
                    queue_drop=max([0]+[int(x) for x in re.findall(r'queue_dropped=(\d+)',text)])))
            left,right=[np.fromfile(x['pcm'],dtype='<i2').reshape(-1,2) for x in paired]
            assert left.shape==right.shape and paired[0]['initial_ram']==paired[1]['initial_ram']
            diff=np.flatnonzero(np.any(left!=right,axis=1));assert len(diff)
            onset=int(diff[0]);callback=next(x for x in paired[1]['callbacks'] if
                int(x['byteOffset'])//4<=onset<int(x['byteOffset'])//4+int(x['samples']))
            bridge_onset=None
            if all(x['candidate'].exists() for x in paired):
                cl,cr=[np.fromfile(x['candidate'],dtype='<i2').reshape(-1,2) for x in paired]
                assert cl.shape==cr.shape
                cd=np.flatnonzero(np.any(cl!=cr,axis=1));assert len(cd);bridge_onset=int(cd[0])
            # Native startup can change the announced rate. Integrate each callback at its actual av_rate.
            seconds=0.;callback_onset_frame=int(callback['frame'])
            for item in paired[1]['callbacks']:
                frame=int(item['frame']);offset=int(item['byteOffset'])//4
                rate=int(next(x['av_rate'] for x in reversed(paired[1]['av_rates']) if int(x['frame'])<=frame))
                seconds+=min(int(item['samples']),max(0,onset-offset))/rate
                if frame>=callback_onset_frame:break
            for x in paired:x.pop('callbacks');x.pop('pcm');x.pop('candidate')
            runs.append(dict(trial=trial,first_input_effect_callback_sample=onset,callback_timeline_seconds=seconds,
                first_input_effect_candidate_sample=bridge_onset,candidate_timeline_seconds=bridge_onset/32768 if bridge_onset is not None else None,
                callback=callback,paired=paired))
        assert len({x['first_input_effect_callback_sample'] for x in runs})==1
        assert len({x['paired'][1]['callback_sha256'] for x in runs})==1
        assert len({x['paired'][0]['callback_sha256'] for x in runs})==1
        rows.append(dict(mode=mode,speed=speed,reproducible=True,runs=runs))
        print(mode,speed,'onset',runs[0]['first_input_effect_callback_sample'],flush=True)
    assert len({x['paired'][1]['initial_ram'] for row in rows for x in row['runs']})==1
    assert all(sha(Path(f))==h for f,h in inputs.items())
    (d/'results.json').write_text(json.dumps(dict(inputs=inputs,rows=rows,
        coordinate_warning='First input effect in mixed PCM is not an isolated note attack or wall-clock latency. Clock/event and candidate coordinates exclude native startup, AV rate changes and callback ring latency. Callback timeline seconds integrate announced rates and are a concatenated stream coordinate, not measured real time. Disabled uses native output. Native/backend waveform phases may differ.',
        original_inputs_unchanged=True),indent=2)+'\n')

if __name__=='__main__':main()
