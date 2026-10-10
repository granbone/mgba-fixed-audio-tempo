"""DAT-only public identities joined by exact hashes. SPDX-License-Identifier: MPL-2.0"""
import argparse
from collections import Counter
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
STATUSES = {
 'LIMITED_TEST_PASS': '動作確認済み（限定試験） / Limited test pass. Fixed Audio worked in specific test scenarios; no complete playthrough or guarantee for every scene, BGM, sound effect, or feature.',
 'EXPERIMENTAL_UNVERIFIED': '実験的対応（動作未確認） / Experimental unverified. Fixed Audioを試行する機能はありますが、正常動作は未確認です。 Static backend checks passed; live checks can still select native audio.',
 'NATIVE_FALLBACK': '通常音声へ切替 / Native fallback observed in a limited runtime test. A scenario result, not a permanent driver verdict.',
 'UNSUPPORTED': '現行実装では未対応 / Current backend rejected the analyzed identity. Native emulation remains available.',
 'UNKNOWN_DRIVER': '音源方式未特定 / Analysis exists but driver family is unidentified. This does not establish unsupported behavior.',
 'NOT_ANALYZED': '未解析 / Public DAT identity without reliable exact-identity driver analysis.'
}
FAMILIES = ['MP2K_ROM_PLAYER','MP2K_EWRAM_PLAYER','MP2K_MIXED / OTHER_MP2K','CUSTOM_DRIVER','OTHER_DRIVER','UNKNOWN_DRIVER','NOT_ANALYZED']
FIELDS = ['release_id','title','region','languages','revision','game_code','header_revision','crc32','sha1','sha256','rom_size',
 'driver_family','public_status','runtime_eligibility','verification_status','fixed_audio_2x','fixed_audio_3x','state_load','rewind',
 'fallback_behavior','tested_core_version','notes','metadata_source','evidence_sources',
 'bgm_verification_status','bgm_test_speeds','bgm_test_scope','bgm_tested_commit','bgm_evidence','se_verification_status']
