"""Replay an immutable 48-case PCM/event matrix using explicit final DLLs.

SPDX-License-Identifier: MPL-2.0. All private inputs and references are external.
No reference updates, test relaxation, playback or publication.
"""
import argparse,hashlib,json,os,subprocess,time
from pathlib import Path

MARKERS=('[MP2K EVENT]','[MP2K SEMANTIC]','[MP2K FIRST TICK]',
 '[MP2K FRONTEND]','[FIXED AUDIO RUN]','[FIXED AUDIO] end',
 '[B6JJ RUN]','[B6JJ CLOCK]','[B6JJ EVENTS]','[B6JJ BUFFER]')
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest().upper()
def events(p):
 lines=p.read_text(errors='replace').splitlines()
 return hashlib.sha256(('\n'.join(x for x in lines if any(m in x for m in MARKERS))+'\n').encode()).hexdigest().upper()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('reference','rom-dir','core','bridge','runner','output'):p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
 refs=json.loads((a.reference/'results.json').read_text())['runs']
 cases=[x for x in refs if x['label'].endswith('-poc')];assert len(cases)==48
 roms={}
 for f in a.rom_dir.glob('*.gba'):
  with f.open('rb') as stream:stream.seek(0xac);code=stream.read(4).decode('ascii',errors='replace')
  roms.setdefault(code,[]).append(f)
 rows=[];controls=a.reference/'controls.txt';controls_hash=sha(controls)
 for ref in cases:
  before=a.reference/ref['label'];folder=a.output/ref['label'];folder.mkdir()
  assert sha(before/'callback.s16le')==ref['callback_sha256']
  assert events(before/'run.log')==ref['event_sha256']
  rom=next(f for f in roms[ref['game_code']] if sha(f)==ref['rom_sha256'])
  actions=folder/'actions.txt';actions.write_bytes((before/'actions.txt').read_bytes())
  env={k:v for k,v in os.environ.items() if not k.startswith('MGBA_')}
  env.update(LIBRETRO_SYSTEM_DIRECTORY=str(folder.resolve()),MGBA_RUNNER_FIXED_AUDIO_MODE=ref['mode'],MGBA_RUNNER_CALLBACK_PATH=str((folder/'callback.s16le').resolve()),MGBA_FIXED_AUDIO_RUN_TRACE='1',MGBA_FIXED_AUDIO_DIAGNOSTICS='1',MGBA_RUNNER_STATE_ACTIONS=str(actions.resolve()),MGBA_MP2K_BRIDGE_PATH=str(a.bridge.resolve()))
  start=time.perf_counter()
  with (folder/'process.log').open('wb') as log:
   q=subprocess.run([str(a.runner.resolve()),str(a.core.resolve()),str(rom.resolve()),str((folder/'run').resolve()),'normal',str(ref['frames']),str(controls.resolve())],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300)
  row=dict(case=ref['label'],returncode=q.returncode,seconds=time.perf_counter()-start,pcm_exact=sha(folder/'callback.s16le')==ref['callback_sha256'],event_exact=events(folder/'run.log')==ref['event_sha256'],reference_pcm_sha256=ref['callback_sha256'],reference_event_sha256=ref['event_sha256'],actions_sha256=sha(actions))
  assert sha(rom)==ref['rom_sha256'] and sha(controls)==controls_hash
  rows.append(row)
  result=dict(rows=rows,core_sha256=sha(a.core),bridge_sha256=sha(a.bridge),runner_sha256=sha(a.runner),controls_sha256=controls_hash,baseline_scope='Immutable corrected-bridge reference; not original v0.3 package',goldens_updated=False)
  (a.output/'results.json').write_text(json.dumps(result,indent=2)+'\n')
  print(row['case'],row['pcm_exact'],row['event_exact'],flush=True)
 assert all(x['returncode']==0 and x['pcm_exact'] and x['event_exact'] for x in rows)
if __name__=='__main__':main()
