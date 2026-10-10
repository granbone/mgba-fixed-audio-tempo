"""Collect bounded Phase5 metadata and private listening clips. No playback.
SPDX-License-Identifier: MPL-2.0. Does not alter any prior phase artifact.
"""
import argparse,json,re,subprocess,wave
from pathlib import Path
from test_three_x_poc import sha

def read(p):return json.loads(p.read_text(encoding='utf8'))
def write(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf8')
def curate(root):
 private=root/'build-phase5';review=private/'listening-review';review.mkdir(exist_ok=False);manifest=[]
 prod=read(private/'stop-final-production/results.json');before=read(private/'stop-diagnostic/results.json')
 def clip(name,source,rate,begin,end,meta,raw=False):
  if raw:data=source.read_bytes()[int(begin*rate)*4:int(end*rate)*4];channels=2
  else:
   with wave.open(str(source),'rb') as f:
    rate=f.getframerate();channels=f.getnchannels();assert f.getsampwidth()==2;f.setpos(int(begin*rate));data=f.readframes(int((end-begin)*rate))
  path=review/name
  with wave.open(str(path),'wb') as f:f.setparams((channels,2,rate,0,'NONE','not compressed'));f.writeframes(data)
  manifest.append(dict(file=str(path),sha256=sha(path),source=str(source),source_sha256=sha(source),source_begin=begin,source_end=end,rate=rate,seconds=len(data)/channels/2/rate,game='AORJ',human_review='HUMAN_REVIEW_REQUIRED',**meta))
 for speed in (1,2,3):
  source=private/f'stop-final-production/experimental-{speed}/callback.s16le'
  keys=list(prod['inputs']);clip(f'0{speed}-Phase5-{speed}x-SE202.wav',source,32768,.28,.78,dict(speed=speed,core_sha256=sha(private/'final/runtime/mgba_fixed_audio_libretro.dll'),bridge_sha256=sha(private/'final/runtime/libmgba_mp2k_bridge.dll'),source_head='3a3a1b2af1184eb962989385f848a993f8ad069e',scene='Same isolated pre-SE fixture, A at19*speed. Compare completion of repeated final key88 notes, not BGM mixture phase.'),True)
 source=private/'stop-diagnostic/experimental-3/callback.s16le'
 clip('04-Before-3x-SE202.wav',source,32768,.28,.78,dict(speed=3,core_sha256=sha(private/'diagnostic/runtime/mgba_fixed_audio_libretro.dll'),bridge_sha256=sha(root/'build-phase4/priority-probe-timing/build/libmgba_mp2k_bridge.dll'),source_head='ca19d9b711b5a44560f1c44ba42ea3459d6c3073 plus read-only lifetime probes; Phase4 audio semantics',scene='Inherited terminal synthetic STOP cuts first key88 note; compare03. Instrumented bridge matched Phase4 production in Phase4.'),True)
 live=read(private/'windows-AORJ-3-retry/results.json');cmd=next(x['elapsed'] for x in live['commands'] if x['command']=='RETROPAD' and x.get('value')==1)
 clip('05-Windows-Phase5-3x-SE202.wav',private/'windows-AORJ-3-retry/windows-loopback.wav',48000,max(0,cmd-.65),cmd+1.05,dict(speed=3,core_sha256=sha(private/'guarded/runtime/mgba_fixed_audio_libretro.dll'),bridge_sha256=sha(private/'guarded/runtime/libmgba_mp2k_bridge.dll'),source_head='6a67cd330b3e2b3880e4ad3da446ede9f37c062d production content',scene='Real Windows endpoint mix around A; provenance differs from final DLL. Single-SE headless PCM/event exact across guarded/final builds. Endpoint mix not process-isolated.'))
 synth=read(private/'final-isolated-sfx/results.json')
 for i,row in enumerate(synth['rows'],6):
  clip(f'0{i}-Isolated-{row["label"]}.wav',Path(row['wav']),32768,0,.8,dict(speed=3 if row['label']!='1x-reference' else 1,core_sha256=None,bridge_sha256=sha(private/'final/runtime/libmgba_mp2k_bridge.dll'),source_head='3a3a1b2af1184eb962989385f848a993f8ad069e',scene='Isolated direct bridge, no BGM or native output. Baseline/3x-batch PCM exact; explicit-stop is deliberately cancelled at sample2926, not a natural-completion model.'))
 write(review/'manifest.json',manifest)
 (review/'README.md').write_text('Private Phase5 listening materials; no automatic playback.\n01/02/03: same AORJ SE202 at1x/2x/3x.02 retains the old truncation;03 restores the full seven-note sequence.04 is the before3x truncation.05 is actual Windows output, with possible endpoint mix contamination.06/07 are identical isolated synthesis at different callback batch sizes;08 is an intentional explicit cancellation and should end early.\nFocus on the repeated final high notes, their length, cutoff, pitch and timbre. BGM phase differences in01..05 must not be mistaken for missing SFX. All auditory judgments remain HUMAN_REVIEW_REQUIRED. See manifest.json for DLL hashes/source heads.\n',encoding='utf8')
 print('Curated',len(manifest),'short clips, no playback.')

def collect(root):
 private=root/'build-phase5';e=root/'docs/three-x-phase5-evidence';e.mkdir(exist_ok=True)
 names={'regression48':'release-48/results.json','scenes16':'release-16/results.json','legacy12':'release-12/results.json','three36':'three-36/results.json','three-extra36':'three-extra/results.json','stop':'release-stop-analysis.json','synthesis':'final-isolated-sfx/results.json','performance':'performance/results.json','windows':'windows-AORJ-3-retry/results.json','listening':'listening-review/manifest.json'}
 for label,path in names.items():write(e/(label+'.json'),read(private/path))
 unit=next(json.loads(x) for x in (private/'final-lifetime.log').read_text(errors='replace').splitlines() if x.startswith('{"passed"'));assert unit['passed'];write(e/'lifetime-unit.json',unit)
 r=read(private/'release-48/results.json');s=read(private/'release-16/results.json');l=read(private/'release-12/results.json')
 assert len(r['checks'])==48 and all(c['byte_exact'] for c in r['checks'])
 assert len(s['comparisons'])==16 and all(c['pcm_exact'] and c['event_exact'] and c['initial_ram_exact'] for c in s['comparisons'])
 assert len(l['cases'])==12 and l['passed']
 originals=read(root/'docs/three-x-phase3-evidence/original-wavs.json')
 old=read(root/'docs/three-x-phase3-evidence/protection.json')['phase3_protected_wavs']
 p4=read(root/'docs/three-x-phase4-evidence/protection.json')
 protected=originals+old+p4['private_phase4_wavs_protected']
 assert all(sha(Path(x['path']))==x['sha256'] for x in protected)
 assert all(sha(Path(f))==h for f,h in p4['public_artifacts'].items())
 reader=root.parent/'mgba_public_v03_fresh/build-publication-inputs/agbplay/src/agbplay/SequenceReader.cpp';assert sha(reader)==p4['pinned_reader_sha256']
 public=root.parent/'mgba_public_v03_fresh'
 assert subprocess.check_output(['git','-C',str(public),'rev-parse','HEAD'],text=True).strip()=='91f153b4e7fd192872a892d81f37501b3a8d4304'
 assert not subprocess.check_output(['git','-C',str(public),'status','--short'],text=True).strip()
 tag=subprocess.check_output(['git','-C',str(public),'rev-parse','refs/tags/v0.3-preview'],text=True).strip()
 assert tag=='4e3f3bc196cf9575496429791650a89b85970f01'
 write(e/'protection.json',dict(protected_wav_hashes_verified=len(protected),phase2_27=True,phase3_25=True,phase4_16=True,public_artifacts=p4['public_artifacts'],public_source_head='91f153b4e7fd192872a892d81f37501b3a8d4304',public_tag=tag,public_status='clean',pinned_reader_sha256=sha(reader),deletions_or_moves=False,phase5_wav_count=len(list(private.rglob('*.wav'))),phase5_bytes=sum(f.stat().st_size for f in private.rglob('*') if f.is_file()),media_policy='Finite suite/build qualification output, no cleanup, ROM/save/state/media uncommitted;8 curated short clips and one bounded Windows original.'))
 runtime=dict(source_head='3a3a1b2af1184eb962989385f848a993f8ad069e',dlls={str(f):sha(f) for f in (private/'final/runtime').glob('*.dll')},runner_sha256=sha(private/'retro-runner.exe'),probe_sha256=sha(root/'build-phase4/priority-probe-timing/build/libmgba_mp2k_bridge.dll'))
 write(e/'runtime.json',runtime)
 before=read(root/'build-phase4/regression-main/results.json')['runs']+read(root/'build-phase4/regression-rest/results.json')['runs'];comparisons=[]
 for row in r['runs']:
  if not row['label'].endswith('-poc'):continue
  b=next(x for x in before if x['label']==row['label']);oldpath=root/('build-phase4/regression-main' if b['game_code'] in ['AAMJ','AFXJ','AFEJ','B6JJ','AORJ'] else 'build-phase4/regression-rest')/b['label']/'callback.s16le';newpath=private/'release-48'/row['label']/'callback.s16le'
  exact=True;offset=0
  with oldpath.open('rb') as x,newpath.open('rb') as y:
   while block:=x.read(1024*1024):
    if block!=y.read(len(block)):exact=False;break
    offset+=len(block)
  comparisons.append(dict(case=row['label'],phase4_frames=b['frames'],phase5_frames=row['frames'],matched_phase4_pcm_prefix=exact,compared_pcm_bytes=offset,full_pcm_exact=exact and oldpath.stat().st_size==newpath.stat().st_size,event_exact=b['event_sha256']==row['event_sha256'] if b['frames']==row['frames'] else None))
 assert len(comparisons)==48 and all(x['matched_phase4_pcm_prefix'] for x in comparisons)
 write(e/'phase4-compatibility.json',dict(comparisons=comparisons,method='No new1x/2x PCM changes vs Phase4.30 conditions have equal6000-frame full PCM/events;18 EWRAM/ROM conditions compare the complete old4800-frame PCM prefix to new6000-frame output; event hashes have unequal durations there and are not declared equal. Original v0.3 priority differences retain the Phase4 explanation, not a relaxed golden.'))
 stop=read(private/'release-stop-analysis.json');runs=read(private/'three-36/results.json')['runs']+read(private/'three-extra/results.json')['runs']
 summary=dict(schema_version=1,phase=5,date='2026-10-10',git=dict(worktree=str(root),branch='feature/3x-fixed-audio-poc',start_head='ca19d9b711b5a44560f1c44ba42ea3459d6c3073',implementation_commits=['6a67cd330b3e2b3880e4ad3da446ede9f37c062d','3a3a1b2af1184eb962989385f848a993f8ad069e'],source_head=runtime['source_head'],documentation_commit='See final git log; this report cannot contain its own commit hash.',push=False,tag=False,release=False),runtime=runtime,cause=stop['stop_origin'],scope=stop['scope'],aorj=stop['rows'],regression=dict(common_bridge_48=48,original_package_and_common_scenes16=16,legacy_suite12=12,phase4_pcm_compatibility48=True,goldens_updated=False),safety=dict(runs72=len(runs),natural_underrun_max=max(x['underrun'] for x in runs),natural_overrun_max=max(x['overrun'] for x in runs),queue_dropped_max=max(x['queue_dropped'] for x in runs),queue_peak_max=max(x['queue_peak'] for x in runs),known_fallbacks=[dict(case=x['label'],reasons=x['safety_fallback_reasons']) for x in runs if x['safety_fallback_reasons']],guards_weakened=False),release_decision='HOLD',remaining=['Inherited2x SE202 truncation retained to preserve existing compatibility','General classification of native completion vs explicit Stop outside exact AORJ SE2023x scope','Auditory timbre/pitch/SE listening, battles, looped and simultaneous SFX, pitch bend','Perceptual Load/Rewind continuity; frame-level polling coverage remains partial'],human_listening='HUMAN_REVIEW_REQUIRED',protected_wav_hashes_verified=len(protected),evidence=[dict(path=str(f.relative_to(root)).replace('\\','/'),sha256=sha(f)) for f in sorted(e.glob('*.json'))])
 write(root/'docs/THREE_X_PHASE5.json',summary);print('Collected',len(summary['evidence']),'reports;48/48,16/16,12/12; protected WAVs',len(protected))

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=Path.cwd());p.add_argument('--curate-only',action='store_true');a=p.parse_args();root=a.root.resolve()
 if a.curate_only:curate(root)
 else:collect(root)
if __name__=='__main__':main()
