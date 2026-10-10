"""Collect bounded Phase7 metadata and verify protected assets. No publication.
SPDX-License-Identifier: MPL-2.0.
"""
import argparse,json,re,subprocess
from pathlib import Path
from test_three_x_poc import sha

def read(p):return json.loads(p.read_text(encoding='utf8'))
def write(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def git(r,*args):return subprocess.check_output(['git','-C',str(r),*args],text=True).strip()

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--verify-only',action='store_true');a=p.parse_args();r=Path.cwd();d=r/'build-phase7'
 protection=read(d/'protection-start.json')
 for x in protection['wavs']:assert sha(Path(x['path']))==x['sha256']
 for file,h in protection['public_artifacts'].items():assert sha(Path(file))==h
 public=r.parent/'mgba_public_v03_fresh';assert git(public,'rev-parse','HEAD')=='91f153b4e7fd192872a892d81f37501b3a8d4304'
 assert not git(public,'status','--short')
 assert git(public,'rev-parse','v0.3-preview')=='4e3f3bc196cf9575496429791650a89b85970f01'
 assert git(public,'rev-parse','v0.3-preview^{}')=='91f153b4e7fd192872a892d81f37501b3a8d4304'
 runtime=read(r/'docs/three-x-phase6-evidence/runtime.json')
 for file,h in runtime['dlls'].items():assert sha(Path(file))==h
 assert sha(r/'build-phase6/retro-runner.exe')==runtime['runner_sha256']
 pin=public/'build-publication-inputs/agbplay/src/agbplay/SequenceReader.cpp'
 assert sha(pin)=='7B77F0A163F9E2EA7BAB3714777764808CDD88ACD82F9707D588520FCE712AC0'
 for head in ('91f153b4e7fd192872a892d81f37501b3a8d4304','e1db58ff4421715daefee0be13099e6b1202c8ca','d08add4741f454f3a459343067b0bb275e998e34','de16b98406b70d6c3602e9fa94375a2dd8326255'):
  subprocess.run(['git','merge-base','--is-ancestor',head,'HEAD'],check=True)
 assert not git(r,'diff','de16b98406b70d6c3602e9fa94375a2dd8326255','--','src','include','tools/mp2k-audio-trace/bridge')
 if a.verify_only:print('103 protected WAVs, public source/tag/artifacts, pinned input and reused runtime verified');return
 out=r/'docs/three-x-phase7-evidence';out.mkdir(exist_ok=False)
 files={'latency':'latency-candidate/results.json','cross-title':'cross-title-analysis.json','cross-title-raw':'cross-title-checked/results.json','cross-title-A8CJ':'cross-title-A8CJ-boot/results.json',
  'native-reference':'cross-title-native/results.json','native-A8CJ-reference':'cross-title-native-A8CJ/results.json','regression48':'regression48/results.json','scenes16':'scenes16/results.json','legacy12':'legacy12/results.json',
  'frontend':'frontend-analysis.json','recovery':'replay/results.json','listening':'listening-review/manifest.json'}
 for name,file in files.items():write(out/(name+'.json'),read(d/file))
 units=[json.loads(next(line for line in reversed((d/f'native-lifetime-{speed}.log').read_text().splitlines()) if line.startswith('{'))) for speed in (2,3)]
 assert all(x['passed'] for x in units);write(out/'native-lifetime.json',dict(results=units,executable_sha256=sha(r/'build-phase6/accepted-lifetime.exe'),static_library_sha256=runtime['static_test_library_sha256'],source_head=runtime['source_head'],controlled_native_calls_not_gameplay=True))
 write(out/'runtime.json',dict(**runtime,reused=True,new_dll_build=False,production_audio_source_changed=False))
 reg=read(out/'regression48.json');scenes=read(out/'scenes16.json');legacy=read(out/'legacy12.json');cross=read(out/'cross-title.json');frontend=read(out/'frontend.json');latency=read(out/'latency.json')
 assert len(reg['rows'])==48 and all(x['pcm_exact'] and x['event_exact'] and x['returncode']==0 for x in reg['rows'])
 assert len(scenes['comparisons'])==16 and all(x['pcm_exact'] and x['event_exact'] and x['initial_ram_exact'] for x in scenes['comparisons'])
 assert len(legacy['cases'])==12 and legacy['passed']
 assert len(cross['rows'])==4
 # Revalidate the retained public/common-bridge baselines; no synthetic PASS or golden replacement.
 original=read(r/'build-phase6/regression48/results.json');assert len(original['checks'])==48
 assert sum(x['byte_exact'] for x in original['checks'])==46
 for ref in original['runs']:
  if ref['label'].endswith('-v03'):assert sha(r/f'build-phase6/regression48/{ref["label"]}/callback.s16le')==ref['callback_sha256']
 earlier=read(r/'docs/three-x-phase6-evidence/regression48.json');assert earlier['unexplained_difference_count']==0
 write(out/'public48-inherited.json',dict(scope='Revalidated retained v0.3 core/common Phase6 bridge reference PCM, plus new48/48 match to Phase6 candidate. Public core was not executed again for full48 in Phase7.',strict_exact=46,justified_differences=2,unexplained=0,
  changed_cases=['AORJ-2x','AORJ-recovery'],reason='Phase6 excludes only the independently verified natural-completion STOP202 at2x. Other semantic commands, clock advances and callback counts match. Original package comparison is newly rerun for16 loaded-BGM conditions.',source='docs/three-x-phase6-evidence/regression48.json',source_sha256=sha(r/'docs/three-x-phase6-evidence/regression48.json')))
 before=read(r/'docs/three-x-phase6-evidence/three72.json');guards=read(r/'docs/three-x-phase6-evidence/guard-analysis.json')
 write(out/'inherited-safety.json',dict(three72_runs=len(before['runs']),three72_returncodes_ok=all(x['returncode']==0 for x in before['runs']),guard_verified=guards['verified'],guard_not_applicable=guards['not_applicable'],status='INHERITED_VERIFIED_IDENTICAL_AUDIO_SOURCE_AND_DLL; not newly rerun',queue_drop_max=max(x['queue_dropped'] for x in before['runs']),ring_underrun_max=max(x['underrun'] for x in before['runs']),ring_overrun_max=max(x['overrun'] for x in before['runs']),new_hard_guard_suite_pass=True,new_native_lifetime_branches=42,new_modes='Experimental/Conservative/Disabled in rerun48; Experimental3x in actual frontend and cross-title; native Disabled1x.',goldens_updated=False))
 protection.update(public_head=git(public,'rev-parse','HEAD'),public_status=git(public,'status','--short'),public_tag=git(public,'rev-parse','v0.3-preview'),protected_wavs_unchanged=True,new_wavs=0,
  phase7_private_bytes=sum(p.stat().st_size for p in d.rglob('*') if p.is_file()),deletions=False,moves=False,other_emulators_modified=False,pinned_reader_sha256=sha(pin))
 write(out/'protection.json',protection)
 bgm=[]
 for code,measure in scenes['bgm'].items():
  for x in measure['comparisons']:bgm.append(dict(game=code,speed=x['speed'],minimum_correlation=min(w['correlation'] for w in x['windows']),minimum_spectral_cosine=min(w['spectral_cosine'] for w in x['windows']),phrase_time_slope=x['phrase_time_slope'],dominant_frequencies_equal=all(w['reference_dominant_hz']==w['fast_dominant_hz'] for w in x['windows']),audio_clock_sample_rate_ratio=x['audio_clock_sample_rate_ratio'],scope='Six unretimed2-second loaded-BGM windows; no SFX/ear certification'))
 switches=[]
 for run in frontend['current']:
  if 'switch' in run['path']:
   report=run['report'];switches.append(dict(game=Path(run['path']).name.split('-')[0],segments=report['segments'],one_core_cpu_percent=report['one_core_cpu_percent'],fallback=report['fallback_reasons']+report['b6jj_fallback_reasons'],muted=True))
 human=[dict(id='H01',games=['AAMJ','AFXJ','AORJ','B6JJ'],item='BGM timbre/pitch/tempo and clicks at1x/2x/3x;19 manual listening references available'),
  dict(id='H02',games=['AORJ'],item='SE202 final repeated notes, individual lengths/pitch, duplicate attacks; Phase6 before/after and Windows1/2/3 references'),
  dict(id='H03',games=['AFXJ','AFEJ','AAKJ','A8CJ'],item='Finite-program audible role and shortened endings. First repair cancellation/epoch handling; current automatic lifetime result is FAIL.'),
  dict(id='H04',games=['AAMJ','AFXJ','AORJ','B6JJ'],item='Cursor/confirm/cancel/battle/loop/concurrent/burst/pitch-bend and accepted same-song replay in real gameplay'),
  dict(id='H05',games=['AAMJ','AFXJ','AORJ','B6JJ'],item='Perceptual recovery and switch continuity; pending independent SFX are intentionally discarded'),
  dict(id='H06',games=['AORJ'],item='Repeat2x physical output/capture with device timing and isolated endpoint; Phase7 muted repeats do not replace this'),
  dict(id='H07',games=['AAMJ','AFXJ','AORJ','B6JJ'],item='Long-run actual frontend output sustainability and physical audio underruns; Phase7 switch segments are8s, muted')]
 gates=[dict(id='A',name='1x/2x互換性',status='PASS',scope='New48/48 exact vsPhase6, new16/16 original/common scenes and12/12 suites; public common full48 inherited46 exact+2 justified. Not all SFX fidelity.'),
  dict(id='B',name='3xゲーム進行',status='PASS',scope='Actual hidden RetroArch ~2.953–2.995x;98.44–99.84% target for four8s-scene segments. Near3x, not strict3.000x or long-run guarantee.'),
  dict(id='C',name='BGMテンポ・音程',status='PASS',scope='Unretimed headless scene windows at2x/3x; same-scale correlation/spectra/phrase progression. Windows audible current runs and real-time sustainability remain INCONCLUSIVE.'),
  dict(id='D',name='SE発音と寿命',status='FAIL',scope='AORJ202 passes verified relative lifetime at1/2/3; four other observed finite programs shorten at2/3. Remaining SFX classes INCONCLUSIVE.'),
  dict(id='E',name='速度切替',status='PASS',scope='New48 and pending-switch traces, plus actual1→3→1→3 on four scenes; ring/fallback0. Perceptual continuity not certified.'),
  dict(id='F',name='State Load/Rewind',status='PASS',scope='Safety/rebinding/stale-event clearing in rerun48,12suites and10 pending tests. Inherited3x matrix unchanged; continuing SE intentionally discarded. No perfect audible restoration claim.'),
  dict(id='G',name='安全性',status='PASS',scope='No production guards changed; new hard-guard suite and42 native branches. Natural ring/queue errors0; retained26 guard checks+2 B6 injection not applicable.'),
  dict(id='H',name='実音声聴取',status='INCONCLUSIVE',scope='HUMAN_REVIEW_REQUIRED. No Phase7 physical playback/capture; retained Windows recordings reused. Prior Phase3 BGM comment is not a new SFX review.')]
 summary=dict(schema_version=1,phase=7,date='2026-10-10',git=dict(worktree=str(r),branch=git(r,'branch','--show-current'),start_head='de16b98406b70d6c3602e9fa94375a2dd8326255',final_head='See final local report commit; no self-referential commit hash',push=False,tag=False,release=False),
  runtime=dict(source_head=runtime['source_head'],dlls=runtime['dlls'],reused=True),production_fix=False,
  aorj_latency=dict(status='INCONCLUSIVE_SOURCE_OF_TRANSIENT_STALL',event_play_samples=[10972]*3,candidate_first_input_effect_samples=[11782]*3,relative_event_to_candidate_onset_samples=810,relative_ms=810/32768*1000,
   repetition_counts='Three input/control pairs each: native1 and Fixed1/2/3. Three trials are deterministic; candidate onset exact across speeds.',
   callback_first_effect_samples=[12840,15034,15766],callback_difference_explanation='Native startup/Load recovery/AV rate/buffer coordinates; not directly wall-clock delay. No deterministic queue/bridge onset delay.',prior_stall_added_us=frontend['prior_stall_minus_previous_interval'],repeat2x_rates=frontend['aorj_2x_repeat_measured_speed'],repeat_stall_over100ms=frontend['repeated_stall_over_100ms'],production_change='None; source of transient OS/frontend/I/O scheduling not established.'),
  cross_title=cross['rows'],stop_generalization='NOT_ENABLED; multiple active-driver cancellation coverage and accepted/rejected Start/voice epochs incomplete. No blanket natural-STOP ignore.',
  regression=dict(new_phase6_candidate_exact=48,new_original_and_common_scene_exact=16,new_legacy_suites_pass=12,public_common_full48_inherited_strict=46,public_common_full48_justified=2,unexplained=0,goldens_updated=False),
  audio=dict(bgm=bgm,aorj202='PASS_AUTOMATED_SEVEN_REQUESTS_AND_IDENTICAL_LIFETIME; human review pending',b6jj='BGM windows/legacy dedicated-backend regressions PASS; new SFX listening INCONCLUSIVE',current_windows_audio='Not newly captured; existing Phase3–6 WAVs reused',listening_page=str(d/'listening-review/index.html'),listening_references=19,new_wavs=0),
  frontend=switches,recovery=dict(pending_tests=10,accepted_replay_unproved=4,continuous_pending_sfx_discarded=True,aamj_ownership_fallback_preserved=True,ffta_long_restore_runs=956),gates=gates,human_review_required=human,
  release_decision='HOLD_V04_RC_AND_PREVIEW; four confirmed finite-program truncations plus incomplete listening/cancellation proof',
  next_steps=['Model accepted Start generations and explicit cancel routes per active SDK, including Stop after native FINE and new-voice reuse','Fix the four confirmed2x/3x finite-program truncations without changing1x or ignoring loop/BGM cancellation; preserve guards and rerun regressions','Complete targeted physical hearing of repaired SFX and sustainable frontend output; isolate2x transient source if reproducible'],
  evidence=[dict(path=str(f.relative_to(r)).replace('\\','/'),sha256=sha(f)) for f in sorted(out.glob('*.json'))])
 write(r/'docs/THREE_X_PHASE7.json',summary);print('Collected Phase7:',len(summary['evidence']),'reports;48/48,16/16,12/12;RC HOLD;103 WAVs unchanged')

if __name__=='__main__':main()
