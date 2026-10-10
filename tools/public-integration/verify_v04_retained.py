"""Reverify immutable regressions/recordings without regenerating gigabytes of PCM.
SPDX-License-Identifier: MPL-2.0. Does not change references or classify new tests.
"""
import hashlib,json,subprocess
from pathlib import Path
from test_three_x_poc import sha,MARKERS
def read(p):return json.loads(p.read_text(encoding='utf8'))
def events(p):return hashlib.sha256(('\n'.join(x for x in p.read_text(errors='replace').splitlines() if any(m in x for m in MARKERS))+'\n').encode()).hexdigest().upper()
def main():
 root=Path.cwd();p=root/'build-phase9';checks=[]
 report=read(root/'docs/GBA_BGM_COVERAGE_PHASE9.json')
 for e in report['evidence']:assert sha(root/e['path'])==e['sha256'],e['path']
 for e in report['runtime']['final']['files']:assert sha(Path(e['path']))==e['sha256']
 for r in read(p/'regression48/results.json')['rows']:
  d=p/'regression48'/r['case'];ref=Path(r['reference'])
  assert r['pcm_exact'] and r['event_exact'] and r['returncode']==0
  assert sha(ref/'run.log')==r['reference_log_sha256']
  assert sha(d/'callback.s16le')==sha(ref/'callback.s16le') and events(d/'run.log')==events(ref/'run.log'),r['case']
 checks.append(dict(name='48 conditions',passed=48,scope='Retained full PCM/event evidence rehashed; no new execution'))
 scene=read(p/'scenes16/results.json');assert len(scene['comparisons'])==16
 for r in scene['comparisons']:
  code,speed,label=r['game'],str(r['speed']),r['comparison']
  assert r['pcm_exact'] and r['event_exact'] and r['initial_ram_exact']
  for prefix,k in [('phase4','candidate_pcm_sha256'),(label,'reference_pcm_sha256')]:assert sha(p/'scenes16'/(prefix+'-'+code)/speed/'callback.s16le')==r[k]
 checks.append(dict(name='16 public/common bridge comparisons',passed=16,scope='Retained original-public-package and common-corrected-bridge comparisons kept separate; full PCM rehashed'))
 three=read(p/'three36/results.json')['runs'];assert len(three)==36
 for r in three:
  d=p/'three36'/r['label'];assert sha(d/'callback.s16le')==r['callback_sha256'] and events(d/'run.log')==r['event_sha256']
  assert r['returncode']==0 and not any(r[k] for k in ('underrun','overrun','queue_dropped'))
 checks.append(dict(name='representative3x36',passed=36,scope='Retained full callback/event evidence rehashed, includes switching/recovery/modes'))
 legacy=read(p/'legacy12/results.json');assert legacy['passed'] and len(legacy['cases'])==12
 for r in legacy['cases']:assert r['returncode']==0 and (p/'legacy12'/(r['case']+'.log')).is_file()
 checks.append(dict(name='legacy12 suite',passed=12,scope='Previously passed suite metadata/logs retained; not rerun in Phase10'))
 assets={}
 for r in read(root/'build-phase7/protection-start.json')['wavs']:assets[r['path']]=r['sha256']
 for r in read(root/'build-phase8/listening-review-final/manifest.json')['rows']:assets[r['path']]=r['sha256']
 for r in read(root/'build-phase9/listening-review/manifest.json')['new_clips']:assets[r['path']]=r['sha256']
 assert len(assets)==127
 for name,h in assets.items():assert sha(Path(name))==h,name
 original=read(root/'build-phase7/protection-start.json')['public_artifacts']
 for name,h in original.items():assert sha(Path(name))==h
 assert subprocess.check_output(['git','-C',str(root.parent/'mgba_public_v03_fresh'),'status','--porcelain']).strip()==b''
 checks.append(dict(name='protection',protected_wavs=127,all_hashes_unchanged=True,public_artifacts=len(original),public_worktree_clean=True))
 result=dict(passed=True,checks=checks,method='No golden updates or relaxed comparisons; current RC is byte-identical to Phase9 final. Fresh prefix12 and actual frontend results are reported separately.')
 (root/'build-phase10/retained-verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(checks)
if __name__=='__main__':main()