WARNING_EN = 'Experimental Feature. Unverified games may crash, freeze, produce incorrect audio, or experience save-data loss. Back up saves and save states before testing. Use the Conservative or Disabled mode if problems occur.'
WARNING_JP = '未検証のゲームでは、クラッシュ、フリーズ、音声異常、セーブデータやステートセーブの破損等が発生する可能性があります。試用前にバックアップを作成してください。問題がある場合はConservativeまたはDisabledへ切り替えてください。動作保証のない実験版です。'
LIMIT_EN = 'Limited test pass means Fixed Audio was observed to work in specific test scenarios. It does not indicate a complete playthrough or guarantee correct behavior for every scene, BGM, sound effect, or feature.'
LIMIT_JP = '「動作確認済み（限定試験）」は、特定の場面においてFixed Audioの動作を確認したことを示します。ゲームを最後までプレイしたことや、すべてのBGM・SE・機能が正常に動作することを保証するものではありません。'
def load(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def write(path,obj):
 Path(path).parent.mkdir(parents=True,exist_ok=True)
 Path(path).write_text(json.dumps(obj,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def summary(rows):
 return dict(total_releases=len(rows),
  driver_families={f:dict(total=sum(r['driver_family']==f for r in rows),
   **{s:sum(r['driver_family']==f and r['public_status']==s for r in rows) for s in STATUSES},
   experimental_trial_eligible=sum(r['driver_family']==f and r['runtime_eligibility'] in ('EXPERIMENTAL_TRIAL','CONSERVATIVE_AND_EXPERIMENTAL') for r in rows)) for f in FAMILIES},
  public_status={s:Counter(r['public_status'] for r in rows)[s] for s in STATUSES},
  experimental_trial_eligible=sum(r['runtime_eligibility'] in ('EXPERIMENTAL_TRIAL','CONSERVATIVE_AND_EXPERIMENTAL') for r in rows))

def apply_bgm_evidence(root, db):
 """Independent, exact-identity overlay. Never promotes general/SE eligibility."""
 path=root/'compatibility/sources/v04-bgm-observations.json'
 entries=load(path)['records'] if path.exists() else []
 bysha={e['sha256']:e for e in entries}
 assert len(bysha)==len(entries)
 found=set()
 for r in db['records']:
  r.update(bgm_verification_status='NOT_TESTED',bgm_test_speeds=[],bgm_test_scope='',
   bgm_tested_commit='',bgm_evidence='',se_verification_status='NOT_ASSESSED_BY_BGM_TEST')
  e=bysha.get(r['sha256'])
  if not e:continue
  assert all(r[k]==e[k] for k in ('release_id','sha1','crc32','rom_size','game_code','revision'))
  found.add(r['sha256'])
  for k in ('bgm_verification_status','bgm_test_speeds','bgm_test_scope','bgm_tested_commit','bgm_evidence'):r[k]=e[k]
 assert found==set(bysha), 'Unmatched BGM identity'
 db.update(schema_version=3,version_candidate='v0.4-preview-rc1',
  bgm_summary=dict(Counter(r['bgm_verification_status'] for r in db['records'])),
  fixed_audio_3x_scope='Legacy general-validation field retained. Scoped 1x/2x/3x BGM evidence is recorded separately.')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=ROOT);a=p.parse_args();root=a.root
 ext=load(root/'compatibility/sources/no-intro-gba-releases.json')
 analysis=load(root/'compatibility/sources/local-scanner-evidence.json')
 acceptance=load(root/'compatibility/sources/integration-acceptance.json')
 runtime=load(root/'compatibility/sources/local-runtime-evidence.json')
 eligibility=load(root/'compatibility/sources/runtime-eligibility.json')
 observed=load(root/'compatibility/sources/v03-runtime-observations.json')
 accepted={e['rom_sha256']:e for e in acceptance['entries']}
 tested={e['sha256']:e for e in runtime['records']}
 eligible={e['sha256']:e for e in eligibility['records']}
 observations={e['sha256']:e for e in observed['records']}
 bysha={};bycrc={}
 for s in analysis['records']:
  if s['input_status']!='ROM_HEADER_VALID' or s['scan_error']:continue
  bysha.setdefault(s['sha1'],[]).append(s);bycrc.setdefault((s['crc32'],s['rom_size']),[]).append(s)
 rows=[];seen=set()
 for m in ext['releases']:
  key=(m['sha1'],m['rom_size'])
  if key in seen:continue
  seen.add(key)
  matches=[s for s in bysha.get(m['sha1'],[]) if s['rom_size']==m['rom_size'] and s['crc32']==m['crc32']]
  if not matches:
   matches=[s for s in bycrc.get((m['crc32'],m['rom_size']),[]) if not s.get('sha1')]
  unique={s['sha256']:s for s in matches}
  s=next(iter(unique.values())) if len(unique)==1 else None
  if s and m.get('game_code') and s.get('game_code') and m['game_code']!=s['game_code']:s=None
  r=dict.fromkeys(FIELDS)
  r.update({k:m.get(k) for k in ('title','region','languages','revision','game_code','crc32','sha1','rom_size')})
  r.update(release_id='gba-'+m['sha1'].lower(),driver_family='NOT_ANALYZED',public_status='NOT_ANALYZED',
   runtime_eligibility='NOT_ESTABLISHED',verification_status='NO_DRIVER_ANALYSIS',fixed_audio_2x='NOT_TESTED',
   fixed_audio_3x='NOT_YET_VALIDATED',state_load='NOT_TESTED',rewind='NOT_TESTED',fallback_behavior='NATIVE_UNLESS_BACKEND_VALIDATES',
   notes='Exact release/revision has no reliable driver analysis. No regional result is inherited.',metadata_source=ext['url'],evidence_sources=[])
  if s:
   r.update(sha256=s['sha256'],header_revision=s['revision'],evidence_sources=['EXACT_HASH_ANALYSIS'],verification_status='DRIVER_ANALYSIS_ONLY')
   driver=s['detected_driver'];variant=s['profile_variant']
   r['driver_family']='MP2K_'+variant if driver=='MP2K' and variant in ('ROM_PLAYER','EWRAM_PLAYER') else 'MP2K_MIXED / OTHER_MP2K' if driver=='MP2K' else 'UNKNOWN_DRIVER' if driver in ('UNKNOWN','UNKNOWN_NOT_SCANNED') else 'OTHER_DRIVER'
   r.update(public_status='UNKNOWN_DRIVER',notes='Driver analysis only. Detection is separate from trial eligibility and audio verification.')
   e=eligible.get(s['sha256'])
   if e:
    r['evidence_sources'].append('PRODUCTION_PROFILE_PROBE')
    if e['static_eligible'] is True:
     r.update(public_status='EXPERIMENTAL_UNVERIFIED',runtime_eligibility='EXPERIMENTAL_TRIAL',
      notes='Implemented backend passed production static table/hook/SoundMode checks. Live player/track checks, bridge initialization, event/buffer limits and timeouts can still cause native fallback. Correct BGM/SE is unverified.')
    elif e['static_eligible'] is False:
     r.update(public_status='UNSUPPORTED',runtime_eligibility='STATIC_REJECTED',notes='Current backend rejects this exact identity: '+e['reason']+'. Native audio remains available.')
   # A recognized GAX family has no implemented backend in this candidate.
   if driver=='GAX':
    r.update(public_status='UNSUPPORTED',runtime_eligibility='NO_IMPLEMENTED_BACKEND',notes='GAX driver identified. This candidate has no GAX Fixed Audio backend; native audio is used.')
   t=tested.get(s['sha256'])
   if t and not any(x['activated'] for x in t['runs']) and r['runtime_eligibility']!='EXPERIMENTAL_TRIAL':
    r.update(public_status='NATIVE_FALLBACK',verification_status='NATIVE_FALLBACK_OBSERVED',fixed_audio_2x='NATIVE_FALLBACK',
     fallback_behavior='NATIVE_AUDIO_OBSERVED',tested_core_version='v0.2-preview integration baseline',notes=r['notes']+' Native fallback was observed in prior 1x/2x/switch scenarios.')
   e=accepted.get(s['sha256'])
   if e:
    assert (e['rom_crc32'],e['rom_size'])==(s['crc32'],s['rom_size'])
    assert e['fast_forward_2x']==e['load']==e['rewind']=='PASS'
    r.update(public_status='LIMITED_TEST_PASS',runtime_eligibility='CONSERVATIVE_AND_EXPERIMENTAL',verification_status='LIMITED_AUDIO_COMPARISON_PASS',
     fixed_audio_2x='PASS',state_load=e['load'],rewind=e['rewind'],fallback_behavior='GUARDED_NATIVE_FALLBACK',tested_core_version='v0.2-preview integration baseline',
     notes='Prior exact-identity limited PCM/event comparison, 1x/2x switching, BGM/SE and recovery evidence. Full playthrough and every audio scene are unverified.',
     evidence_sources=r['evidence_sources']+['INTEGRATION_PCM_EVENT_ACCEPTANCE'])
    if e['game_code']=='B6JJ':r['driver_family']='CUSTOM_DRIVER'
    if e['game_code']=='AFXJ':r['notes']+=' Late-state recovery can retain native audio for about 18 seconds.'
   o=observations.get(s['sha256'])
   if o:
    r.update(tested_core_version='v0.3-preview candidate',verification_status=o['verification_status'],notes=r['notes']+' '+o['notes'])
    r['evidence_sources'].append('V03_RUNTIME_OBSERVATION')
    if o['result']=='Fallback':
     r.update(public_status='NATIVE_FALLBACK',fixed_audio_2x='NATIVE_FALLBACK',fallback_behavior='NATIVE_AUDIO_OBSERVED')
    # ACTIVE alone never promotes public_status.
  rows.append(r)
 rows.sort(key=lambda r:(r['title'].casefold(),r['sha1']))
 assert all(r['driver_family'] in FAMILIES for r in rows)
 db=dict(schema_version=2,version_candidate='v0.3-preview',metadata_scope=ext['scope'],status_definitions=STATUSES,
  warnings=dict(en=WARNING_EN,ja=WARNING_JP),limited_test_scope=dict(en=LIMIT_EN,ja=LIMIT_JP),
  sources=[dict(id=ext['source_id'],url=ext['url'],revision=ext['revision'],sha256=ext['dat_sha256'],acquired=ext['acquired'],
   license='CC BY-SA 4.0 (libretro distribution)',scope=ext['scope']),
   dict(id='EXACT_IDENTITY_EVIDENCE',url='sources/runtime-eligibility.json',acquired='2026-10-09',license='CC BY-SA 4.0',scope='Hash-only analysis and production static eligibility. No audio correctness inferred.'),
   dict(id='LIMITED_RUNTIME_TESTS',url='sources/v03-runtime-observations.json',acquired='2026-10-09',license='CC BY-SA 4.0',scope='Scenario-specific observations. Prior limited PCM/event acceptance retained separately.')],
  gb_production_support=False,fixed_audio_3x='NOT_YET_VALIDATED',summary=summary(rows),records=rows)
 apply_bgm_evidence(root,db)
 write(root/'compatibility/gba-compatibility.json',db)
 print(json.dumps(db['summary'],indent=2))
if __name__=='__main__':main()
