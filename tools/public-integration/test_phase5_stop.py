"""Read-only AORJ native-finish / polled STOP reproduction. No playback.
SPDX-License-Identifier: MPL-2.0. No STOP policy override or guard bypass.
"""
import argparse, json, os, re, subprocess
from pathlib import Path
from test_three_x_poc import sha, FPS, MARKERS
import hashlib

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('runner','core','bridge','rom','state','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--seconds',type=float,default=1.1)
 a=p.parse_args();d=a.output.resolve();d.mkdir(parents=True,exist_ok=False)
 inputs={str(f.resolve()):sha(f) for f in (a.runner,a.core,a.bridge,a.rom,a.state)}
 assert sha(a.rom)=='0E9997636409C47734895EA2180521FA7AD8898D7F45E01634C9E207039D7808'
 state=d/'scene.rawstate';state.write_bytes(a.state.read_bytes());rows=[]
 for mode,speed in [('experimental',1),('experimental',2),('experimental',3),('disabled',1)]:
  folder=d/f'{mode}-{speed}';folder.mkdir()
  actions=folder/'actions.txt';actions.write_text(f'0 SPEED {speed}\n')
  controls=folder/'controls.txt';controls.write_text(f'{19*speed} A 1\n')
  env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
  env.update(LIBRETRO_SYSTEM_DIRECTORY=str(folder),MGBA_RUNNER_FIXED_AUDIO_MODE=mode,
   MGBA_MP2K_BRIDGE_PATH=str(a.bridge.resolve()),MGBA_RUNNER_INITIAL_STATE=str(state),
   MGBA_RUNNER_CALLBACK_PATH=str(folder/'callback.s16le'),MGBA_RUNNER_CALLBACK_INDEX='1',
   MGBA_RUNNER_INITIAL_SNAPSHOT=str(folder/'initial'),MGBA_RUNNER_STATE_ACTIONS=str(actions),
   MGBA_FIXED_AUDIO_RUN_TRACE='1',MGBA_FIXED_AUDIO_DIAGNOSTICS='1',
   MGBA_MP2K_LIFETIME_STOP_PC='0x08082158',MGBA_MP2K_LIFETIME_FINISH_PC='0x08081422',
   MGBA_RUNNER_LIFETIME_PLAYER='0x03006d10',MGBA_RUNNER_NATIVE_PSG_BASE='0x03006b90',
   MGBA_PHASE4_PSG_TRACE=str(folder/'psg.log'),MGBA_MP2K_PSG_OWNER_TRACE='1')
  with (folder/'process.log').open('wb') as f:
   r=subprocess.run([str(a.runner.resolve()),str(a.core.resolve()),str(a.rom.resolve()),str(folder/'run'),'normal',str(int(a.seconds*FPS*speed)),str(controls)],env=env,stdout=f,stderr=subprocess.STDOUT,timeout=90)
  text=(folder/'run.log').read_text(errors='replace');lines=text.splitlines()
  events=[x for x in lines if any(m in x for m in MARKERS)]
  row=dict(mode=mode,speed=speed,returncode=r.returncode,callback_sha256=sha(folder/'callback.s16le'),
   initial_ram_sha256=sha(folder/'initial.ram'),event_sha256=hashlib.sha256(('\n'.join(events)+'\n').encode()).hexdigest().upper(),
   evidence=[x for x in lines if '[MP2K LIFETIME' in x or '[HOST LIFETIME' in x or '[HOST NATIVE PSG]' in x or '[MP2K SEMANTIC]' in x],
   voices=(folder/'psg.log').read_text().splitlines() if (folder/'psg.log').exists() else [])
  rows.append(row);print(mode,speed,len(row['evidence']),flush=True)
 assert all(sha(Path(f))==h for f,h in inputs.items())
 assert all(r['returncode']==0 for r in rows)
 assert len(set(r['initial_ram_sha256'] for r in rows))==1
 (d/'results.json').write_text(json.dumps(dict(inputs=inputs,runs=rows,originals_unchanged=True),indent=2)+'\n')
if __name__=='__main__':main()
