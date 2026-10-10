"""Bounded AORJ replay and pending-lifetime recovery traces; no playback.
SPDX-License-Identifier: MPL-2.0. Recovery drops old independent voices by design.
"""
import argparse,json,os,re,subprocess
from pathlib import Path
from test_three_x_poc import sha,FPS

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('runner','core','bridge','rom','state','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    inputs={str(f.resolve()):sha(f) for f in (a.runner,a.core,a.bridge,a.rom,a.state)}
    state=a.output/'scene.rawstate';state.write_bytes(a.state.read_bytes());rows=[]
    for speed in (2,3):
        first=19*speed
        for scenario in ('late-replay','pending-replay','pending-load','pending-rewind','pending-switch'):
            d=a.output/f'{speed}-{scenario}';d.mkdir();commands=[(0,'SPEED',speed)];presses=[first]
            if scenario=='late-replay':presses.append(55*speed)
            if scenario=='pending-replay':presses.append(first+18)
            if scenario=='pending-load':commands += [(first+17,'SAVE',0),(first+19,'LOAD',0)]
            if scenario=='pending-rewind':commands += [(first+17,'SAVE',0),(first+19,'REWIND',1),(first+20,'LOAD',0),(first+22,'REWIND',0),(first+23,'SPEED',1),(first+30,'SPEED',speed)]
            if scenario=='pending-switch':commands += [(first+17,'SPEED',5-speed),(first+21,'SPEED',1),(first+25,'SPEED',speed)]
            actions=d/'actions.txt';actions.write_text(''.join(f'{f} {op} {v}\n' for f,op,v in commands))
            controls=d/'controls.txt';controls.write_text(''.join(f'{f} A 1\n' for f in presses))
            env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
            env.update(LIBRETRO_SYSTEM_DIRECTORY=str(d.resolve()),MGBA_MP2K_BRIDGE_PATH=str(a.bridge.resolve()),
                MGBA_RUNNER_INITIAL_STATE=str(state.resolve()),MGBA_RUNNER_FIXED_AUDIO_MODE='experimental',
                MGBA_RUNNER_STATE_ACTIONS=str(actions.resolve()),MGBA_RUNNER_STATE_OUTPUT_PREFIX=str((d/'private-state').resolve()),
                MGBA_RUNNER_CALLBACK_PATH=str((d/'callback.s16le').resolve()),MGBA_FIXED_AUDIO_DIAGNOSTICS='1',
                MGBA_FIXED_AUDIO_RUN_TRACE='1',MGBA_MP2K_LIFETIME_STOP_PC='0x08082158',MGBA_MP2K_LIFETIME_FINISH_PC='0x08081422',
                MGBA_PHASE4_PSG_TRACE=str((d/'psg.log').resolve()))
            with (d/'process.log').open('wb') as log:
                r=subprocess.run([str(a.runner.resolve()),str(a.core.resolve()),str(a.rom.resolve()),str((d/'run').resolve()),'normal',str(int(1.7*FPS*speed)),str(controls.resolve())],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90)
            text=(d/'run.log').read_text(errors='replace');voices=(d/'psg.log').read_text().splitlines()
            plays=[s for s in text.splitlines() if 'type=PLAY_SONG song=202 ' in s]
            stops=[s for s in text.splitlines() if 'type=STOP_SONG song=202 ' in s]
            notes=[s for s in voices if 'player=2 track=0 ' in s and 'decision=REQUEST ' in s]
            ends=[s for s in voices if 'decision=END player=2 track=0 ' in s]
            fields=lambda s:dict(re.findall(r'(\w+)=([^\s]+)',s))
            note_keys=[int(fields(s)['key']) for s in notes]
            assert r.returncode==0 and not stops
            expected_plays=2 if 'replay' in scenario else 1
            missing_replay='replay' in scenario and len(plays)==1
            assert len(plays)==expected_plays or missing_replay
            if missing_replay:assert note_keys==[76,81,83,88,88,88,88]
            if scenario=='late-replay' and not missing_replay:assert note_keys==[76,81,83,88,88,88,88]*2
            if scenario=='pending-replay' and not missing_replay:
                starts=[i for i,k in enumerate(note_keys) if k==76]
                assert len(starts)==2 and note_keys[starts[-1]:]==[76,81,83,88,88,88,88]
            if scenario=='pending-switch':assert note_keys==[76,81,83,88,88,88,88]
            state_calls=re.findall(r'\[HOST\] state (\w+) slot=(\d+) frame=(\d+) success=(\d)',text)
            if scenario in ('pending-load','pending-rewind'):
                saves_loads=[s for s in state_calls if s[0] in ('SAVE','LOAD')]
                assert len(saves_loads)==2 and all(s[-1]=='1' for s in saves_loads)
            under=max([0]+list(map(int,re.findall(r'underrun=(\d+)',text))));over=max([0]+list(map(int,re.findall(r'overrun=(\d+)',text))))
            assert under==over==0
            row=dict(speed=speed,scenario=scenario,returncode=r.returncode,play_count=len(plays),stop_count=len(stops),
                note_keys=note_keys,requests=[fields(s) for s in notes],ends=[fields(s) for s in ends],
                state_calls=state_calls,underrun=under,overrun=over,callback_sha256=sha(d/'callback.s16le'),
                recovery_markers=[s for s in text.splitlines() if 'STATE_LOAD' in s or 'REWIND_' in s or 'NATURAL COMPLETE' in s],
                status='INCONCLUSIVE_NO_SECOND_GAME_REQUEST' if missing_replay else 'PASS_STRUCTURAL_TRACE',human_listening='HUMAN_REVIEW_REQUIRED')
            rows.append(row);print(speed,scenario,'notes',len(notes),flush=True)
    assert all(sha(Path(f))==h for f,h in inputs.items())
    (a.output/'results.json').write_text(json.dumps(dict(inputs=inputs,runs=rows,
        limits='Injected native call tests cover explicit Stop separately. These gameplay fixtures cover replay, speed changes and discarding pending voices on recovery, not all battle/loop/pitch-bend SFX.'),indent=2)+'\n',encoding='utf8')
if __name__=='__main__':main()
