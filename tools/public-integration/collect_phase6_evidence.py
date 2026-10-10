"""Collect Phase6 metadata and bounded private listening clips; no playback.
SPDX-License-Identifier: MPL-2.0. Previous protected assets remain read-only.
"""
import argparse,json,re,subprocess,wave
from pathlib import Path
from test_three_x_poc import sha

SOURCE='d08add4741f454f3a459343067b0bb275e998e34'
def read(p):return json.loads(p.read_text(encoding='utf8'))
def write(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf8')
def curate(root):
    d=root/'build-phase6';review=d/'listening-review';review.mkdir(exist_ok=False);rows=[]
    def clip(name,source,begin,end,rate,meta,raw=False):
        if raw:data=source.read_bytes()[int(begin*rate)*4:int(end*rate)*4];channels=2
        else:
            with wave.open(str(source),'rb') as f:
                rate=f.getframerate();channels=f.getnchannels();assert f.getsampwidth()==2
                f.setpos(int(begin*rate));data=f.readframes(int((end-begin)*rate))
        path=review/name
        with wave.open(str(path),'wb') as f:f.setparams((channels,2,rate,0,'NONE','not compressed'));f.writeframes(data)
        rows.append(dict(path=str(path),sha256=sha(path),source=str(source),source_sha256=sha(source),begin=begin,end=end,
            rate=rate,seconds=len(data)/channels/2/rate,game='AORJ',song=202,human_review='HUMAN_REVIEW_REQUIRED',**meta))
    core=d/'final/runtime/mgba_fixed_audio_libretro.dll';bridge=core.with_name('libmgba_mp2k_bridge.dll')
    common=dict(core_sha256=sha(core),bridge_sha256=sha(bridge),source_head=SOURCE)
    for speed in (1,2,3):
        clip(f'0{speed}-Phase6-{speed}x-SE202.wav',d/f'final-stop-production/experimental-{speed}/callback.s16le',.28,.78,32768,
            dict(speed=speed,scene='Same pre-SE state; A at19*speed game frames. Focus on the repeated final key88 notes; BGM mixture phase is not an SFX omission.',**common),True)
    clip('04-Phase5-before-2x-SE202.wav',root/'build-phase5/stop-final-probe/experimental-2/callback.s16le',.28,.78,32768,
        dict(speed=2,scene='Protected Phase5 truncated 2x reference; compare02. Probe PCM was verified equal to production.',
            core_sha256=sha(root/'build-phase5/final/runtime/mgba_fixed_audio_libretro.dll'),bridge_sha256=sha(root/'build-phase4/priority-probe-timing/build/libmgba_mp2k_bridge.dll'),source_head='3a3a1b2af1184eb962989385f848a993f8ad069e'),True)
    for index,speed in enumerate((1,2,3),5):
        source=d/(f'windows-AORJ-{speed}' if speed==1 else f'windows-quiet-AORJ-{speed}')
        report=read(source/'results.json');pressed=next(x['elapsed'] for x in report['commands'] if x['command']=='RETROPAD' and x.get('value')==1)
        clip(f'0{index}-Windows-{speed}x-SE202.wav',source/'windows-loopback.wav',max(0,pressed-.65),pressed+1.05,48000,
            dict(speed=speed,scene='Real Windows endpoint mix around the single A press. Not process-isolated; no automatic listening judgment.',
                measured_speed_x=report['segments'][0]['measured_speed_x'],**common))
    write(review/'manifest.json',rows)
    (review/'README.md').write_text('Private Phase6 review; no automatic playback.\n01/02/03 are same-fixture1x/2x/3x.04 is the protected Phase5 2x truncation: compare02, especially the last two repeated high notes and the release.05/06/07 are real Windows output around one identified SE202 trigger. Listen for individual note duration/pitch, the final cutoff, BGM overlap and unwanted duplicate attacks. Faster game-side trigger spacing is a separate property. Windows output is an endpoint mix. All listening items remain HUMAN_REVIEW_REQUIRED. See manifest.json for source, DLL hashes, speed measurements and sample cuts.\n',encoding='utf8')
    print('Created7 short clips; no playback.')
def collect(root):
    d=root/'build-phase6';out=root/'docs/three-x-phase6-evidence';out.mkdir(exist_ok=False)
    files={'stop':'final-stop-analysis.json','regression48':'regression-analysis.json','scenes16':'scenes16/results.json',
        'legacy12':'legacy12/results.json','three72':'three72/results.json','guards28':'guards28/results.json',
        'polling':'polling-analysis.json','generic-lifetime':'generic-lifetime-final.json','replay':'replay-final/results.json',
        'synthesis':'final-isolated-sfx/results.json','priority':'native-priority/results.json','jobs':'matrix-jobs.json',
        'listening':'listening-review/manifest.json'}
    for name,path in files.items():write(out/(name+'.json'),read(d/path))
    protected=read(d/'protected-inputs.json')
    for row in protected['wavs']:assert sha(Path(row['path']))==row['sha256']
    for path,h in protected['public_artifacts'].items():assert sha(Path(path))==h
    public=root.parent/'mgba_public_v03_fresh'
    git=lambda *args:subprocess.check_output(['git','-C',str(public),*args],text=True).strip()
    assert git('rev-parse','HEAD')=='91f153b4e7fd192872a892d81f37501b3a8d4304' and git('status','--porcelain')==''
    assert git('rev-parse','v0.3-preview')=='4e3f3bc196cf9575496429791650a89b85970f01'
    reader=public/'build-publication-inputs/agbplay/src/agbplay/SequenceReader.cpp'
    assert sha(reader)=='7B77F0A163F9E2EA7BAB3714777764808CDD88ACD82F9707D588520FCE712AC0'
    protection=dict(protected_wav_count=len(protected['wavs']),protected_wavs=protected['wavs'],public_artifacts=protected['public_artifacts'],
        public_head=git('rev-parse','HEAD'),public_tag=git('rev-parse','v0.3-preview'),public_status='clean',pinned_reader_sha256=sha(reader),
        old_commits_retained=subprocess.run(['git','merge-base','--is-ancestor','e1db58ff4421715daefee0be13099e6b1202c8ca','HEAD'],cwd=root).returncode==0,
        new_private_wavs=[dict(path=str(f),sha256=sha(f)) for f in sorted(d.rglob('*.wav'))],
        phase6_bytes=sum(f.stat().st_size for f in d.rglob('*') if f.is_file()),deletions=False,moves=False,other_emulator_folders_modified=False)
    write(out/'protection.json',protection)
    runtime=dict(source_head=SOURCE,dlls={str(f):sha(f) for f in (d/'final/runtime').glob('*.dll')},
        runner_sha256=sha(d/'retro-runner.exe'),static_test_library_sha256=sha(d/'final/core-build/libmgba.a'),
        instrumentation_bridge_sha256=sha(root/'build-phase4/priority-probe-timing/build/libmgba_mp2k_bridge.dll'))
    write(out/'runtime.json',runtime)
    units=[json.loads(next(s for s in reversed((d/f'accepted-lifetime-{speed}.log').read_text(errors='replace').splitlines()) if s.startswith('{'))) for speed in (2,3)]
    assert all(x['passed'] for x in units);write(out/'native-lifetime.json',dict(results=units,
        limits='Controlled native calls and private in-memory guard faults, not gameplay/listening. Before-FINE Stop retains enabled flags and is rejected as natural; the existing polling path and direct bridge Stop preserve cancellation.',
        rejected_trials=['replay: no second game request, now explicitly INCONCLUSIVE','reuse: initial artificial entry PC was unsuitable for full function execution; corrected pipeline in bounded nativeCall','reuse: native Start initializes enabled flags with additional bits; mask and retained flags are verified, not equality to0x80']))
    live={name:read(d/name/'results.json') for name in ('windows-AORJ-1','windows-AORJ-2','windows-AORJ-3','windows-quiet-AORJ-2','windows-quiet-AORJ-3','quiet-3-probe','quiet-2-phase5','quiet-2-phase6')}
    write(out/'windows.json',dict(runs=live,initial_concurrent_tests='3x2.499x /27296samples_per_second: not certified as3x or1x real-time audio',human_review='HUMAN_REVIEW_REQUIRED'))
    reg=read(out/'regression48.json');scenes=read(out/'scenes16.json');legacy=read(out/'legacy12.json');three=read(out/'three72.json')['runs'];guards=read(out/'guards28.json')['runs']
    assert reg['unexplained_difference_count']==0 and len(reg['rows'])==48
    assert len(scenes['comparisons'])==16 and all(x['pcm_exact'] and x['event_exact'] for x in scenes['comparisons'])
    assert legacy['passed'] and len(legacy['cases'])==12
    assert len(three)==72 and len(guards)==28 and all(x['returncode']==0 for x in three+guards)
    guard_checks=[]
    for run in guards:
        label=run['label'];reasons=run['safety_fallback_reasons'];applies=True
        if 'explicit-disabled-conflict' in label:assert not run['fixed_activation'] and not run['final_fixed']
        elif run['game_code']=='B6JJ' and ('ring-overflow' in label or 'ring-shortage' in label):applies=False
        elif '-rate-' in label:
            reason='UNSUPPORTED_FRONTEND_CLOCK' if run['game_code']=='B6JJ' else 'FRONTEND_THROTTLE_TEMPORARILY_UNSUPPORTED'
            assert any(reason in x for x in reasons)
        elif '-partial' in label:assert any('FRONTEND_PARTIAL_AUDIO_BATCH' in x for x in reasons)
        elif 'ring-overflow' in label:assert run['overrun']==1 and any('RING_OVERRUN' in x for x in reasons)
        elif 'ring-shortage' in label:assert run['underrun']==1 and any('RING_UNDERRUN_OR_PENDING_FIRST_TICK' in x for x in reasons)
        else:raise AssertionError(label)
        guard_checks.append(dict(case=label,status='PASS_EXPECTED_GUARD' if applies else 'NOT_APPLICABLE_MP2K_INJECTION_TO_B6JJ_RING',reasons=reasons))
    write(out/'guard-analysis.json',dict(checks=guard_checks,verified=26,not_applicable=2,
        natural_ring_errors=0,injected_errors_do_not_measure_audio_quality=True))
    summary=dict(schema_version=1,phase=6,date='2026-10-10',
        git=dict(worktree=str(root),branch='feature/3x-fixed-audio-poc',start_head='e1db58ff4421715daefee0be13099e6b1202c8ca',implementation_head=SOURCE,final_head='See final git log; report cannot contain its own commit hash',push=False,tag=False,release=False),
        runtime=runtime,aorj=read(out/'stop.json')['rows'],
        regression=dict(strict_exact=reg['strict_exact_count'],conditions=48,justified_corrections=reg['intentional_difference_count'],unexplained=0,scenes16=16,legacy12=12,goldens_updated=False,
            original_package_scope='16 loaded-scene comparisons include original core/bridge. The48 matrix uses the common priority-corrected bridge; original full48 package was not rerun.'),
        safety=dict(three_runs=72,guard_runs=28,guard_verified=26,guard_not_applicable=2,underrun_max=max(x['underrun'] for x in three),overrun_max=max(x['overrun'] for x in three),queue_dropped_max=max(x['queue_dropped'] for x in three),queue_peak_max=max(x['queue_peak'] for x in three),
            known_fallbacks=[dict(case=x['label'],reasons=x['safety_fallback_reasons']) for x in three if x['safety_fallback_reasons']],guards_weakened=False),
        generic_rollout='NOT_ENABLED_PENDING_CANCELLATION_PROOF',new_title_song_exceptions=0,
        human_review='HUMAN_REVIEW_REQUIRED',release_decision='HOLD',protected_original_wavs_verified=len(protected['wavs']),
        remaining=['Native-finish versus explicit-cancel proof and accepted Start epochs for other MP2K drivers','Audible battle/loop/concurrent/pitch-bend SFX and cancellation intent','Gameplay repeat fixture: second A did not request SE202','Perceptual Load/Rewind continuity; pending voices are intentionally discarded','Long-run real frontend sustainability and output endpoint mix isolation'],
        evidence=[dict(path=str(f.relative_to(root)).replace('\\','/'),sha256=sha(f)) for f in sorted(out.glob('*.json'))])
    write(root/'docs/THREE_X_PHASE6.json',summary);print('Collected',len(summary['evidence']),'reports;strict',reg['strict_exact_count'],'/48;protected',len(protected['wavs']))
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=Path.cwd());p.add_argument('--curate-only',action='store_true');a=p.parse_args()
    if a.curate_only:curate(a.root.resolve())
    else:collect(a.root.resolve())
if __name__=='__main__':main()
